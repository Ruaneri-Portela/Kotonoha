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

# The master provider (KOTONOHA_DEPS_PROVIDER), the per-library SDL override
# (KOTONOHA_SDL_PROVIDER: VCPKG/SYSTEM/SUBMODULES) and the ImGui override are
# declared/resolved by KotonohaDepsProvider.cmake. Dear ImGui is handled in
# KotonohaImGui.cmake and is never looked up in system packages here.
include("${CMAKE_CURRENT_LIST_DIR}/KotonohaDepsProvider.cmake")
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
        if (NOT kotonoha_pkgconf_candidate MATCHES "[/\\]android[/\\]")
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
# Resolve SDL3/SDL3_ttf: master provider first, legacy explicit override
# second. Only an explicit SUBMODULES request (or Android) may initialize the
# pinned submodules; SYSTEM/VCPKG fail instead of falling back to them.
if(DEFINED KOTONOHA_SDL_PROVIDER_EFFECTIVE AND
   NOT KOTONOHA_SDL_PROVIDER_EFFECTIVE STREQUAL "")
    set(_kotonoha_sdl_request "${KOTONOHA_SDL_PROVIDER_EFFECTIVE}")
else()
    set(_kotonoha_sdl_request "${KOTONOHA_SDL_PROVIDER}")
endif()
string(TOUPPER "${_kotonoha_sdl_request}" _kotonoha_sdl_request)
if(NOT _kotonoha_sdl_request MATCHES "^(AUTO|SYSTEM|VCPKG|SUBMODULES)$")
    message(FATAL_ERROR "Invalid SDL provider: ${_kotonoha_sdl_request}")
endif()
if(ANDROID)
    set(_kotonoha_sdl_request SUBMODULES)
endif()
set(_kotonoha_sdl_resolved "")
set(_kotonoha_sdl_config_sdl3 OFF)
set(_kotonoha_sdl_config_ttf OFF)
set(_kotonoha_sdl_pkg_sdl3 OFF)
set(_kotonoha_sdl_pkg_ttf OFF)
if(_kotonoha_sdl_request MATCHES "^(AUTO|SYSTEM)$")
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
            if(KOTONOHA_SYSTEM_SDL3_FOUND)
                set(_kotonoha_sdl_pkg_sdl3 ON)
            endif()
            if(KOTONOHA_SYSTEM_SDL3_TTF_FOUND)
                set(_kotonoha_sdl_pkg_ttf ON)
            endif()
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
    # Search only the selected vcpkg triplet so a system/MSYS2 installation
    # can never be mixed into a vcpkg build (on any compiler).
    set(_kotonoha_vcpkg_prefix
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(NOT VCPKG_TARGET_TRIPLET OR NOT EXISTS "${_kotonoha_vcpkg_prefix}")
        message(FATAL_ERROR
            "[Kotonoha] vcpkg SDL3 requires an installed target triplet "
            "(${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}). Use the vcpkg "
            "toolchain with feature sdl-vcpkg, or select SYSTEM/SUBMODULES.")
    endif()
    # Cached *-DIR entries can preempt NO_DEFAULT_PATH after switching
    # provider in an existing build tree.
    unset(SDL3_DIR CACHE)
    unset(SDL3_ttf_DIR CACHE)
    find_package(SDL3 CONFIG QUIET
        PATHS "${_kotonoha_vcpkg_prefix}" NO_DEFAULT_PATH)
    find_package(SDL3_ttf CONFIG QUIET
        PATHS "${_kotonoha_vcpkg_prefix}" NO_DEFAULT_PATH)
    if(TARGET SDL3::SDL3 AND TARGET SDL3_ttf::SDL3_ttf)
        set(_kotonoha_sdl_resolved VCPKG)
        set(_kotonoha_sdl_config_sdl3 ON)
        set(_kotonoha_sdl_config_ttf ON)
    endif()
endif()
# A partial resolution (only one of SDL3 / SDL3_ttf was found) must never
# fall back to the pinned submodules: one library already comes from the
# selected provider, so fetching a second SDL copy would mix two SDL
# installations in the same build tree. Fail early and leave the tree clean.
set(_kotonoha_sdl_has_sdl3 OFF)
set(_kotonoha_sdl_has_ttf OFF)
if(TARGET SDL3::SDL3 OR _kotonoha_sdl_pkg_sdl3)
    set(_kotonoha_sdl_has_sdl3 ON)
endif()
if(TARGET SDL3_ttf::SDL3_ttf OR _kotonoha_sdl_pkg_ttf)
    set(_kotonoha_sdl_has_ttf ON)
endif()
if(NOT _kotonoha_sdl_resolved AND
   (_kotonoha_sdl_has_sdl3 OR _kotonoha_sdl_has_ttf))
    if(_kotonoha_sdl_has_sdl3 AND NOT _kotonoha_sdl_has_ttf)
        set(_kotonoha_sdl_missing "SDL3_ttf")
    else()
        set(_kotonoha_sdl_missing "SDL3")
    endif()
    message(FATAL_ERROR
        "[Kotonoha] SDL3/SDL3_ttf was only partially resolved from "
        "${_kotonoha_sdl_request}: ${_kotonoha_sdl_missing} is missing. "
        "No submodule will be fetched, because mixing providers would "
        "link two SDL installations. Install both from the same provider "
        "(pacman -S mingw-w64-ucrt-x86_64-sdl3 "
        "mingw-w64-ucrt-x86_64-sdl3-ttf on MSYS2 UCRT64, "
        "brew install sdl3 sdl3_ttf on macOS, "
        "apt install libsdl3-dev libsdl3-ttf-dev on Debian/Ubuntu), or "
        "reconfigure a clean build directory with "
        "-DKOTONOHA_SDL_PROVIDER=SUBMODULES.")
endif()
if(NOT _kotonoha_sdl_resolved)
    if(NOT _kotonoha_sdl_request STREQUAL "SUBMODULES")
        message(FATAL_ERROR
            "[Kotonoha] SDL3/SDL3_ttf are unavailable from "
            "${_kotonoha_sdl_request}. Install both from the same provider "
            "(brew install sdl3 sdl3_ttf on macOS, "
            "pacman -S mingw-w64-ucrt-x86_64-sdl3 "
            "mingw-w64-ucrt-x86_64-sdl3-ttf on MSYS2 UCRT64, "
            "apt install libsdl3-dev libsdl3-ttf-dev on Debian/Ubuntu), or "
            "reconfigure a clean build directory with "
            "-DKOTONOHA_SDL_PROVIDER=SUBMODULES, the only mode that "
            "initializes the pinned SDL submodules.")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/KotonohaGitSubmodules.cmake")
    if(KOTONOHA_SDL_SOURCE_DIR STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/submodules/SDL")
      kotonoha_require_submodule("lib/submodules/SDL" "CMakeLists.txt")
    endif()
    kotonoha_require_submodule("lib/submodules/SDL_ttf" "CMakeLists.txt")
    if(NOT EXISTS "${KOTONOHA_SDL_SOURCE_DIR}/CMakeLists.txt" OR
       NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/submodules/SDL_ttf/CMakeLists.txt")
        message(FATAL_ERROR "No SDL3/SDL3_ttf provider available. Use the "
            "system packages listed above, git submodule update --init "
            "--recursive, or the vcpkg toolchain with feature sdl-vcpkg.")
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

