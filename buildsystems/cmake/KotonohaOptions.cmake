# Common build options and platform-specific port selection.
include_guard(GLOBAL)

option(KOTONOHA_BUILD_WASM "Build for WebAssembly" OFF)
option(KOTONOHA_WEB_SELFTEST "Build optional WebAssembly integration self-test hooks" OFF)
option(KOTONOHA_VERBOSE_BUILD "Print full compiler and linker commands" OFF)
option(KOTONOHA_BUILD_MACOS_BUNDLES "Build macOS application bundles" OFF)
option(KOTONOHA_BUILD_ALL "Build every game port" OFF)


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE "${KOTONOHA_VERBOSE_BUILD}")

if (ANDROID OR EMSCRIPTEN)
    set(BUILD_TESTING OFF CACHE BOOL "Disable native tests on cross targets" FORCE)
endif ()
include(CTest)

if (ANDROID)
    # Align Kotonoha-linked ELF shared objects for 16 KiB page size devices.
    add_link_options("-Wl,-z,max-page-size=16384")
    set(KOTONOHA_BUILD_MACOS_BUNDLES OFF
            CACHE BOOL "Build macOS application bundles" FORCE)
endif ()

# Declare individual CMake options for each game port.
foreach (game IN LISTS KOTONOHA_GAMES)
    string(REPLACE "|" ";" fields "${game}")
    list(GET fields 0 game_option)
    option(KOTONOHA_BUILD_${game_option} "Build ${game_option} game" OFF)
endforeach ()

# KOTONOHA_BUILD_ALL enables all games (non-Android desktop targets).
if (KOTONOHA_BUILD_ALL AND NOT ANDROID)
    foreach (game IN LISTS KOTONOHA_GAMES)
        string(REPLACE "|" ";" fields "${game}")
        list(GET fields 0 game_option)
        if (EMSCRIPTEN AND "${game_option}" STREQUAL "DDLC")
            set(KOTONOHA_BUILD_${game_option} OFF CACHE BOOL "" FORCE)
        else ()
            set(KOTONOHA_BUILD_${game_option} ON)
        endif ()
    endforeach ()
endif ()

# ------------------------------------------------------------
# Android Strategy: Enable Exactly One Game
# ------------------------------------------------------------

if (ANDROID)
    set(_KOTONOHA_GAME_FOUND FALSE)
    foreach (game IN LISTS KOTONOHA_GAMES)
        string(REPLACE "|" ";" fields "${game}")
        list(GET fields 0 game_option)
        list(GET fields 1 game_id)

        if (KOTONOHA_BUILD_GAME STREQUAL game_id)
            set(KOTONOHA_BUILD_${game_option} ON CACHE BOOL "" FORCE)
            set(_KOTONOHA_GAME_FOUND TRUE)
        else ()
            set(KOTONOHA_BUILD_${game_option} OFF CACHE BOOL "" FORCE)
        endif ()
    endforeach ()

    if (NOT _KOTONOHA_GAME_FOUND)
        message(FATAL_ERROR "[Kotonoha] Unknown Android game: ${KOTONOHA_BUILD_GAME}")
    endif ()
endif ()
