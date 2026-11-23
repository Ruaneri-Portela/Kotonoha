# Kotonoha custom triplet: macOS 15.0, Intel x86_64, dynamic library linkage.
# The amd64 spelling is used only for the triplet, preset and build directory;
# the actual target architecture remains x86_64.
#
# Based on vcpkg's built-in x64-osx triplet (builtin-baseline
# 9a5dfef5d1a0438a7d24856a85bd60f309d355e9), switched to dynamic library
# linkage and pinned to the macOS 15.0 deployment target. Unique triplet
# name keeps its binary-cache entries separate from stock x64-osx.
#
# Usage:
#   cmake -S . -B out/build/macos-amd64-vcpkg -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
#     -DVCPKG_TARGET_TRIPLET=macos-amd64-vcpkg \
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
# The macos-amd64-vcpkg preset supplies the Python3_* and linker rpath flags
# above automatically. Keep them for manual configurations only: without the
# Xcode rpath, dependency fixup resolves @rpath/Python3.framework/... against
# Homebrew's arm64-only Python.framework and fails verify_app inside the
# x86_64 bundle.
#
# On an arm64 host, Homebrew's arm64-only Python/libunibreak must be excluded:
# set PKG_CONFIG_LIBDIR to this triplet's lib/pkgconfig dir. The
# macos-amd64-vcpkg preset exports that variable automatically; declaring it
# here lets vcpkg forward it to meson-based ports such as libass. UNTRACKED
# keeps the absolute, build-tree-specific path out of the vcpkg ABI hash.

set(VCPKG_ENV_PASSTHROUGH_UNTRACKED
    "${VCPKG_ENV_PASSTHROUGH_UNTRACKED};PKG_CONFIG_LIBDIR")
list(REMOVE_DUPLICATES VCPKG_ENV_PASSTHROUGH_UNTRACKED)

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)

# macOS 15.0 deployment target for both vcpkg ports and the consumer project.
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
set(VCPKG_C_FLAGS "-mmacosx-version-min=15.0 ${VCPKG_C_FLAGS}")
set(VCPKG_CXX_FLAGS "-mmacosx-version-min=15.0 ${VCPKG_CXX_FLAGS}")
set(VCPKG_LINKER_FLAGS "-mmacosx-version-min=15.0 ${VCPKG_LINKER_FLAGS}")
