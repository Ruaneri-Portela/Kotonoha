#!/usr/bin/env python3
"""Prepare vcpkg and BeeWare CPython 3.15 for Kotonoha on Apple platforms.

Source archives are cached in build/<platform>-deps/python; Python.xcframework
is installed to buildsystems/xcode/python/<platform> for Xcode's references.

    python3 tools/PrepareApple.py
    python3 tools/PrepareApple.py --platform ios
    python3 tools/PrepareApple.py --archive build/iOS-deps/python/Python-iOS.tar.gz
    python3 tools/PrepareApple.py --platform macos
    python3 tools/PrepareApple.py --only-vcpkg --platform ios
    python3 tools/PrepareApple.py --only-python --platform ios

By default, the script prepares vcpkg and Python for both platforms. Use
--only-vcpkg for apps not using Python. --archive alone selects iOS as before.
"""
from __future__ import annotations

import argparse
import contextlib
import copy
import fcntl
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
XCODE_PYTHON_DIR = ROOT / "buildsystems/xcode/python"
VERSION = "3.15"
BEEWARE_TAG = "3.15-b3"
IOS_ARCHIVE_NAME = "Python-3.15-iOS-support.b3.tar.gz"
MACOS_ARCHIVE_NAME = "Python-3.15-macOS-support.b3.tar.gz"
RELEASE_BASE = f"https://github.com/beeware/Python-Apple-support/releases/download/{BEEWARE_TAG}"
GITHUB_RELEASE_API = f"https://api.github.com/repos/beeware/Python-Apple-support/releases/tags/{BEEWARE_TAG}"
IOS_SHA256 = "429080dcfab3778b75bbc6548cbfd754dc943cfc6092a92b72022541ff3212a0"
USER_AGENT = "Kotonoha-PrepareApple/2.0"


def say(message: str) -> None:
    print(f"[Kotonoha/PrepareApple] {message}", flush=True)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verified(path: Path, expected: str) -> bool:
    return path.is_file() and sha256_file(path) == expected.lower()


def fetch_release_digest(asset_name: str) -> str:
    """Retrieve the SHA-256 *published by GitHub* for macOS release assets."""
    request = urllib.request.Request(
        GITHUB_RELEASE_API,
        headers={"User-Agent": USER_AGENT, "Accept": "application/vnd.github+json"},
    )
    with urllib.request.urlopen(request, timeout=45) as reply:
        release = json.load(reply)
    for asset in release.get("assets", []):
        if asset.get("name") == asset_name:
            value = asset.get("digest", "")
            if value.startswith("sha256:") and len(value) == len("sha256:") + 64:
                return value.removeprefix("sha256:").lower()
            raise RuntimeError(
                f"The GitHub release does not publish a SHA-256 for {asset_name}. "
                "Supply --macos-sha256 explicitly."
            )
    raise RuntimeError(f"Release {BEEWARE_TAG} does not list {asset_name}")


def download(url: str, path: Path, expected_sha256: str, force: bool) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and not force and verified(path, expected_sha256):
        say(f"Verified cached download: {path}")
        return path
    say(f"Downloading: {url}")
    temp = path.with_name(path.name + ".part")
    try:
        temp.unlink(missing_ok=True)
        request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        digest = hashlib.sha256()
        with urllib.request.urlopen(request, timeout=90) as response, temp.open("wb") as output:
            for block in iter(lambda: response.read(1024 * 1024), b""):
                output.write(block)
                digest.update(block)
        found = digest.hexdigest()
        if found != expected_sha256.lower():
            raise ValueError(f"SHA-256 mismatch for {url}: got {found}; expected {expected_sha256}")
        temp.replace(path)
    finally:
        temp.unlink(missing_ok=True)
    say(f"Downloaded and verified: {path}")
    return path


def _python_framework_roots(members: list[tarfile.TarInfo]) -> list[tuple[str, ...]]:
    """Identify *real* XCFramework trees by their own regular Info.plist.

    BeeWare archives can include links such as
    ``some/other/Python.xcframework -> ../Python.xcframework``. Those aliases
    must not be flattened to the extraction root: doing so changes their link
    meaning and can mask/collide with the actual XCFramework directory.
    """
    roots: set[tuple[str, ...]] = set()
    for member in members:
        parts = PurePosixPath(member.name).parts
        if member.isfile() and len(parts) >= 2 and parts[-2:] == ("Python.xcframework", "Info.plist"):
            roots.add(parts[:-1])
    return sorted(roots, key=lambda root: (len(root), root))


def _validate_selected_member(member: tarfile.TarInfo) -> None:
    """Validate renamed paths and any in-tree symlinks/hard links."""
    path = PurePosixPath(member.name)
    if path.is_absolute() or ".." in path.parts or "\\" in member.name:
        raise ValueError(f"Unsafe entry in Python archive: {member.name}")
    if not path.parts or path.parts[0] != "Python.xcframework":
        raise ValueError(f"Unexpected entry outside Python.xcframework: {member.name}")
    if member.isdev() or member.isfifo():
        raise ValueError(f"Unexpected special file in Python archive: {member.name}")
    if not (member.isdir() or member.isfile() or member.issym() or member.islnk()):
        raise ValueError(f"Unexpected archive entry type: {member.name}")
    if member.issym() or member.islnk():
        target = PurePosixPath(member.linkname)
        if target.is_absolute() or "\\" in member.linkname:
            raise ValueError(f"Unsafe archive link: {member.name} -> {member.linkname}")
        parts = list(path.parts[:-1] if member.issym() else [])
        for segment in target.parts:
            if segment == "..":
                if len(parts) <= 1:
                    raise ValueError(f"Escaping archive link: {member.name} -> {member.linkname}")
                parts.pop()
            elif segment not in ("", "."):
                parts.append(segment)
        if not parts or parts[0] != "Python.xcframework":
            raise ValueError(f"Escaping archive link: {member.name} -> {member.linkname}")


def install_xcframework(archive: Path, destination: Path) -> None:
    """Install only the *real* Python.xcframework tree, not archive aliases.

    An archive may contain a real XCFramework and separate symlink aliases to
    it. Instead of flattening every path containing ``Python.xcframework``
    (which produced the error ``Python.xcframework -> ../Python.xcframework``),
    select the tree with the real Info.plist and ignore unrelated aliases.
    """
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kotonoha-python-", dir=destination.parent) as staging:
        stage = Path(staging)
        with tarfile.open(archive, "r:gz") as tar:
            original_members = tar.getmembers()
            roots = _python_framework_roots(original_members)
            if not roots:
                raise RuntimeError(f"No real Python.xcframework/Info.plist found inside {archive}")

            def member_count(root: tuple[str, ...]) -> int:
                return sum(
                    len(PurePosixPath(entry.name).parts) > len(root)
                    and PurePosixPath(entry.name).parts[:len(root)] == root
                    for entry in original_members
                )

            # Prefer the root with the most actual content, not a symlink alias.
            root = max(roots, key=lambda item: (member_count(item), -len(item), item))
            say(f"Extracting real XCFramework tree: {'/'.join(root)}")
            selected: list[tarfile.TarInfo] = []
            for original in original_members:
                parts = PurePosixPath(original.name).parts
                if parts[:len(root)] != root:
                    continue
                tail = parts[len(root):]
                if not tail:
                    # A top-level symlink alias is never part of the real tree.
                    if original.issym() or original.islnk():
                        continue
                    if not original.isdir():
                        raise ValueError(f"XCFramework root is not a directory: {original.name}")
                item = copy.copy(original)
                item.name = "/".join(("Python.xcframework", *tail))
                if item.islnk():
                    # Hardlink paths in tar files are archive-root-relative.
                    link_parts = PurePosixPath(item.linkname).parts
                    if link_parts[:len(root)] != root:
                        raise ValueError(f"Hard link outside XCFramework: {original.name} -> {original.linkname}")
                    item.linkname = "/".join(("Python.xcframework", *link_parts[len(root):]))
                _validate_selected_member(item)
                selected.append(item)
            if not selected:
                raise RuntimeError(f"Python.xcframework contents missing from {archive}")

            # Using the same TarFile preserves data offsets; the filter also
            # checks resolved filesystem paths on Python versions supporting it.
            try:
                tar.extractall(stage, members=selected, filter="data")
            except TypeError as exc:
                if "filter" not in str(exc):
                    raise
                # Older macOS Python has no extraction filter; every selected
                # entry and link has already been restricted to this subtree.
                tar.extractall(stage, members=selected)
        installed = stage / "Python.xcframework"
        if not (installed / "Info.plist").is_file():
            raise RuntimeError(f"Invalid Python.xcframework in {archive}: Info.plist is missing")
        if destination.is_symlink() or destination.is_file():
            destination.unlink()
        elif destination.exists():
            shutil.rmtree(destination)
        shutil.move(str(installed), str(destination))
    say(f"Installed Python.xcframework: {destination}")


def ensure_xcframework(
    *, platform: str, archive: Path | None, checksum: str | None, force_download: bool
) -> Path:
    xc = XCODE_PYTHON_DIR / ("iOS" if platform == "ios" else "macOS") / "Python.xcframework"
    if archive is None and (xc / "Info.plist").is_file() and not force_download:
        say(f"Existing XCFramework will be reused: {xc}")
        return xc

    if platform == "ios":
        expected = checksum or IOS_SHA256
        name = IOS_ARCHIVE_NAME
        cached = ROOT / "build/iOS-deps/python/Python-iOS.tar.gz"
    else:
        name = MACOS_ARCHIVE_NAME
        expected = checksum or fetch_release_digest(name)
        cached = ROOT / "build/macOS-deps/python/Python-macOS.tar.gz"
    if archive is not None:
        archive = archive.resolve()
        if not archive.is_file():
            raise FileNotFoundError(f"Missing local Python archive: {archive}")
        if not verified(archive, expected):
            raise ValueError(
                f"SHA-256 mismatch for local Python archive: {archive}\n"
                f"  actual:   {sha256_file(archive)}\n"
                f"  expected: {expected}"
            )
        say(f"Verified local archive: {archive}")
    else:
        archive = download(f"{RELEASE_BASE}/{name}", cached, expected, force_download)
    install_xcframework(archive, xc)
    return xc


def run(command: list[str]) -> None:
    say("Running: " + " ".join(command))
    subprocess.run(command, check=True)


def ios_library_source(xc: Path) -> Path:
    common = xc / "lib/python3.15"
    if common.is_dir():
        return common
    for variant in ("ios-arm64", "ios-arm64_x86_64-simulator"):
        candidate = xc / variant / "lib/python3.15"
        if candidate.is_dir():
            return candidate
    raise RuntimeError(f"Python 3.15 standard library was not found in {xc}")


def prepare_ios(xc: Path, force_prepare: bool = False) -> None:
    source_stdlib = ios_library_source(xc)
    dest_base = ROOT / "build/iOS-deps/python/native"
    dest_base.mkdir(parents=True, exist_ok=True)
    for sdk, folder in (
        ("iphoneos", "ios-arm64"),
        ("iphonesimulator", "ios-arm64_x86_64-simulator"),
    ):
        native = dest_base / sdk
        std = native / "python/lib/python3.15"
        stamp = native / ".kotonoha-python-prepared"
        if std.is_dir() and stamp.is_file() and stamp.read_text(encoding="utf-8").strip() == BEEWARE_TAG and not force_prepare:
            say(f"Already prepared for {sdk}: {native}")
            continue
        platform = xc / folder
        if not platform.is_dir():
            raise RuntimeError(f"Python XCFramework has no {sdk} slice: {platform}")
        with tempfile.TemporaryDirectory(prefix=f"kotonoha-{sdk}-", dir=dest_base) as temp:
            staging = Path(temp) / sdk
            std = staging / "python/lib/python3.15"
            shutil.copytree(source_stdlib, std, symlinks=True)
            dynamic = std / "lib-dynload"
            dynamic.mkdir(exist_ok=True)
            extdirs = sorted(platform.glob("lib-*/python3.15/lib-dynload"))
            extdirs += sorted(platform.glob("lib/python3.15/lib-dynload"))
            files: dict[str, list[Path]] = {}
            for directory in extdirs:
                for library in directory.glob("*.so"):
                    files.setdefault(library.name, []).append(library)
            if not extdirs:
                say(f"Note: no architecture-specific extension directories found in {platform}")
            for filename, libraries in sorted(files.items()):
                module = filename.split(".", 1)[0]
                framework = staging / "Frameworks" / f"{module}.framework"
                framework.mkdir(parents=True, exist_ok=True)
                binary = framework / module
                if len(libraries) == 1:
                    shutil.copy2(libraries[0], binary)
                else:
                    run(["xcrun", "lipo", "-create", *map(str, libraries), "-output", str(binary)])
                run(["xcrun", "install_name_tool", "-id", f"@rpath/{module}.framework/{module}", str(binary)])
                metadata = {
                    "CFBundleExecutable": module,
                    "CFBundleIdentifier": f"org.python.extensions.{module.replace('_', '-')}",
                    "CFBundleName": module,
                    "CFBundlePackageType": "FMWK",
                    "CFBundleVersion": "1",
                    "MinimumOSVersion": "15.0",
                    "CFBundleSupportedPlatforms": [
                        "iPhoneSimulator" if sdk == "iphonesimulator" else "iPhoneOS"
                    ],
                }
                (framework / "Info.plist").write_bytes(plistlib.dumps(metadata))
                marker = dynamic / (filename.removesuffix(".so") + ".fwork")
                marker.write_text(f"Frameworks/{module}.framework/{module}", encoding="utf-8")
                (framework / f"{module}.origin").write_text(
                    f"python/lib/python3.15/lib-dynload/{marker.name}", encoding="utf-8"
                )
            (staging / ".kotonoha-python-prepared").write_text(BEEWARE_TAG + "\n", encoding="utf-8")
            if native.exists():
                shutil.rmtree(native)
            shutil.move(str(staging), str(native))
            say(f"Prepared {sdk}: {native} ({len(files)} extension frameworks)")


def prepare_macos(xc: Path, force_prepare: bool = False) -> None:
    framework = xc / "macos-arm64_x86_64/Python.framework"
    if not framework.is_dir():
        raise RuntimeError(f"Missing universal macOS Python.framework: {framework}")
    destination = ROOT / "build/macOS-deps/Frameworks/Python.framework"
    if destination.is_dir() and not force_prepare:
        say(f"Already prepared macOS: {destination}")
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kotonoha-macos-framework-", dir=destination.parent) as temp:
        target = Path(temp) / "Python.framework"
        shutil.copytree(framework, target, symlinks=True)
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(str(target), str(destination))
    say(f"Prepared macOS Python.framework: {destination}")



# vcpkg manifest is maintained by the main Kotonoha repository, not copied
# into the Xcode integration ZIP. Keep the existing Xcode installation layout.
VCPKG_INSTALLS = {
    "macosx": ("arm64-osx", "macos"),
    "iphoneos": ("arm64-ios", "ios"),
    "iphonesimulator": ("arm64-ios-simulator", "simulator"),
}

# These are the third-party libraries linked by Config/Common.xcconfig.
# z, bz2 and iconv may also come from Apple SDKs and are omitted from the
# completeness probe; vcpkg will still install whatever the manifest declares.
VCPKG_LINK_LIBRARIES = (
    "json-c", "ass", "harfbuzz", "fribidi", "freetype", "fontconfig",
    "brotlidec", "brotlicommon", "expat", "png", "unibreak",
    "avcodec", "avformat", "avutil", "swresample", "swscale",
)


def sdk_variants(platform: str, sdk: str | None) -> list[str]:
    if sdk and sdk not in VCPKG_INSTALLS:
        raise ValueError(f"Unsupported Apple SDK {sdk!r}; choose macosx, iphoneos or iphonesimulator")
    if platform == "macos":
        if sdk and sdk != "macosx":
            raise ValueError(f"--sdk {sdk} is incompatible with --platform macos")
        return ["macosx"]
    if platform == "ios":
        if sdk and sdk not in ("iphoneos", "iphonesimulator"):
            raise ValueError(f"--sdk {sdk} is incompatible with --platform ios")
        return [sdk] if sdk else ["iphoneos", "iphonesimulator"]
    return [sdk] if sdk else ["macosx", "iphoneos", "iphonesimulator"]


def vcpkg_install_root(sdk: str) -> Path:
    _, folder = VCPKG_INSTALLS[sdk]
    return ROOT / "buildsystems/xcode/vcpkg" / folder


def missing_vcpkg_libraries(sdk: str) -> list[str]:
    triplet, _ = VCPKG_INSTALLS[sdk]
    root = vcpkg_install_root(sdk) / triplet
    includes, libs = root / "include", root / "lib"
    if not includes.is_dir() or not libs.is_dir():
        return list(VCPKG_LINK_LIBRARIES)

    def present(name: str) -> bool:
        prefix = "lib" + name
        return any((libs / (prefix + ext)).is_file() for ext in (".a", ".dylib", ".tbd")) or any(libs.glob(prefix + ".*.dylib"))
    return [name for name in VCPKG_LINK_LIBRARIES if not present(name)]


def resolve_vcpkg(explicit: Path | None, *, allow_bootstrap: bool) -> Path:
    candidates: list[Path] = []
    if explicit is not None:
        candidates.append(explicit.expanduser())
    elif os.environ.get("VCPKG_EXE"):
        candidates.append(Path(os.environ["VCPKG_EXE"]).expanduser())
    else:
        if os.environ.get("VCPKG_ROOT"):
            candidates.append(Path(os.environ["VCPKG_ROOT"]).expanduser() / "vcpkg")
        exe_in_path = shutil.which("vcpkg")
        if exe_in_path:
            candidates.append(Path(exe_in_path))
        candidates.extend((ROOT / "vcpkg/vcpkg", ROOT / "tools/vcpkg/vcpkg", ROOT / "build/tools/vcpkg/vcpkg"))
    for path in candidates:
        if path.is_file() and os.access(path, os.X_OK):
            return path.resolve()
    if explicit is not None or os.environ.get("VCPKG_EXE"):
        raise FileNotFoundError(f"Requested vcpkg executable not found: {candidates[0]}")
    if not allow_bootstrap:
        raise FileNotFoundError("vcpkg executable not found (set VCPKG_ROOT, VCPKG_EXE or --vcpkg-executable)")
    if sys.platform != "darwin":
        raise RuntimeError("Bootstrap of Apple vcpkg dependencies requires macOS and Xcode command-line tools")
    destination = ROOT / "build/tools/vcpkg"
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not (destination / "bootstrap-vcpkg.sh").is_file():
        if destination.exists() and any(destination.iterdir()):
            raise RuntimeError(f"Incomplete vcpkg checkout at {destination}. Resolve it or use --vcpkg-executable.")
        say(f"Cloning upstream vcpkg to {destination}")
        run(["git", "clone", "https://github.com/microsoft/vcpkg.git", str(destination)])
    say("Bootstrapping vcpkg executable")
    run(["bash", str(destination / "bootstrap-vcpkg.sh"), "-disableMetrics"])
    executable = destination / "vcpkg"
    if not executable.is_file():
        raise RuntimeError("vcpkg bootstrap did not create the executable")
    return executable


@contextlib.contextmanager
def install_lock(sdk: str):
    """Serialize potentially concurrent builds sharing the same vcpkg destination."""
    lockpath = ROOT / "buildsystems/xcode/vcpkg" / f".prepare-{sdk}.lock"
    lockpath.parent.mkdir(parents=True, exist_ok=True)
    with lockpath.open("a+b") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)


def prepare_vcpkg(*, platform: str, sdk: str | None, mode: str, executable: Path | None) -> None:
    if mode == "skip":
        say("vcpkg preparation skipped by --vcpkg skip")
        return
    manifest = ROOT / "vcpkg.json"
    variants = sdk_variants(platform, sdk)
    for selected_sdk in variants:
        triplet, _ = VCPKG_INSTALLS[selected_sdk]
        root = vcpkg_install_root(selected_sdk)
        with install_lock(selected_sdk):
            missing = missing_vcpkg_libraries(selected_sdk)
            if mode != "install" and not missing:
                say(f"Verified vcpkg ({selected_sdk}, {triplet}): {root}")
                continue
            if mode == "check":
                raise RuntimeError(f"Missing vcpkg libraries for {triplet}: {', '.join(missing)}. Run with --vcpkg auto.")
            if not manifest.is_file():
                raise FileNotFoundError(f"Missing {manifest}; copy the integration to the root of the original Kotonoha repository")
            tool = resolve_vcpkg(executable, allow_bootstrap=True)
            say(f"Preparing vcpkg ({selected_sdk}, {triplet}) in {root}")
            # --x-install-root puts each triplet at the same directory referenced
            # by the Xcode .xcconfig files. The repo vcpkg.json drives packages.
            cmd = [str(tool), "install", f"--triplet={triplet}",
                   f"--x-manifest-root={ROOT}", f"--x-install-root={root}"]
            run(cmd)
            missing = missing_vcpkg_libraries(selected_sdk)
            if missing:
                raise RuntimeError(
                    f"vcpkg completed for {triplet}, but required libraries are still missing: "
                    f"{', '.join(missing)}. Check the root vcpkg.json, target triplet and Xcode linker flags."
                )
            say(f"vcpkg ready ({selected_sdk}, {triplet})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("ios", "macos", "all"), default=None)
    parser.add_argument("--sdk", choices=("iphoneos", "iphonesimulator", "macosx"),
                        help="Restrict vcpkg preparation to an Xcode SDK (default: all for selected platform)")
    parser.add_argument("--only-vcpkg", action="store_true", help="Prepare only vcpkg, for non-DokiDoki targets")
    parser.add_argument("--only-python", action="store_true", help="Prepare only CPython, without vcpkg")
    parser.add_argument("--vcpkg", choices=("auto", "install", "check", "skip"), default="auto",
                        help="auto reuses complete installs; install runs vcpkg; check only verifies; skip disables")
    parser.add_argument("--vcpkg-executable", type=Path, help="Use an existing vcpkg executable")
    parser.add_argument("--archive", type=Path, help="Existing iOS BeeWare tar.gz (compatible with legacy command)")
    parser.add_argument("--macos-archive", type=Path, help="Existing macOS BeeWare tar.gz")
    parser.add_argument("--sha256", help="Override iOS Python SHA-256 for a custom archive")
    parser.add_argument("--macos-sha256", help="Pin macOS Python SHA-256")
    parser.add_argument("--force-download", action="store_true", help="Redownload Python archives")
    parser.add_argument("--force-prepare", action="store_true", help="Rebuild prepared Python files")
    args = parser.parse_args()
    if args.only_python and args.only_vcpkg:
        parser.error("--only-vcpkg and --only-python are mutually exclusive")
    if args.only_vcpkg and (args.archive or args.macos_archive):
        parser.error("--archive/--macos-archive cannot be used with --only-vcpkg")
    selected = args.platform
    if selected is None:
        selected = ("ios" if args.archive and not args.macos_archive else
                    "macos" if args.macos_archive and not args.archive else "all")
    if args.sdk:
        sdk_variants(selected, args.sdk)
    try:
        say(f"Preparing Apple dependencies, platform={selected}, SDK={args.sdk or 'all'}")
        if not args.only_vcpkg:
            if selected in ("ios", "all"):
                ios = ensure_xcframework(
                    platform="ios", archive=args.archive,
                    checksum=args.sha256, force_download=args.force_download,
                )
                prepare_ios(ios, force_prepare=args.force_prepare or args.force_download or args.archive is not None)
            if selected in ("macos", "all"):
                macos = ensure_xcframework(
                    platform="macos", archive=args.macos_archive,
                    checksum=args.macos_sha256, force_download=args.force_download,
                )
                prepare_macos(macos, force_prepare=args.force_prepare or args.force_download or args.macos_archive is not None)
        if not args.only_python:
            prepare_vcpkg(platform=selected, sdk=args.sdk, mode=args.vcpkg, executable=args.vcpkg_executable)
    except (OSError, RuntimeError, ValueError, tarfile.TarError, urllib.error.URLError,
            subprocess.CalledProcessError) as exc:
        print(f"error: [Kotonoha/PrepareApple] {exc}", file=sys.stderr)
        return 1
    say("Done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
