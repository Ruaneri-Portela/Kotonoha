# Single switch for native dependency discovery (SDL3, SDL3_ttf, FFmpeg,
# libass, json-c, ...). Supported providers:
#   AUTO       - vcpkg when its toolchain is active, otherwise system
#                packages; SDL3 additionally falls back to submodules.
#   VCPKG      - vcpkg only (toolchain required).
#   SYSTEM     - Homebrew / MSYS2 UCRT / apt (pkg-config + CMake configs).
#   SUBMODULES - pinned SDL3/SDL3_ttf submodules; native media deps from
#                the system package manager.
# Per-library overrides resolve independently:
#   SDL3/SDL3_ttf: VCPKG, SUBMODULES or SYSTEM (KOTONOHA_SDL_PROVIDER).
#   Dear ImGui:    SUBMODULES or VCPKG only (KOTONOHA_IMGUI_PROVIDER).
#                  SYSTEM is intentionally rejected: no system imgui package
#                  is used, since the engine needs pinned SDL3 backends.
# This module is included before project() to declare the cache options and
# after project() to resolve them once VCPKG_INSTALLED_DIR is available.
include_guard(GLOBAL)

set(KOTONOHA_DEPS_PROVIDER "AUTO" CACHE STRING
    "Native dependency provider: AUTO, VCPKG, SYSTEM or SUBMODULES")
set_property(CACHE KOTONOHA_DEPS_PROVIDER PROPERTY STRINGS
    AUTO VCPKG SYSTEM SUBMODULES)
# Per-library override used by presets and CI. AUTO means "follow
# KOTONOHA_DEPS_PROVIDER"; an explicit value pins SDL3/SDL3_ttf only.
set(KOTONOHA_SDL_PROVIDER "AUTO" CACHE STRING
    "SDL3/SDL3_ttf provider: AUTO (follow deps provider), VCPKG, SYSTEM, SUBMODULES")
set_property(CACHE KOTONOHA_SDL_PROVIDER PROPERTY STRINGS
    AUTO VCPKG SYSTEM SUBMODULES)
# Dear ImGui is never taken from system packages. AUTO follows the master
# provider (submodule by default, vcpkg when its imgui package is installed).
set(KOTONOHA_IMGUI_PROVIDER "AUTO" CACHE STRING
    "Dear ImGui provider: AUTO (follow deps provider), SUBMODULES, VCPKG (no SYSTEM)")
set_property(CACHE KOTONOHA_IMGUI_PROVIDER PROPERTY STRINGS
    AUTO SUBMODULES VCPKG)

function(kotonoha_resolve_deps_provider)
    string(TOUPPER "${KOTONOHA_DEPS_PROVIDER}" _master)
    if(NOT _master MATCHES "^(AUTO|VCPKG|SYSTEM|SUBMODULES)$")
        message(FATAL_ERROR
            "[Kotonoha] Invalid KOTONOHA_DEPS_PROVIDER=${KOTONOHA_DEPS_PROVIDER}. "
            "Expected AUTO, VCPKG, SYSTEM or SUBMODULES.")
    endif()
    string(TOUPPER "${KOTONOHA_SDL_PROVIDER}" _sdl_requested)
    if(NOT _sdl_requested MATCHES "^(AUTO|SYSTEM|VCPKG|SUBMODULES)$")
        message(FATAL_ERROR
            "[Kotonoha] Invalid KOTONOHA_SDL_PROVIDER=${KOTONOHA_SDL_PROVIDER}. "
            "Expected AUTO, VCPKG, SYSTEM or SUBMODULES.")
    endif()
    string(TOUPPER "${KOTONOHA_IMGUI_PROVIDER}" _imgui_requested)
    if(_imgui_requested STREQUAL "SYSTEM")
        message(FATAL_ERROR
            "[Kotonoha] KOTONOHA_IMGUI_PROVIDER=SYSTEM is not supported. "
            "Dear ImGui is NEVER resolved from system packages: the engine "
            "requires the pinned SDL3/SDL3 renderer backends. Use "
            "KOTONOHA_IMGUI_PROVIDER=SUBMODULES (pinned submodule, default "
            "for AUTO/SYSTEM builds) or KOTONOHA_IMGUI_PROVIDER=VCPKG with "
            "the imgui-vcpkg manifest feature.")
    endif()
    if(NOT _imgui_requested MATCHES "^(AUTO|SUBMODULES|VCPKG)$")
        message(FATAL_ERROR
            "[Kotonoha] Invalid KOTONOHA_IMGUI_PROVIDER="
            "${KOTONOHA_IMGUI_PROVIDER}. Expected AUTO, SUBMODULES or VCPKG; "
            "SYSTEM is intentionally rejected.")
    endif()

    set(_vcpkg_active OFF)
    if(CMAKE_TOOLCHAIN_FILE MATCHES "[\\/]vcpkg\\.cmake$")
        set(_vcpkg_active ON)
    endif()
    if(VCPKG_INSTALLED_DIR OR VCPKG_MANIFEST_MODE)
        set(_vcpkg_active ON)
    endif()

    if(_master STREQUAL "VCPKG" AND NOT _vcpkg_active)
        message(FATAL_ERROR
            "[Kotonoha] KOTONOHA_DEPS_PROVIDER=VCPKG requires the vcpkg "
            "toolchain. Pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/"
            "buildsystems/vcpkg.cmake on the FIRST configuration, or use "
            "SYSTEM / SUBMODULES for system packages and pinned submodules.")
    endif()

    if(_master STREQUAL "AUTO")
        if(_vcpkg_active)
            set(_resolved VCPKG)
        else()
            set(_resolved SYSTEM)
        endif()
    else()
        set(_resolved "${_master}")
    endif()
    # Android always uses vcpkg media packages plus SDL's Java integration
    # from the pinned submodule; the native provider switch does not apply.
    if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        set(_resolved VCPKG)
    endif()

    # Cross-compiling Windows with mingw-w64 from macOS/Linux has no system
    # package source: apt/brew packages describe the host, not the Windows
    # target. vcpkg (with a *-mingw-dynamic triplet) is mandatory there.
    if(MINGW AND CMAKE_CROSSCOMPILING AND
       NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND
       _resolved STREQUAL "SYSTEM")
        message(FATAL_ERROR
            "[Kotonoha] MinGW cross-compilation requires VCPKG: system "
            "packages (apt/brew) cannot provide Windows target libraries. "
            "Use a windows-*-mingw-cross-vcpkg-* preset, or set "
            "KOTONOHA_DEPS_PROVIDER=VCPKG with a *-mingw-dynamic triplet.")
    endif()

    if(_sdl_requested STREQUAL "AUTO")
        set(_sdl_effective "${_resolved}")
    else()
        set(_sdl_effective "${_sdl_requested}")
    endif()
    if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        set(_sdl_effective SUBMODULES)
    endif()
    # Dear ImGui: an explicit VCPKG master means every library comes from
    # vcpkg. AUTO with an active vcpkg toolchain prefers the imgui package
    # when the imgui-vcpkg manifest feature installed it, and otherwise
    # keeps the pinned submodule as the safe default.
    if(_imgui_requested STREQUAL "AUTO")
        set(_imgui_effective SUBMODULES)
        if(_master STREQUAL "VCPKG")
            set(_imgui_effective VCPKG)
        elseif(_resolved STREQUAL "VCPKG" AND VCPKG_INSTALLED_DIR AND
               VCPKG_TARGET_TRIPLET AND
               EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/imgui/imgui-config.cmake")
            set(_imgui_effective VCPKG)
        elseif(_resolved STREQUAL "VCPKG")
            message(STATUS
                "[Kotonoha] vcpkg imgui package not installed; ImGui AUTO "
                "uses the pinned submodule. Add \"imgui-vcpkg\" to "
                "VCPKG_MANIFEST_FEATURES to select vcpkg explicitly.")
        endif()
    else()
        set(_imgui_effective "${_imgui_requested}")
    endif()
    # Android needs the SDL Java bindings from the pinned submodule and does
    # not ship an imgui vcpkg package per ABI. Keep the user-facing cache
    # option in sync (also asserted by the Android CI cache checks).
    if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        set(_imgui_effective SUBMODULES)
        set(KOTONOHA_IMGUI_PROVIDER "SUBMODULES" CACHE STRING
            "Dear ImGui provider (Android: pinned submodule only)" FORCE)
    endif()
    if(_resolved STREQUAL "VCPKG")
        set(_media VCPKG)
    else()
        set(_media SYSTEM)
    endif()

    set(KOTONOHA_DEPS_PROVIDER_RESOLVED "${_resolved}" CACHE INTERNAL
        "Resolved native dependency provider" FORCE)
    set(KOTONOHA_MEDIA_PROVIDER_RESOLVED "${_media}" CACHE INTERNAL
        "Resolved FFmpeg/libass/json-c provider" FORCE)
    set(KOTONOHA_SDL_PROVIDER_EFFECTIVE "${_sdl_effective}" CACHE INTERNAL
        "Resolved SDL3/SDL3_ttf provider (master + per-library override)" FORCE)
    set(KOTONOHA_IMGUI_PROVIDER_EFFECTIVE "${_imgui_effective}" CACHE INTERNAL
        "Resolved Dear ImGui provider (never SYSTEM)" FORCE)

    if(MSVC AND _media STREQUAL "SYSTEM")
        message(WARNING
            "[Kotonoha] MSVC + SYSTEM dependencies: make sure the pkg-config "
            "metadata and libraries come from a single ABI-compatible "
            "installation. MSYS2/UCRT64 (*.dll.a) and MSVC (*.lib) binaries "
            "cannot be mixed; for a native MSVC toolchain prefer "
            "KOTONOHA_DEPS_PROVIDER=VCPKG with an x64-windows triplet.")
    endif()

    # System-package prefixes. Only applied when a library actually uses
    # SYSTEM (SDL3 override on a vcpkg master, or the master itself): a
    # vcpkg build must never pick up Homebrew, MSYS2 or /usr by accident.
    # ImGui never uses SYSTEM, so it never contributes system prefixes.
    # CMAKE_PREFIX_PATH is written with PARENT_SCOPE: a plain set() inside a
    # CMake function would stay local and never reach find_package() in lib/.
    set(_kotonoha_added_prefixes "")
    if(NOT _resolved STREQUAL "VCPKG" OR _sdl_effective STREQUAL "SYSTEM")
        set(_kotonoha_system_prefixes "")
        if(APPLE)
            list(APPEND _kotonoha_system_prefixes "/opt/homebrew" "/usr/local")
        elseif(WIN32)
            # MSYS2 shells export MSYSTEM_PREFIX=/ucrt64. Convert it to a
            # native path when CMake itself is a Windows (non-MSYS2) build.
            set(_msys_prefix_raw "")
            if(DEFINED ENV{MSYSTEM_PREFIX})
                set(_msys_prefix_raw "$ENV{MSYSTEM_PREFIX}")
            elseif(DEFINED ENV{MINGW_PREFIX})
                set(_msys_prefix_raw "$ENV{MINGW_PREFIX}")
            endif()
            if(_msys_prefix_raw MATCHES "^/")
                find_program(KOTONOHA_CYGPATH_EXECUTABLE NAMES cygpath)
                if(KOTONOHA_CYGPATH_EXECUTABLE)
                    execute_process(COMMAND "${KOTONOHA_CYGPATH_EXECUTABLE}" -m
                        "${_msys_prefix_raw}"
                        OUTPUT_VARIABLE _msys_prefix_native
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
                    if(IS_DIRECTORY "${_msys_prefix_native}")
                        set(_msys_prefix_raw "${_msys_prefix_native}")
                    endif()
                endif()
            endif()
            if(_msys_prefix_raw)
                file(TO_CMAKE_PATH "${_msys_prefix_raw}" _msys_prefix)
                if(IS_DIRECTORY "${_msys_prefix}" AND
                   NOT "${_msys_prefix}" IN_LIST _kotonoha_system_prefixes)
                    list(APPEND _kotonoha_system_prefixes "${_msys_prefix}")
                endif()
            endif()
        elseif(UNIX)
            list(APPEND _kotonoha_system_prefixes "/usr/local")
        endif()
        foreach(_prefix IN LISTS _kotonoha_system_prefixes)
            if(IS_DIRECTORY "${_prefix}")
                if(NOT "${_prefix}" IN_LIST CMAKE_PREFIX_PATH AND
                   NOT "${_prefix}" IN_LIST _kotonoha_added_prefixes)
                    list(APPEND _kotonoha_added_prefixes "${_prefix}")
                endif()
                set(ENV{PKG_CONFIG_PATH}
                    "${_prefix}/lib/pkgconfig:${_prefix}/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")
            endif()
        endforeach()
    endif()
    if(_kotonoha_added_prefixes)
        set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${_kotonoha_added_prefixes}
            PARENT_SCOPE)
    endif()

    message(STATUS "[Kotonoha] Native deps provider: ${_resolved} "
        "(SDL3/SDL3_ttf: ${_sdl_effective}; ImGui: ${_imgui_effective}; "
        "media: ${_media})")
endfunction()
