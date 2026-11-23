# Common build options and platform-specific port selection.
include_guard(GLOBAL)

option(KOTONOHA_WEB_SELFTEST "Build optional WebAssembly integration self-test hooks" OFF)
option(KOTONOHA_VERBOSE_BUILD "Print full compiler and linker commands" OFF)
option(KOTONOHA_BUILD_MACOS_BUNDLES "Build macOS application bundles" OFF)
option(KOTONOHA_BUILD_ALL "Build every game port" OFF)
option(KOTONOHA_BUILD_LAUNCHER "Build the unified multi-engine launcher" OFF)
# Single feature switches propagated to engine, game ports and public headers.
set(_kotonoha_upscaler_default ON)
set(_kotonoha_mcp_default ON)
if(EMSCRIPTEN)
    set(_kotonoha_upscaler_default OFF)
    set(_kotonoha_mcp_default OFF)
elseif(ANDROID OR (APPLE AND CMAKE_SYSTEM_NAME MATCHES "iOS|tvOS|watchOS"))
    set(_kotonoha_mcp_default OFF)
endif()
option(KOTONOHA_ENABLE_UPSCALER "Compile image upscaler infrastructure" ${_kotonoha_upscaler_default})
option(KOTONOHA_ENABLE_MCP "Compile MCP server and command-line interface" ${_kotonoha_mcp_default})
# Platform policy overrides stale build caches and explicit ON requests.
if(EMSCRIPTEN)
    set(KOTONOHA_ENABLE_UPSCALER OFF CACHE BOOL "Compile image upscaler infrastructure" FORCE)
    set(KOTONOHA_ENABLE_MCP OFF CACHE BOOL "Compile MCP server and command-line interface" FORCE)
elseif(ANDROID OR (APPLE AND CMAKE_SYSTEM_NAME MATCHES "iOS|tvOS|watchOS"))
    set(KOTONOHA_ENABLE_MCP OFF CACHE BOOL "Compile MCP server and command-line interface" FORCE)
endif()
unset(_kotonoha_upscaler_default)
unset(_kotonoha_mcp_default)
# WASM starts at 128 MiB and grows only as needed. The HTTP VFS keeps a
# bounded page cache, not entire retail .bin/.gpk images in browser memory.
set(KOTONOHA_WASM_INITIAL_MEMORY 134217728 CACHE STRING
    "Initial WASM linear memory in bytes (multiple of 64 KiB)")
set(KOTONOHA_WASM_MAX_MEMORY 1073741824 CACHE STRING
    "Maximum WASM linear memory in bytes")
set(KOTONOHA_WASM_FETCH_CACHE_MIB 48 CACHE STRING
    "Per-provider HTTP Range LRU cache budget (MiB)")
if(EMSCRIPTEN)
    math(EXPR _wasm_memory_pages "${KOTONOHA_WASM_INITIAL_MEMORY} % 65536")
    if(NOT _wasm_memory_pages EQUAL 0 OR
       KOTONOHA_WASM_INITIAL_MEMORY LESS 67108864 OR
       KOTONOHA_WASM_MAX_MEMORY LESS KOTONOHA_WASM_INITIAL_MEMORY OR
       KOTONOHA_WASM_FETCH_CACHE_MIB LESS 4)
        message(FATAL_ERROR
            "Invalid WASM memory settings: align initial memory to 64KiB, "
            "start at >=64MiB, max >= initial, Fetch cache >=4MiB")
    endif()
endif()



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
        if (EMSCRIPTEN AND ("${game_option}" STREQUAL "DDLC" OR
                           "${game_option}" STREQUAL "KATAWA"))
            set(KOTONOHA_BUILD_${game_option} OFF CACHE BOOL "" FORCE)
        else ()
            set(KOTONOHA_BUILD_${game_option} ON)
        endif ()
    endforeach ()
endif ()

# ------------------------------------------------------------
# Android Strategy: Enable One Game or Unified Launcher ('all')
# ------------------------------------------------------------

if (ANDROID)
    set(_KOTONOHA_GAME_FOUND FALSE)
    if (KOTONOHA_BUILD_GAME STREQUAL "all")
        set(KOTONOHA_BUILD_ALL ON CACHE BOOL "" FORCE)
        set(KOTONOHA_BUILD_LAUNCHER ON CACHE BOOL "" FORCE)
        foreach (game IN LISTS KOTONOHA_GAMES)
            string(REPLACE "|" ";" fields "${game}")
            list(GET fields 0 game_option)
            if (game_option STREQUAL "EXAMPLE")
                set(KOTONOHA_BUILD_${game_option} OFF CACHE BOOL "" FORCE)
            else ()
                set(KOTONOHA_BUILD_${game_option} ON CACHE BOOL "" FORCE)
            endif ()
        endforeach ()
        set(_KOTONOHA_GAME_FOUND TRUE)
    else ()
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
    endif ()

    if (NOT _KOTONOHA_GAME_FOUND)
        message(FATAL_ERROR "[Kotonoha] Unknown Android game: ${KOTONOHA_BUILD_GAME}")
    endif ()
endif ()
