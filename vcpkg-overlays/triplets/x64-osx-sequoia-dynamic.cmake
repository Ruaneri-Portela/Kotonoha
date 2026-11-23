# Kotonoha custom triplet: macOS Sequoia (15.0), Intel x86_64,
# dynamic library linkage.
#
# Based on vcpkg's built-in x64-osx triplet (builtin-baseline
# 9a5dfef5d1a0438a7d24856a85bd60f309d355e9), switched to dynamic library
# linkage and pinned to the macOS Sequoia deployment target. Unique triplet
# name keeps its binary-cache entries separate from stock x64-osx.
#
# Usage:
#   cmake -S . -B out/build/macos-sequoia-x64-dynamic -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
#     -DVCPKG_TARGET_TRIPLET=x64-osx-sequoia-dynamic \
#     -DVCPKG_OVERLAY_TRIPLETS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
#     -DVCPKG_OVERLAY_PORTS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
#     '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg' \
#     -DKOTONOHA_SDL_PROVIDER=VCPKG \
#     -DKOTONOHA_BUILD_ALL=ON -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
#     -DPython3_EXECUTABLE=/usr/bin/python3 \
#     -DPython3_ROOT_DIR=/Applications/Xcode.app/Contents/Developer/Library/Frameworks \
#     -DPython3_FIND_FRAMEWORK=FIRST \
#     '-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,/Applications/Xcode.app/Contents/Developer/Library/Frameworks'
#
# On an arm64 host, Homebrew's arm64-only Python/libunibreak must be excluded:
# pre-install with PKG_CONFIG_LIBDIR set to this triplet's lib/pkgconfig dir.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)

# Sequoia deployment target for both vcpkg ports and the consumer project.
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
set(VCPKG_C_FLAGS "-mmacosx-version-min=15.0 ${VCPKG_C_FLAGS}")
set(VCPKG_CXX_FLAGS "-mmacosx-version-min=15.0 ${VCPKG_CXX_FLAGS}")
set(VCPKG_LINKER_FLAGS "-mmacosx-version-min=15.0 ${VCPKG_LINKER_FLAGS}")
