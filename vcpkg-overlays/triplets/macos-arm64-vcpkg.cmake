# Kotonoha custom triplet: macOS 15.0, Apple Silicon arm64, dynamic library
# linkage.
#
# Based on vcpkg's built-in arm64-osx triplet (builtin-baseline
# 9a5dfef5d1a0438a7d24856a85bd60f309d355e9), switched to dynamic library
# linkage and pinned to the macOS 15.0 deployment target. Unique triplet
# name keeps its binary-cache entries separate from stock arm64-osx.
#
# Usage:
#   cmake -S . -B out/build/macos-arm64-vcpkg -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
#     -DVCPKG_TARGET_TRIPLET=macos-arm64-vcpkg \
#     -DVCPKG_OVERLAY_TRIPLETS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
#     -DVCPKG_OVERLAY_PORTS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
#     '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg' \
#     -DKOTONOHA_SDL_PROVIDER=VCPKG \
#     -DKOTONOHA_BUILD_ALL=ON -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)

set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

# macOS 15.0 deployment target for both vcpkg ports and the consumer project.
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
set(VCPKG_C_FLAGS "-mmacosx-version-min=15.0 ${VCPKG_C_FLAGS}")
set(VCPKG_CXX_FLAGS "-mmacosx-version-min=15.0 ${VCPKG_CXX_FLAGS}")
set(VCPKG_LINKER_FLAGS "-mmacosx-version-min=15.0 ${VCPKG_LINKER_FLAGS}")
