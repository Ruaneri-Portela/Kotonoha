# Centralised runtime output layout for final application targets.
#
# Desktop application targets are placed under <build>/outputs:
#   * Windows: executables and runtime DLLs next to each other in per-target folders.
#   * Linux/macOS without bundles: executables in per-target folders and
#     shared libraries in an adjacent "libs" directory.
#   * macOS bundles: each application is a self-contained ".app" folder.
#
# Non-application targets (tests, tools, core libraries) keep their default
# build-tree locations (e.g. bin/lib), unless explicitly configured otherwise.
# Android keeps the Gradle/NDK layout because its native libraries are packaged
# into APKs rather than run from the build tree.

include_guard(GLOBAL)

if(NOT DEFINED KOTONOHA_OUTPUT_DIR)
    set(KOTONOHA_OUTPUT_DIR "${CMAKE_BINARY_DIR}/outputs"
            CACHE PATH "Directory receiving Kotonoha runtime artifacts")
endif()

get_filename_component(KOTONOHA_OUTPUT_DIR "${KOTONOHA_OUTPUT_DIR}" ABSOLUTE
        BASE_DIR "${CMAKE_BINARY_DIR}")
file(TO_CMAKE_PATH "${KOTONOHA_OUTPUT_DIR}" KOTONOHA_OUTPUT_DIR)

get_property(_kotonoha_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_kotonoha_multi_config)
    set(KOTONOHA_OUTPUT_CONFIG_DIR "${KOTONOHA_OUTPUT_DIR}/$<CONFIG>")
else()
    set(KOTONOHA_OUTPUT_CONFIG_DIR "${KOTONOHA_OUTPUT_DIR}")
endif()
set(KOTONOHA_OUTPUT_LIB_DIR "${KOTONOHA_OUTPUT_CONFIG_DIR}/libs")
set(KOTONOHA_OUTPUT_ARCHIVE_DIR "${KOTONOHA_OUTPUT_CONFIG_DIR}/lib")

if(NOT ANDROID)
    # Defaults for non-application targets (tests, tools, core libraries).
    # Final application targets override the runtime directory below so each
    # game/app owns its outputs under the outputs tree.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

# Select the output directory for an application target. macOS bundles are
# emitted directly inside outputs; every other desktop application gets its own
# folder containing the executable, assets, game-assets and runtime libraries.
function(kotonoha_configure_app_output target)
    if(ANDROID)
        return()
    endif()

    get_target_property(_kotonoha_is_bundle ${target} MACOSX_BUNDLE)
    if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin"
            AND KOTONOHA_BUILD_MACOS_BUNDLES AND _kotonoha_is_bundle)
        set_target_properties(${target} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${KOTONOHA_OUTPUT_CONFIG_DIR}")
        return()
    endif()

    set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${KOTONOHA_OUTPUT_CONFIG_DIR}/${target}")

    if(APPLE)
        set_target_properties(${target} PROPERTIES
                BUILD_RPATH "@loader_path/libs"
                INSTALL_RPATH "@loader_path/libs"
                BUILD_WITH_INSTALL_RPATH TRUE)
    elseif(UNIX)
        set_target_properties(${target} PROPERTIES
                BUILD_RPATH "\$ORIGIN/libs;\$ORIGIN"
                INSTALL_RPATH "\$ORIGIN/libs;\$ORIGIN"
                BUILD_WITH_INSTALL_RPATH TRUE)
    endif()
endfunction()
