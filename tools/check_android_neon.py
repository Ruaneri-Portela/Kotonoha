#!/usr/bin/env python3
"""Static validation of Android ARMv7 NEON Gradle, CMake and APK contracts.

The separate Android workflow performs the real NDK build and APK inspection.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def check() -> None:
    gradle = read("buildsystems/android/app/build.gradle")
    cmake = read("buildsystems/cmake/KotonohaAndroid.cmake")
    ci = read(".github/workflows/android.yml")

    require(".orElse('arm64-v8a,armeabi-v7a,x86_64')" in gradle,
            "Default Gradle ABI list must include ARMv7")
    require("'armeabi-v7a': 'arm-android'" not in gradle,
            "ARMv7 must never use the non-NEON vcpkg triplet")
    require(gradle.count("'armeabi-v7a': 'arm-neon-android'") == 2,
            "Both Python stdlib and extension staging must use NEON triplet")
    require("'-DANDROID_ARM_NEON=ON'" in gradle,
            "Gradle must enable NEON before the NDK toolchain runs")
    require("set(ANDROID_ARM_NEON ON CACHE BOOL" in cmake,
            "CMake must enforce ARMv7 NEON before project()")
    require('set(_VCPKG_ANDROID_TRIPLET "arm-neon-android")' in cmake,
            "CMake must choose the ARMv7 NEON vcpkg triplet")
    require('set(_VCPKG_ANDROID_TRIPLET "arm64-android")' in cmake,
            "ARM64 support must remain available")

    # Fork safety: retain both Android ARM64 and x86_64 ABI mappings even
    # while extending upstream ARMv7 NEON and CI contracts.
    require("'arm64-v8a': 'arm64-android'" in gradle,
            "ARM64 triplet mapping must remain available")
    require("'x86_64'   : 'x64-android'" in gradle,
            "x86_64 triplet mapping must remain available")
    require('set(_VCPKG_ANDROID_TRIPLET "x64-android")' in cmake,
            "CMake x86_64 triplet mapping must remain available")

    require("for triplet in arm64-android arm-neon-android; do" in ci,
            "CI must precompile both ARM ABIs")
    require("-Pabis=arm64-v8a,armeabi-v7a" in ci,
            "CI must build both ARM ABIs")
    require('for abi in ("arm64-v8a", "armeabi-v7a"):' in ci,
            "CI must verify JNI libraries for both ARM ABIs")
    require("armv7_caches" in ci and "ANDROID_ARM_NEON:" in ci
            and "VCPKG_TARGET_TRIPLET:" in ci,
            "CI must verify actual ARMv7 NEON CMake configuration")
    require("kotonoha-android-arm64-armv7-neon.zip" in ci,
            "CI must package both ARM ABIs")

    for path in ("docs/BUILD_ANDROID.md", "docs/BUILD_ANDROID.pt-BR.md",
                 "buildsystems/android/README.md",
                 "buildsystems/android/README.pt-BR.md"):
        require("armeabi-v7a" in read(path), f"Missing ARMv7 docs: {path}")

    print("[Kotonoha] Android ARMv7 NEON contracts passed")


if __name__ == "__main__":
    check()
