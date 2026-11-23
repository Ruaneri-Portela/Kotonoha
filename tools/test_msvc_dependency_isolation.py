#!/usr/bin/env python3
"""Configuration-only MSVC provider smoke tests against fake native/MSYS trees.

Use a CMake LANGUAGE NONE project and set MSVC=TRUE. This checks provider
selection without pretending to exercise a real Microsoft compiler/linker.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
def dependency_file(name: str) -> Path:
    for folder in ("buildsystems/cmake/dependencies", "buildsystems/cmake"):
        candidate = ROOT / folder / name
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("Missing Kotonoha dependency module: " + name)


def put(path: Path, content: str = "") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def configure(project: Path, build: Path, *args: str,
              expected: bool = True, diagnostic: str = "") -> None:
    proc = subprocess.run(
        ["cmake", "-S", str(project), "-B", str(build), *args],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        check=False)
    if (proc.returncode == 0) != expected:
        raise AssertionError("Unexpected CMake result:\n" + proc.stdout[-6000:])
    if diagnostic and diagnostic not in proc.stdout:
        raise AssertionError("Missing " + diagnostic + ":\n" + proc.stdout[-6000:])


def make_sdl_config(prefix: Path, name: str) -> None:
    inc = (prefix / "include").as_posix()
    path = prefix / "share" / name / (name + "Config.cmake")
    put(path, (
        "if(NOT TARGET " + name + "::" + name + ")\n"
        "  add_library(" + name + "::" + name + " INTERFACE IMPORTED GLOBAL)\n"
        "  set_target_properties(" + name + "::" + name
        + " PROPERTIES INTERFACE_INCLUDE_DIRECTORIES \"" + inc + "\")\n"
        "endif()\n"
    ))
    (prefix / "include").mkdir(parents=True, exist_ok=True)


def test_sdl(work: Path) -> None:
    native = work / "vcpkg" / "x64-windows"
    host = work / "host-msys2"
    for prefix in (native, host):
        for name in ("SDL3", "SDL3_ttf"):
            make_sdl_config(prefix, name)
    project = work / "src"
    module = dependency_file("KotonohaSDL.cmake").as_posix()
    cmake = (
        "cmake_minimum_required(VERSION 3.16)\n"
        "project(TestMSVCSDL LANGUAGES NONE)\n"
        "set(MSVC TRUE)\n"
        'set(VCPKG_INSTALLED_DIR "' + native.parent.as_posix() + '")\n'
        'set(VCPKG_TARGET_TRIPLET "x64-windows")\n'
        'set(CMAKE_PREFIX_PATH "' + host.as_posix() + '")\n'
        'include("' + module + '")\n'
        'if(NOT KOTONOHA_SDL_PROVIDER_RESOLVED STREQUAL "VCPKG")\n'
        '  message(FATAL_ERROR "MSVC AUTO selected non-vcpkg SDL")\n'
        'endif()\n'
        'get_target_property(_sdl_inc SDL3::SDL3 INTERFACE_INCLUDE_DIRECTORIES)\n'
        'if(NOT _sdl_inc STREQUAL "' + (native / "include").as_posix() + '")\n'
        '  message(FATAL_ERROR "MSVC selected host SDL3 headers")\n'
        'endif()\n'
    )
    put(project / "CMakeLists.txt", cmake)
    configure(project, work / "native", "-DKOTONOHA_SDL_PROVIDER=AUTO",
              "-DSDL3_DIR=" + (host / "share/SDL3").as_posix(),
              "-DSDL3_ttf_DIR=" + (host / "share/SDL3_ttf").as_posix())
    configure(project, work / "system", "-DKOTONOHA_SDL_PROVIDER=SYSTEM",
              expected=False, diagnostic="SYSTEM is not supported with MSVC")
    put(project / "CMakeLists.txt", cmake.replace(
        'set(VCPKG_TARGET_TRIPLET "x64-windows")',
        'set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic")'))
    configure(project, work / "mingw", "-DKOTONOHA_SDL_PROVIDER=AUTO",
              expected=False, diagnostic="MinGW vcpkg triplet")


def test_media(work: Path) -> None:
    module = dependency_file("KotonohaMedia.cmake")
    if not module.is_file():
        print("[MSVC provider] No standalone media module on this branch")
        return
    native = work / "vcpkg" / "x64-windows"
    for rel in ("include/ass/ass.h", "lib/libass.a",
                "debug/lib/libass.a", "lib/libavcodec.a"):
        put(native / rel)
    put(native / "share/json-c/json-c-config.cmake",
        "add_library(json-c::json-c INTERFACE IMPORTED)\n")
    ffmpeg = work / "FindFFMPEG.cmake"
    native_header = (native / "include").as_posix()
    def write_ffmpeg(include: str) -> None:
        put(ffmpeg,
            'set(FFMPEG_FOUND TRUE)\n'
            'set(FFMPEG_INCLUDE_DIRS "' + include + '")\n'
            'set(FFMPEG_LIBRARY_DIRS "' + (native / "lib").as_posix() + '")\n'
            'set(FFMPEG_LIBRARIES "' + (native / "lib/libavcodec.a").as_posix() + '")\n')
    write_ffmpeg(native_header)
    project = work / "src"
    put(project / "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.16)\n"
        "project(TestMSVCMedia LANGUAGES NONE)\n"
        "set(MSVC TRUE)\n"
        "set(CMAKE_FIND_LIBRARY_PREFIXES lib)\n"
        "set(CMAKE_FIND_LIBRARY_SUFFIXES .a)\n"
        'set(VCPKG_INSTALLED_DIR "' + native.parent.as_posix() + '")\n'
        'set(VCPKG_TARGET_TRIPLET "x64-windows")\n'
        'set(PKG_CONFIG_EXECUTABLE "' + (work / "missing-pkg-config").as_posix() + '")\n'
        'list(PREPEND CMAKE_MODULE_PATH "' + work.as_posix() + '")\n'
        'include("' + module.as_posix() + '")\n'
        'if(NOT TARGET kotonoha_ass OR NOT TARGET kotonoha_ffmpeg)\n'
        '  message(FATAL_ERROR "Native media targets missing")\n'
        'endif()\n'
        'if(NOT kotonoha_json_target STREQUAL "json-c::json-c")\n'
        '  message(FATAL_ERROR "Unexpected json-c provider")\n'
        'endif()\n')
    configure(project, work / "media-native")
    write_ffmpeg((work / "host-msys2/include").as_posix())
    configure(project, work / "media-poison", expected=False,
              diagnostic="outside native vcpkg triplet")


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="kotonoha-msvc-providers-") as tmp:
        work = Path(tmp)
        test_sdl(work / "sdl")
        test_media(work / "media")
    print("[MSVC provider] Native SDL and media selected; "
          "host packages, stale cache and MinGW triplets rejected")


if __name__ == "__main__":
    main()
