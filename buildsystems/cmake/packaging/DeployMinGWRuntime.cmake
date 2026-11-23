# Deploy the MinGW GCC runtime and the vcpkg runtime DLLs next to a Windows
# executable.
#
# file(GET_RUNTIME_DEPENDENCIES) cannot reliably resolve the GCC runtime stack
# on a cross-compiling host: libwinpthread-1.dll, libgcc_s_*.dll and
# libstdc++-6.dll live in the compiler sysroot, outside the vcpkg search paths.
# Transitive imports (e.g. libplutosvg.dll pulled in by SDL3_ttf) can also be
# missed when the import table is inspected from a non-Windows host. Copy the
# complete deployment set explicitly instead of relying on import discovery.

if(NOT DEFINED TARGET_DIR OR NOT IS_DIRECTORY "${TARGET_DIR}")
    message(FATAL_ERROR
        "[Kotonoha] Missing or invalid runtime deployment directory: "
        "${TARGET_DIR}")
endif()

if(NOT DEFINED MINGW_RUNTIME_DLLS)
    set(MINGW_RUNTIME_DLLS "")
endif()
if(NOT DEFINED VCPKG_BIN_DIR)
    set(VCPKG_BIN_DIR "")
endif()

set(runtime_dlls ${MINGW_RUNTIME_DLLS})

if(VCPKG_BIN_DIR AND IS_DIRECTORY "${VCPKG_BIN_DIR}")
    file(GLOB vcpkg_dlls LIST_DIRECTORIES FALSE "${VCPKG_BIN_DIR}/*.dll")
    list(APPEND runtime_dlls ${vcpkg_dlls})
endif()

foreach(dll IN LISTS runtime_dlls)
    if(NOT EXISTS "${dll}")
        message(FATAL_ERROR "[Kotonoha] Runtime DLL is missing: ${dll}")
    endif()
    file(COPY "${dll}" DESTINATION "${TARGET_DIR}")
endforeach()

list(LENGTH runtime_dlls deployed_count)
message(STATUS "[Kotonoha] Deployed ${deployed_count} runtime DLLs to ${TARGET_DIR}")
