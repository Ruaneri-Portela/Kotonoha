# Override vcpkg's stock wasm32-emscripten triplet, preserving static
# dependencies while compiling them as PIC for first-party SIDE_MODULEs.
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED EMSCRIPTEN_ROOT EMSDK PATH)
set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${VCPKG_ROOT_DIR}/scripts/toolchains/emscripten.cmake")
set(VCPKG_C_FLAGS "-fPIC")
set(VCPKG_CXX_FLAGS "-fPIC")
