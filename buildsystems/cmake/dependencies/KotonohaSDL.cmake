# SDL3/SDL3_ttf provider choice and cache options.
if (EMSCRIPTEN)
    set(SDL_SHARED OFF CACHE BOOL "Build shared SDL3" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Build static SDL3" FORCE)
else ()
    set(SDL_SHARED ON CACHE BOOL "Build shared SDL3")
    set(SDL_STATIC OFF CACHE BOOL "Build static SDL3")
endif ()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Disable SDL test library")
set(SDL_TESTS OFF CACHE BOOL "Disable SDL tests")
set(SDL_EXAMPLES OFF CACHE BOOL "Disable SDL examples")
# The Apple text codec lives in Encoding.mm (CoreFoundation); bundled SDL3
# must not require the host's libiconv for text conversion.
if(APPLE)
    set(SDL_SYSTEM_ICONV OFF CACHE BOOL "Do not link system iconv on Apple" FORCE)
else()
    set(SDL_SYSTEM_ICONV ON CACHE BOOL "Use iconv() from system-installed libraries" FORCE)
endif()

set(SDLTTF_SAMPLES OFF CACHE BOOL "Disable SDL_ttf examples")
set(SDLTTF_VENDORED OFF CACHE BOOL "Use system FreeType/HarfBuzz/PlutoSVG")
set(SDLTTF_STRICT ON CACHE BOOL "Require enabled SDL_ttf dependencies")
set(SDLTTF_PLUTOSVG ON CACHE BOOL "Optional SVG emoji support")

set(KOTONOHA_SDL_PROVIDER "AUTO" CACHE STRING
    "SDL3 provider: AUTO (system, vcpkg, submodules), SYSTEM, VCPKG, SUBMODULES")
set_property(CACHE KOTONOHA_SDL_PROVIDER PROPERTY STRINGS AUTO SYSTEM VCPKG SUBMODULES)
set(KOTONOHA_SDL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/submodules/SDL"
    CACHE PATH "SDL3 source checkout (mandatory on Android)")


# ==============================================================================
# Dependencias da Plataforma (Android vs. Outros)
# ==============================================================================
if (ANDROID AND NOT PKG_CONFIG_EXECUTABLE AND VCPKG_INSTALLED_DIR)
    if (CMAKE_HOST_WIN32)
        set(kotonoha_pkgconf_name "pkgconf.exe")
    else ()
        set(kotonoha_pkgconf_name "pkgconf")
    endif ()

    file(GLOB kotonoha_pkgconf_candidates LIST_DIRECTORIES FALSE
            "${VCPKG_INSTALLED_DIR}/*/tools/pkgconf/${kotonoha_pkgconf_name}")

    foreach (kotonoha_pkgconf_candidate IN LISTS kotonoha_pkgconf_candidates)
        file(RELATIVE_PATH kotonoha_pkgconf_relative
                "${VCPKG_INSTALLED_DIR}" "${kotonoha_pkgconf_candidate}")
        if (NOT kotonoha_pkgconf_relative MATCHES "^[^/]*-android/")
            set(PKG_CONFIG_EXECUTABLE "${kotonoha_pkgconf_candidate}" CACHE FILEPATH
                    "pkg-config compatible executable" FORCE)
            break ()
        endif ()
    endforeach ()
endif ()

# --- SDL dependency selection ---
if (EMSCRIPTEN AND VCPKG_INSTALLED_DIR)
    set(_kotonoha_wasm_pc_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig")
    set(ENV{PKG_CONFIG_LIBDIR} "${_kotonoha_wasm_pc_dir}")
    set(ENV{PKG_CONFIG_PATH} "${_kotonoha_wasm_pc_dir}")
    # These SDL_ttf find modules cache pkg-config paths. A build tree changed
    # from Debug to Release must not retain the previous configuration's libs.
    get_cmake_property(_kotonoha_cache_vars CACHE_VARIABLES)
    foreach (_kotonoha_cache_var IN LISTS _kotonoha_cache_vars)
        if (_kotonoha_cache_var MATCHES "^(harfbuzz_|plutosvg_|plutovg_|PC_HARFBUZZ_|PC_PLUTOSVG_|PC_PLUTOVG_|pkgcfg_lib_PC_HARFBUZZ_|pkgcfg_lib_PC_PLUTOSVG_|pkgcfg_lib_PC_PLUTOVG_)")
            unset(${_kotonoha_cache_var} CACHE)
        endif ()
    endforeach ()
endif ()
# Resolve SDL3 and SDL3_ttf from the host first, then vcpkg or submodules.
string(TOUPPER "${KOTONOHA_SDL_PROVIDER}" _kotonoha_sdl_request)
if(NOT _kotonoha_sdl_request MATCHES "^(AUTO|SYSTEM|VCPKG|SUBMODULES)$")
    message(FATAL_ERROR "Invalid KOTONOHA_SDL_PROVIDER=${KOTONOHA_SDL_PROVIDER}")
endif()
# MSVC uses Microsoft's C runtime and ABI. Never import SDL headers, libs
# or pkg-config metadata from MSYS2/MinGW, even when AUTO is requested.
if(MSVC)
    if(_kotonoha_sdl_request STREQUAL "SYSTEM")
        message(FATAL_ERROR
            "[Kotonoha] KOTONOHA_SDL_PROVIDER=SYSTEM is not supported with MSVC. "
            "Use AUTO (vcpkg -> submodules), VCPKG or SUBMODULES.")
    endif()
    if(VCPKG_TARGET_TRIPLET MATCHES "[Mm][Ii][Nn][Gg][Ww]")
        message(FATAL_ERROR
            "[Kotonoha] MSVC cannot use a MinGW vcpkg triplet: "
            "${VCPKG_TARGET_TRIPLET}. Select x64-windows or another MSVC triplet.")
    endif()
endif()
if(ANDROID)
    if(_kotonoha_sdl_request STREQUAL "SYSTEM" OR
       _kotonoha_sdl_request STREQUAL "VCPKG")
        message(FATAL_ERROR "Android SDL3 requires submodules for its Java integration")
    endif()
    set(_kotonoha_sdl_request SUBMODULES)
endif()
set(_kotonoha_sdl_resolved "")
set(_kotonoha_sdl_config_sdl3 OFF)
set(_kotonoha_sdl_config_ttf OFF)
if(NOT MSVC AND _kotonoha_sdl_request MATCHES "^(AUTO|SYSTEM)$")
    find_package(SDL3 CONFIG QUIET)
    find_package(SDL3_ttf CONFIG QUIET)
    if(TARGET SDL3::SDL3)
        set(_kotonoha_sdl_config_sdl3 ON)
    endif()
    if(TARGET SDL3_ttf::SDL3_ttf)
        set(_kotonoha_sdl_config_ttf ON)
    endif()
    if(_kotonoha_sdl_config_sdl3 AND _kotonoha_sdl_config_ttf)
        set(_kotonoha_sdl_resolved SYSTEM)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(KOTONOHA_SYSTEM_SDL3 QUIET IMPORTED_TARGET GLOBAL sdl3)
            pkg_check_modules(KOTONOHA_SYSTEM_SDL3_TTF QUIET IMPORTED_TARGET GLOBAL SDL3_ttf)
            if(KOTONOHA_SYSTEM_SDL3_FOUND AND KOTONOHA_SYSTEM_SDL3_TTF_FOUND)
                if(NOT TARGET SDL3::SDL3)
                    add_library(SDL3::SDL3 INTERFACE IMPORTED GLOBAL)
                    target_link_libraries(SDL3::SDL3 INTERFACE PkgConfig::KOTONOHA_SYSTEM_SDL3)
                endif()
                if(NOT TARGET SDL3_ttf::SDL3_ttf)
                    add_library(SDL3_ttf::SDL3_ttf INTERFACE IMPORTED GLOBAL)
                    target_link_libraries(SDL3_ttf::SDL3_ttf INTERFACE PkgConfig::KOTONOHA_SYSTEM_SDL3_TTF)
                endif()
                set(_kotonoha_sdl_resolved SYSTEM)
            endif()
        endif()
    endif()
endif()
if(NOT _kotonoha_sdl_resolved AND
   _kotonoha_sdl_request MATCHES "^(AUTO|VCPKG)$" AND VCPKG_INSTALLED_DIR)
    if(MSVC)
        # Only accept packages from the vcpkg target triplet. Unqualified
        # find_package may silently discover C:/msys64/ucrt64.
        set(_kotonoha_vcpkg_prefix
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        if(NOT VCPKG_TARGET_TRIPLET OR
           NOT EXISTS "${_kotonoha_vcpkg_prefix}")
            message(FATAL_ERROR
                "[Kotonoha] MSVC requires an installed native vcpkg triplet "
                "(for example x64-windows). Set CMAKE_TOOLCHAIN_FILE to "
                "vcpkg.cmake before the first project() call.")
        endif()
        # Cached *-DIR entries can preempt NO_DEFAULT_PATH. Drop them
        # when switching an old MinGW/system build tree over to MSVC.
        unset(SDL3_DIR CACHE)
        unset(SDL3_ttf_DIR CACHE)
        find_package(SDL3 CONFIG QUIET
            PATHS "${_kotonoha_vcpkg_prefix}" NO_DEFAULT_PATH)
        find_package(SDL3_ttf CONFIG QUIET
            PATHS "${_kotonoha_vcpkg_prefix}" NO_DEFAULT_PATH)
    else()
        find_package(SDL3 CONFIG QUIET)
        find_package(SDL3_ttf CONFIG QUIET)
    endif()
    if(TARGET SDL3::SDL3 AND TARGET SDL3_ttf::SDL3_ttf)
        set(_kotonoha_sdl_resolved VCPKG)
        set(_kotonoha_sdl_config_sdl3 ON)
        set(_kotonoha_sdl_config_ttf ON)
    endif()
endif()
if(NOT _kotonoha_sdl_resolved)
    if(_kotonoha_sdl_request STREQUAL "SYSTEM")
        message(FATAL_ERROR "System SDL3/SDL3_ttf unavailable. On Debian: "
            "sudo apt install libsdl3-dev libsdl3-ttf-dev. "
            "Or set KOTONOHA_SDL_PROVIDER=VCPKG/SUBMODULES.")
    elseif(_kotonoha_sdl_request STREQUAL "VCPKG")
        message(FATAL_ERROR "vcpkg SDL3/SDL3_ttf unavailable. Configure the "
            "vcpkg toolchain with feature sdl-vcpkg.")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/KotonohaGitSubmodules.cmake")
    if(KOTONOHA_SDL_SOURCE_DIR STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/submodules/SDL")
      kotonoha_require_submodule("lib/submodules/SDL" "CMakeLists.txt")
    endif()
    kotonoha_require_submodule("lib/submodules/SDL_ttf" "CMakeLists.txt")
    if(MSVC AND
       (TARGET SDL3::SDL3 OR TARGET SDL3_ttf::SDL3_ttf))
        message(FATAL_ERROR
            "[Kotonoha] Only part of SDL3 was resolved from the vcpkg triplet. "
            "Install both SDL3 and SDL3_ttf using sdl-vcpkg, or select "
            "SUBMODULES in a clean build directory.")
    endif()
    if(NOT EXISTS "${KOTONOHA_SDL_SOURCE_DIR}/CMakeLists.txt" OR
       NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/submodules/SDL_ttf/CMakeLists.txt")
        message(FATAL_ERROR "No SDL3/SDL3_ttf provider available. On Debian: "
            "sudo apt install libsdl3-dev libsdl3-ttf-dev; alternatively "
            "git submodule update --init --recursive or use vcpkg.")
    endif()
    if(TARGET SDL3::SDL3 OR TARGET SDL3_ttf::SDL3_ttf)
        message(FATAL_ERROR "Partial installed SDL3 targets. Install both "
            "SDL3 development packages, or explicitly select SUBMODULES.")
    endif()
    add_subdirectory("${KOTONOHA_SDL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/SDL" EXCLUDE_FROM_ALL)
    add_subdirectory(submodules/SDL_ttf EXCLUDE_FROM_ALL)
    if(ANDROID)
        get_target_property(_ttf_target SDL3_ttf::SDL3_ttf ALIASED_TARGET)
        set_property(TARGET ${_ttf_target} PROPERTY LINKER_LANGUAGE CXX)
    endif()
    foreach(_kotonoha_sdl_shared IN ITEMS SDL3-shared SDL3_ttf-shared)
        if(TARGET ${_kotonoha_sdl_shared})
            set_property(TARGET ${_kotonoha_sdl_shared} PROPERTY VERSION)
            set_property(TARGET ${_kotonoha_sdl_shared} PROPERTY SOVERSION)
        endif()
    endforeach()
    set(_kotonoha_sdl_resolved SUBMODULES)
endif()
set(KOTONOHA_SDL_PROVIDER_RESOLVED "${_kotonoha_sdl_resolved}" CACHE INTERNAL
    "Resolved SDL provider" FORCE)
set(KOTONOHA_SDL_CONFIG_SDL3 "${_kotonoha_sdl_config_sdl3}" CACHE INTERNAL "" FORCE)
set(KOTONOHA_SDL_CONFIG_TTF "${_kotonoha_sdl_config_ttf}" CACHE INTERNAL "" FORCE)
message(STATUS "[Kotonoha] SDL3/SDL3_ttf provider: ${_kotonoha_sdl_resolved}")
