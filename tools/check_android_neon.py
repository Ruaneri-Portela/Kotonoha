#!/usr/bin/env python3
"""Check Android ARMv7 NEON and existing ARM64/x86_64 ABI contracts.

This static check does not replace compiling the NDK libraries or device QA.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def check() -> None:
    gradle = read("buildsystems/android/app/build.gradle")
    cmake = read("buildsystems/cmake/KotonohaAndroid.cmake")
    for expected in (
        ".orElse('arm64-v8a,armeabi-v7a,x86_64')",
        "'armv7', 'armv7a', 'arm-neon', 'arm-neon-android'",
        "'-DANDROID_ARM_NEON=ON'",
        "'armeabi-v7a': 'arm-neon-android'",
        "'x86_64'   : 'x64-android'",
        "'arm64-v8a': 'arm64-android'",
    ):
        assert expected in gradle, f"Missing Android Gradle contract: {expected}"
    assert "'armeabi-v7a': 'arm-android'" not in gradle
    for expected in (
        "set(ANDROID_ARM_NEON ON CACHE BOOL",
        'set(_VCPKG_ANDROID_TRIPLET "arm-neon-android")',
        'set(_VCPKG_ANDROID_TRIPLET "arm64-android")',
        'set(_VCPKG_ANDROID_TRIPLET "x64-android")',
    ):
        assert expected in cmake, f"Missing Android CMake contract: {expected}"
    assert 'set(_VCPKG_ANDROID_TRIPLET "arm-android")' not in cmake
    for doc in (
        "buildsystems/android/README.md",
        "buildsystems/android/README.pt-BR.md",
        "docs/BUILD_ANDROID.md",
        "docs/BUILD_ANDROID.pt-BR.md",
    ):
        assert "armeabi-v7a" in read(doc), f"Missing ARMv7 instructions: {doc}"
    print("[Kotonoha] Android ARMv7 NEON + ARM64/x86_64 contracts passed")


if __name__ == "__main__":
    check()
