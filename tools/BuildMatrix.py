#!/usr/bin/env python3
"""Standard Python entry point for Kotonoha's macOS build matrix.

CMakePresets.json is the single source of truth for every native build.
The vcpkg "universal" mode builds both macOS triplets and joins their
bundles with lipo, producing arm64 + x86_64 .apps.

    python3 tools/BuildMatrix.py --provider vcpkg --arch universal
    python3 tools/BuildMatrix.py --provider vcpkg --arch arm64
    python3 tools/BuildMatrix.py --provider vcpkg --arch x86_64
    python3 tools/BuildMatrix.py --provider macports --arch x86_64
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILDS = ROOT / "out/build"

PROVIDERS = ("vcpkg", "brew", "macports")
ARCHS = ("arm64", "x86_64", "universal")


def say(message: str) -> None:
    print(f"[Kotonoha/BuildMatrix] {message}", flush=True)


def brew_available() -> bool:
    return (Path("/opt/homebrew/bin/brew").is_file() or
            Path("/usr/local/bin/brew").is_file() or
            shutil.which("brew") is not None)


def archs_of(binary: Path) -> set[str]:
    try:
        result = subprocess.run(["lipo", "-archs", str(binary)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            return set()
        return set(result.stdout.split())
    except OSError:
        return set()


def join_universal_bundles() -> Path:
    """Lipo the arm64 and amd64 vcpkg bundles into universal apps."""
    arm = BUILDS / "macos-arm64-vcpkg/outputs"
    amd = BUILDS / "macos-amd64-vcpkg/outputs"
    # Pacote final: apenas os apps universais em out/macos-universal-vcpkg/.
    # Os builds de arm64 e amd64 permanecem em out/build/ (separadamente).
    dest_root = ROOT / "out/macos-universal-vcpkg"
    dest_root.mkdir(parents=True, exist_ok=True)
    joined = 0
    for app in sorted(arm.glob("*.app")):
        other = amd / app.name
        if not other.is_dir():
            say(f"AVISO: bundle ausente no build amd64: {app.name}")
            continue
        target = dest_root / app.name
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(app, target, symlinks=True)
        # Files present in only one architecture (e.g. libiconv, ignored by
        # the arm64 link) still belong in the joined bundle: the x86_64 slice
        # references them, while the arm64 slice does not.
        for source_item in other.rglob("*"):
            relative = source_item.relative_to(other)
            destination = target / relative
            if destination.exists() or destination.is_symlink():
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            if source_item.is_symlink():
                destination.symlink_to(os.readlink(source_item))
            elif source_item.is_file():
                shutil.copy2(source_item, destination)
        for item in target.rglob("*"):
            if item.is_symlink() or not item.is_file():
                continue
            counterpart = other / item.relative_to(target)
            if not counterpart.is_file():
                continue
            item_archs = archs_of(item)
            other_archs = archs_of(counterpart)
            if not item_archs or not other_archs or item_archs == other_archs:
                continue
            if "arm64" not in item_archs and "arm64" not in other_archs:
                continue
            if "x86_64" not in item_archs and "x86_64" not in other_archs:
                continue
            merged = item.with_name(item.name + ".universal")
            result = subprocess.run(
                ["lipo", "-create", str(item), str(counterpart),
                 "-output", str(merged)], capture_output=True, text=True)
            if result.returncode != 0:
                say(f"AVISO: lipo falhou para {item.name}: {result.stderr.strip()}")
                merged.unlink(missing_ok=True)
                continue
            merged.replace(item)
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-",
                        str(target)], capture_output=True)
        main = next((target / "Contents/MacOS").iterdir(), None)
        say(f"Universal: {app.name} -> "
            f"{sorted(archs_of(main)) if main else 'sem executavel'}")
        joined += 1
    say(f"Bundles universais: {joined} em {dest_root}")
    return dest_root


def build_preset(preset: str, action: str, jobs: int, clean: bool) -> None:
    build_dir = BUILDS / preset
    if clean and build_dir.exists():
        shutil.rmtree(build_dir)
    if action in ("configure", "all"):
        cmd = ["cmake", "--preset", preset]
        say(" ".join(cmd))
        subprocess.run(cmd, check=True, cwd=ROOT)
    if action in ("build", "all"):
        cmd = ["cmake", "--build", "--preset", preset,
               "--parallel", str(jobs)]
        say(" ".join(cmd))
        subprocess.run(cmd, check=True, cwd=ROOT)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider", choices=PROVIDERS, required=True)
    parser.add_argument("--arch", choices=ARCHS, required=True)
    parser.add_argument("--action", choices=("configure", "build", "all"),
                        default="all")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--clean", action="store_true",
                        help="Remove os diretorios de build antes de configurar")
    args = parser.parse_args()

    try:
        if args.provider == "brew" and not brew_available():
            raise RuntimeError(
                "Homebrew indisponivel nesta maquina (requer Apple Silicon)")
        if args.provider in ("brew", "macports") and args.arch == "arm64":
            say("AVISO: system packages sao nativos; cross-compilar arm64 "
                "pode falhar na linkagem. Use vcpkg para arm64.")

        if args.provider == "vcpkg" and args.arch == "universal":
            for arch in ("arm64", "amd64"):
                say(f"=== vcpkg universal: preset macos-{arch}-vcpkg ===")
                build_preset(f"macos-{arch}-vcpkg", args.action, args.jobs,
                             args.clean)
            if args.action in ("build", "all"):
                join_universal_bundles()
            say(f"OK: {ROOT / 'out/macos-universal-vcpkg'} (arm64 + x86_64)")
            return 0

        arch_name = "arm64" if args.arch == "arm64" else "amd64"
        preset = f"macos-{arch_name}-{args.provider}"
        build_preset(preset, args.action, args.jobs, args.clean)
        say(f"OK: {BUILDS / preset} (preset {preset})")
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"error: [Kotonoha/BuildMatrix] {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
