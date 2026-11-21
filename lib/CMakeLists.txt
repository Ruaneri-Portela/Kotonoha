cmake_minimum_required(VERSION 3.16)

# ==============================================================================
# Definicao do Projeto e Linguagens
# ==============================================================================
project(KotonohaEngine VERSION 0.1.0 LANGUAGES CXX)
# Standalone lib/ CMake configurations also receive a generated version;
# when built from the root, reuse its single shared Git version header.
if(NOT KOTONOHA_BUILD_VERSION_INCLUDE_DIR)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../buildsystems/cmake/BuildVersion.cmake")
endif()
enable_language(C)

if (APPLE AND NOT EMSCRIPTEN)
    enable_language(OBJC OBJCXX)
endif ()

# Inclusao de rotinas especificas de plataforma
include("${CMAKE_CURRENT_SOURCE_DIR}/../buildsystems/cmake/KotonohaOutputs.cmake")

# Propagate the output layout to the parent directory so game, tool and test
# targets added after add_subdirectory(lib) use the same outputs tree.
# KotonohaOutputs.cmake sets CMAKE_*_OUTPUT_DIRECTORY defaults for non-Android.
if (NOT ANDROID)
    foreach (_kotonoha_output_variable IN ITEMS
            KOTONOHA_OUTPUT_DIR
            KOTONOHA_OUTPUT_CONFIG_DIR
            KOTONOHA_OUTPUT_LIB_DIR
            KOTONOHA_OUTPUT_ARCHIVE_DIR
            CMAKE_RUNTIME_OUTPUT_DIRECTORY
            CMAKE_LIBRARY_OUTPUT_DIRECTORY
            CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
        set(${_kotonoha_output_variable}
                "${${_kotonoha_output_variable}}" PARENT_SCOPE)
    endforeach ()
endif ()

# ==============================================================================
# Configuracoes Globais e Opcoes de Cache
# ==============================================================================
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
set(SDL_SYSTEM_ICONV ON CACHE BOOL "Use iconv() from system-installed libraries" FORCE)

set(SDLTTF_SAMPLES OFF CACHE BOOL "Disable SDL_ttf examples")
set(SDLTTF_VENDORED OFF CACHE BOOL "Use system FreeType/HarfBuzz/PlutoSVG")
set(SDLTTF_STRICT ON CACHE BOOL "Require enabled SDL_ttf dependencies")
set(SDLTTF_PLUTOSVG ON CACHE BOOL "Optional SVG emoji support")

option(KOTONOHA_SDL_FROM_VCPKG "Legacy alias for KOTONOHA_SDL_PROVIDER=VCPKG" OFF)
set(KOTONOHA_SDL_PROVIDER "AUTO" CACHE STRING
    "SDL3 provider: AUTO (system, vcpkg, submodules), SYSTEM, VCPKG, SUBMODULES")
set_property(CACHE KOTONOHA_SDL_PROVIDER PROPERTY STRINGS AUTO SYSTEM VCPKG SUBMODULES)
set(KOTONOHA_SDL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/submodules/SDL"
    CACHE PATH "SDL3 source checkout (mandatory on Android)")

option(KOTONOHA_BUNDLE_LOCAL_GAME_ASSETS
        "Include locally supplied game assets in application bundles" ON)

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

# --- Submodulos SDL e SDL_ttf ---
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
if(KOTONOHA_SDL_FROM_VCPKG AND _kotonoha_sdl_request STREQUAL "AUTO")
    set(_kotonoha_sdl_request VCPKG)
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
    find_package(SDL3 CONFIG QUIET)
    find_package(SDL3_ttf CONFIG QUIET)
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

# --- Bibliotecas Externas (Ass, FFmpeg, JSON-C) ---
find_package(PkgConfig REQUIRED)

if (ANDROID)
    find_path(LIBASS_INCLUDE_DIRS NAMES ass/ass.h
            PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include" NO_DEFAULT_PATH REQUIRED)
    find_package(harfbuzz CONFIG REQUIRED)
    find_package(Freetype CONFIG REQUIRED)
    find_package(Threads REQUIRED)

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(kotonoha_vcpkg_library_dir
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib")
    else ()
        set(kotonoha_vcpkg_library_dir
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
    endif ()

    find_library(KOTONOHA_LIBASS_LIBRARY NAMES ass PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_FRIBIDI_LIBRARY NAMES fribidi PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_UNIBREAK_LIBRARY NAMES unibreak PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_FONTCONFIG_LIBRARY NAMES fontconfig PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_EXPAT_LIBRARY NAMES expat PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)

    add_library(kotonoha_ass INTERFACE)
    target_include_directories(kotonoha_ass INTERFACE ${LIBASS_INCLUDE_DIRS})
    target_link_libraries(kotonoha_ass INTERFACE
            "${KOTONOHA_LIBASS_LIBRARY}"
            "${KOTONOHA_FRIBIDI_LIBRARY}"
            "${KOTONOHA_UNIBREAK_LIBRARY}"
            "${KOTONOHA_FONTCONFIG_LIBRARY}"
            "${KOTONOHA_EXPAT_LIBRARY}"
            harfbuzz::harfbuzz
            Freetype::Freetype
            Threads::Threads
            m
    )
    set(kotonoha_ass_target kotonoha_ass)

    find_package(FFMPEG REQUIRED)
    find_package(json-c CONFIG REQUIRED)

    add_library(kotonoha_ffmpeg INTERFACE)
    target_include_directories(kotonoha_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(kotonoha_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})

    set(kotonoha_ffmpeg_target kotonoha_ffmpeg)
    set(kotonoha_json_target json-c::json-c)
elseif (EMSCRIPTEN)
    pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass>=0.17)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
            libavformat libavcodec libavutil libswscale libswresample)
    pkg_check_modules(JSON_C REQUIRED IMPORTED_TARGET json-c>=0.15)
    set(kotonoha_ass_target PkgConfig::LIBASS)
    set(kotonoha_ffmpeg_target PkgConfig::FFMPEG)
    set(kotonoha_json_target PkgConfig::JSON_C)
    # vcpkg's FFmpeg .pc advertises -pthread even though its WASM archives
    # are built without atomics. Emscripten's single-thread stubs suffice.
    set_property(TARGET PkgConfig::FFMPEG PROPERTY INTERFACE_LINK_OPTIONS "")
else ()
    pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass>=0.17)
    set(kotonoha_ass_target PkgConfig::LIBASS)

    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
            libavformat libavcodec libavutil libswscale libswresample)
    set(kotonoha_ffmpeg_target PkgConfig::FFMPEG)

    pkg_check_modules(JSON_C REQUIRED IMPORTED_TARGET json-c>=0.15)
    set(kotonoha_json_target PkgConfig::JSON_C)
endif ()

# ==============================================================================
# Target: imgui
# ==============================================================================
set(KOTONOHA_LIB_TYPE SHARED)
if (EMSCRIPTEN)
    set(KOTONOHA_LIB_TYPE STATIC)
endif ()

option(KOTONOHA_IMGUI_FROM_VCPKG "Legacy alias for KOTONOHA_IMGUI_PROVIDER=VCPKG" OFF)
set(KOTONOHA_IMGUI_PROVIDER "AUTO" CACHE STRING
    "ImGui provider: AUTO (system, vcpkg, submodule), SYSTEM, VCPKG, SUBMODULES")
set_property(CACHE KOTONOHA_IMGUI_PROVIDER PROPERTY STRINGS AUTO SYSTEM VCPKG SUBMODULES)
set(KOTONOHA_IMGUI_SYSTEM_BACKENDS_DIR "" CACHE PATH
    "Optional directory containing system ImGui SDL3 backend .cpp sources")
string(TOUPPER "${KOTONOHA_IMGUI_PROVIDER}" _kotonoha_imgui_request)
if(NOT _kotonoha_imgui_request MATCHES "^(AUTO|SYSTEM|VCPKG|SUBMODULES)$")
    message(FATAL_ERROR "Invalid KOTONOHA_IMGUI_PROVIDER=${KOTONOHA_IMGUI_PROVIDER}")
endif()
if(KOTONOHA_IMGUI_FROM_VCPKG AND _kotonoha_imgui_request STREQUAL "AUTO")
    set(_kotonoha_imgui_request VCPKG)
endif()
if(ANDROID)
    if(_kotonoha_imgui_request MATCHES "^(SYSTEM|VCPKG)$")
        message(FATAL_ERROR "Android ImGui requires its bundled SDL3 backends")
    endif()
    set(_kotonoha_imgui_request SUBMODULES)
endif()
set(_kotonoha_imgui_resolved "")
set(_kotonoha_imgui_config OFF)
if(_kotonoha_imgui_request MATCHES "^(AUTO|SYSTEM)$")
    # Debian packages the core ImGui library and SDL3 backend implementations
    # separately; compile only those two backend source files.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(KOTONOHA_SYSTEM_IMGUI QUIET IMPORTED_TARGET GLOBAL imgui)
        if(KOTONOHA_SYSTEM_IMGUI_FOUND)
            find_path(_kotonoha_imgui_backend_headers NAMES imgui_impl_sdl3.h
                HINTS ${KOTONOHA_SYSTEM_IMGUI_INCLUDE_DIRS} PATH_SUFFIXES backends)
            find_file(_kotonoha_imgui_sdl3_cpp NAMES imgui_impl_sdl3.cpp
                HINTS "${KOTONOHA_IMGUI_SYSTEM_BACKENDS_DIR}"
                PATHS /usr/share/doc/libimgui-dev/examples/backends
                      /usr/local/share/doc/libimgui-dev/examples/backends)
            find_file(_kotonoha_imgui_renderer_cpp NAMES imgui_impl_sdlrenderer3.cpp
                HINTS "${KOTONOHA_IMGUI_SYSTEM_BACKENDS_DIR}"
                PATHS /usr/share/doc/libimgui-dev/examples/backends
                      /usr/local/share/doc/libimgui-dev/examples/backends)
            if(_kotonoha_imgui_backend_headers AND _kotonoha_imgui_sdl3_cpp
               AND _kotonoha_imgui_renderer_cpp)
                add_library(kotonoha_imgui_system_backends STATIC
                    "${_kotonoha_imgui_sdl3_cpp}"
                    "${_kotonoha_imgui_renderer_cpp}")
                target_include_directories(kotonoha_imgui_system_backends PUBLIC
                    ${KOTONOHA_SYSTEM_IMGUI_INCLUDE_DIRS}
                    "${_kotonoha_imgui_backend_headers}")
                target_link_libraries(kotonoha_imgui_system_backends PUBLIC
                    PkgConfig::KOTONOHA_SYSTEM_IMGUI SDL3::SDL3)
                set(KOTONOHA_IMGUI_TARGET kotonoha_imgui_system_backends)
                set(_kotonoha_imgui_resolved SYSTEM)
            endif()
        endif()
    endif()
    if(NOT _kotonoha_imgui_resolved)
        find_package(imgui CONFIG QUIET)
        if(TARGET imgui::imgui)
            set(KOTONOHA_IMGUI_TARGET imgui::imgui)
            set(_kotonoha_imgui_resolved SYSTEM)
            set(_kotonoha_imgui_config ON)
        endif()
    endif()
endif()
if(NOT _kotonoha_imgui_resolved AND
   _kotonoha_imgui_request MATCHES "^(AUTO|VCPKG)$" AND VCPKG_INSTALLED_DIR)
    find_package(imgui CONFIG QUIET)
    if(TARGET imgui::imgui)
        set(KOTONOHA_IMGUI_TARGET imgui::imgui)
        set(_kotonoha_imgui_resolved VCPKG)
        set(_kotonoha_imgui_config ON)
    endif()
endif()
if(NOT _kotonoha_imgui_resolved)
    if(_kotonoha_imgui_request STREQUAL "SYSTEM")
        message(FATAL_ERROR "System ImGui with SDL3 backends unavailable. "
            "On Debian: sudo apt install libimgui-dev. Otherwise select VCPKG/SUBMODULES.")
    elseif(_kotonoha_imgui_request STREQUAL "VCPKG")
        message(FATAL_ERROR "vcpkg ImGui unavailable: configure the vcpkg toolchain "
            "with feature imgui-vcpkg.")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/submodules/imgui/imgui.cpp" OR
       NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/submodules/imgui/backends/imgui_impl_sdl3.cpp")
        message(FATAL_ERROR "No ImGui provider available. On Debian: "
            "sudo apt install libimgui-dev; alternatively "
            "git submodule update --init --recursive or use vcpkg.")
    endif()
    add_library(imgui ${KOTONOHA_LIB_TYPE}
        submodules/imgui/imgui.cpp
        submodules/imgui/imgui_draw.cpp
        submodules/imgui/imgui_tables.cpp
        submodules/imgui/imgui_widgets.cpp
        submodules/imgui/backends/imgui_impl_sdl3.cpp
        submodules/imgui/backends/imgui_impl_sdlrenderer3.cpp
        submodules/imgui/misc/cpp/imgui_stdlib.cpp)
    target_include_directories(imgui PUBLIC
        submodules/imgui submodules/imgui/backends submodules/imgui/misc/cpp)
    target_link_libraries(imgui PUBLIC SDL3::SDL3)
    set(KOTONOHA_IMGUI_TARGET imgui)
    set(_kotonoha_imgui_resolved SUBMODULES)
endif()
set(KOTONOHA_IMGUI_PROVIDER_RESOLVED "${_kotonoha_imgui_resolved}" CACHE INTERNAL
    "Resolved ImGui provider" FORCE)
set(KOTONOHA_IMGUI_CONFIG "${_kotonoha_imgui_config}" CACHE INTERNAL "" FORCE)
message(STATUS "[Kotonoha] ImGui provider: ${_kotonoha_imgui_resolved}")
# Also visible to sibling game CMakeLists and deployment helpers.
set(KOTONOHA_IMGUI_TARGET "${KOTONOHA_IMGUI_TARGET}" CACHE INTERNAL
    "Single ImGui link target for the active toolchain" FORCE)

# ==============================================================================
# Target principal: KotonohaEngine
# ==============================================================================
file(GLOB_RECURSE KOTONOHA_ENGINE_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

if (APPLE)
    file(GLOB_RECURSE KOTONOHA_ENGINE_SOURCES_OBJC
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
    )
    list(APPEND KOTONOHA_ENGINE_SOURCES ${KOTONOHA_ENGINE_SOURCES_OBJC})
endif ()
# Do not compile backend modules that are disabled for this build.
# Upscaler.cpp provides a small SDL-only compatibility facade when OFF.
if(DEFINED KOTONOHA_ENABLE_MCP AND NOT KOTONOHA_ENABLE_MCP)
    list(FILTER KOTONOHA_ENGINE_SOURCES EXCLUDE REGEX "/mcp/")
endif()
if(DEFINED KOTONOHA_ENABLE_UPSCALER AND NOT KOTONOHA_ENABLE_UPSCALER)
    list(FILTER KOTONOHA_ENGINE_SOURCES EXCLUDE REGEX "/media/upscaling/(MPVCompatibleUpscaler|MetalFXUpscaler)\\.(cpp|mm)$")
endif()

add_library(KotonohaEngine ${KOTONOHA_LIB_TYPE} ${KOTONOHA_ENGINE_SOURCES})
if(KOTONOHA_DISTRIBUTION_BUILD)
    # PUBLIC reaches CLI consumers, game ports and their tests as well.
    target_compile_definitions(KotonohaEngine PUBLIC KOTONOHA_DISTRIBUTION_BUILD=1)
endif()
if(DEFINED KOTONOHA_ENABLE_UPSCALER)
    target_compile_definitions(KotonohaEngine PUBLIC
        KOTONOHA_ENABLE_UPSCALER=$<BOOL:${KOTONOHA_ENABLE_UPSCALER}>)
endif()
if(DEFINED KOTONOHA_ENABLE_MCP)
    target_compile_definitions(KotonohaEngine PUBLIC
        KOTONOHA_ENABLE_MCP=$<BOOL:${KOTONOHA_ENABLE_MCP}>)
endif()
if(EMSCRIPTEN)
    target_compile_definitions(KotonohaEngine PUBLIC
        KOTONOHA_WASM_FETCH_CACHE_MIB=${KOTONOHA_WASM_FETCH_CACHE_MIB})
endif()

# Propriedades do target
target_compile_features(KotonohaEngine PUBLIC cxx_std_17)

set_target_properties(KotonohaEngine PROPERTIES
        CXX_EXTENSIONS OFF
        WINDOWS_EXPORT_ALL_SYMBOLS ON
)
if(_kotonoha_imgui_resolved STREQUAL "SUBMODULES")
    set_target_properties(imgui PROPERTIES
            CXX_EXTENSIONS OFF
            WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if (ANDROID)
    set_target_properties(KotonohaEngine PROPERTIES OUTPUT_NAME kotonoha_engine)
else ()
    set_target_properties(KotonohaEngine PROPERTIES OUTPUT_NAME kotonoha)
endif ()

target_include_directories(KotonohaEngine PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${KOTONOHA_BUILD_VERSION_INCLUDE_DIR}>
)

target_link_libraries(KotonohaEngine
        PUBLIC
        SDL3::SDL3
        SDL3_ttf::SDL3_ttf
        ${kotonoha_ffmpeg_target}
        ${KOTONOHA_IMGUI_TARGET}
        PRIVATE
        ${kotonoha_json_target}
        ${kotonoha_ass_target}
)

if (EMSCRIPTEN AND VCPKG_INSTALLED_DIR)
    # SDL_ttf's pkg-config find modules emit -l flags for static dependencies.
    # Emscripten needs the vcpkg library directory on the final link command.
    target_link_directories(KotonohaEngine PUBLIC
            "$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>")
endif ()

# --- Dependencias de Plataforma / Graficas do KotonohaEngine ---
if (WIN32 AND NOT ANDROID)
    target_link_libraries(KotonohaEngine PRIVATE user32 ws2_32)
endif ()

if (EMSCRIPTEN)
    # Emscripten provides WebGL through its GLES interface, not OpenGL::GL.
    target_link_libraries(KotonohaEngine PRIVATE GLESv2)
elseif (ANDROID)
    target_link_libraries(KotonohaEngine PRIVATE GLESv2)
elseif (APPLE AND CMAKE_SYSTEM_NAME MATCHES "iOS|tvOS")
    find_library(OPENGLES_FRAMEWORK OpenGLES REQUIRED)
    target_link_libraries(KotonohaEngine PRIVATE "${OPENGLES_FRAMEWORK}")
else ()
    find_package(OpenGL REQUIRED)
    target_link_libraries(KotonohaEngine PRIVATE OpenGL::GL)
endif ()

# Configuracoes especificas do Metal (Apple)
if (APPLE AND (NOT DEFINED KOTONOHA_ENABLE_UPSCALER OR KOTONOHA_ENABLE_UPSCALER))
    set_source_files_properties(src/media/upscaling/MetalFXUpscaler.mm
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    find_library(METALFX_FRAMEWORK MetalFX)
    if (METALFX_FRAMEWORK)
        target_compile_definitions(KotonohaEngine PRIVATE KOTONOHA_HAS_METALFX)
        target_link_libraries(KotonohaEngine PRIVATE
                "${METALFX_FRAMEWORK}"
                "-framework Metal"
                "-framework Foundation"
        )
    endif ()
endif ()

# ==============================================================================
# Modulos do Projeto e Testes
# ==============================================================================
get_filename_component(kotonoha_project_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(KOTONOHA_PROJECT_ROOT "${kotonoha_project_root}"
        CACHE INTERNAL "Kotonoha source root" FORCE)

# Exclude unused shader packs from feature-OFF app bundles and WASM
# preloads. Keep all other shared engine assets (fonts/icons/UI) unchanged.
set(_kotonoha_engine_assets "${KOTONOHA_PROJECT_ROOT}/assets")
if(DEFINED KOTONOHA_ENABLE_UPSCALER AND NOT KOTONOHA_ENABLE_UPSCALER)
    set(_kotonoha_engine_assets "${CMAKE_CURRENT_BINARY_DIR}/assets-no-upscaler")
    file(REMOVE_RECURSE "${_kotonoha_engine_assets}")
    file(COPY "${KOTONOHA_PROJECT_ROOT}/assets/"
         DESTINATION "${_kotonoha_engine_assets}"
         PATTERN "Anime4K" EXCLUDE
         PATTERN "ArtCNN" EXCLUDE)
endif()
set(KOTONOHA_ENGINE_ASSETS "${_kotonoha_engine_assets}"
        CACHE INTERNAL "Kotonoha engine assets" FORCE)
unset(_kotonoha_engine_assets)

include("${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/KotonohaApp.cmake")

if(EMSCRIPTEN AND KOTONOHA_WEB_SELFTEST)
    kotonoha_add_app(KotonohaWebSelfTest
        tests/web/main.cpp tests/web/WebSelfTest.cpp)
    target_link_libraries(KotonohaWebSelfTest PRIVATE KotonohaEngine)
    target_compile_definitions(KotonohaWebSelfTest PRIVATE KOTONOHA_WEB_SELFTEST)
    kotonoha_configure_app(KotonohaWebSelfTest
        GAME_ASSET_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/web/game-assets")
endif()

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()

# Add modules to the build system
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/providers/n2" "${CMAKE_CURRENT_BINARY_DIR}/n2archive")
# C++17 N2System/NsScript index (independent of the other game VMs).
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/vms/n2system" "${CMAKE_CURRENT_BINARY_DIR}/n2system")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/vms/meo" "${CMAKE_CURRENT_BINARY_DIR}/meo" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/vms/pmail" "${CMAKE_CURRENT_BINARY_DIR}/pmail" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/providers/pmail" "${CMAKE_CURRENT_BINARY_DIR}/pmail_provider" EXCLUDE_FROM_ALL)
