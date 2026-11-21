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
include("${CMAKE_CURRENT_SOURCE_DIR}/../buildsystems/cmake/KotonohaSDL.cmake")

option(KOTONOHA_BUNDLE_LOCAL_GAME_ASSETS
        "Include locally supplied game assets in application bundles" ON)

# --- Bibliotecas Externas (Ass, FFmpeg, JSON-C) ---
# MSVC uses native vcpkg packages, never the host pkg-config executable.
if(NOT MSVC)
    find_package(PkgConfig REQUIRED)
endif()

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
elseif (MSVC)
    # MSVC must never consume pkg-config metadata or binaries built for
    # MSYS2/MinGW. These three dependencies are provided by native vcpkg.
    if(NOT VCPKG_INSTALLED_DIR OR NOT VCPKG_TARGET_TRIPLET OR
       VCPKG_TARGET_TRIPLET MATCHES "[Mm][Ii][Nn][Gg][Ww]")
        message(FATAL_ERROR
            "[Kotonoha] MSVC requires a native vcpkg triplet, for example "
            "x64-windows, and cannot use MSYS2/MinGW libraries. Set "
            "CMAKE_TOOLCHAIN_FILE before the first project() call.")
    endif()
    set(_kotonoha_native_vcpkg
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(NOT IS_DIRECTORY "${_kotonoha_native_vcpkg}/include")
        message(FATAL_ERROR
            "[Kotonoha] Missing native vcpkg headers: "
            "${_kotonoha_native_vcpkg}/include. Install dependencies "
            "for ${VCPKG_TARGET_TRIPLET}.")
    endif()

    # libass has no portable CMake config package. Search only the
    # selected native triplet for its headers and native .lib files.
    find_path(KOTONOHA_MSVC_LIBASS_INCLUDE_DIR NAMES ass/ass.h
        PATHS "${_kotonoha_native_vcpkg}/include"
        NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_MSVC_LIBASS_RELEASE NAMES ass libass
        PATHS "${_kotonoha_native_vcpkg}/lib"
        NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_MSVC_LIBASS_DEBUG NAMES ass libass
        PATHS "${_kotonoha_native_vcpkg}/debug/lib"
        NO_DEFAULT_PATH REQUIRED)
    add_library(kotonoha_ass INTERFACE)
    target_include_directories(kotonoha_ass INTERFACE
        "${KOTONOHA_MSVC_LIBASS_INCLUDE_DIR}")
    target_link_libraries(kotonoha_ass INTERFACE
        "$<IF:$<CONFIG:Debug>,${KOTONOHA_MSVC_LIBASS_DEBUG},${KOTONOHA_MSVC_LIBASS_RELEASE}>")
    set(kotonoha_ass_target kotonoha_ass)

    # Native vcpkg's FFmpeg FindFFMPEG wrapper exports these variables.
    find_package(FFMPEG REQUIRED)
    if(NOT FFMPEG_LIBRARIES OR NOT FFMPEG_INCLUDE_DIRS)
        message(FATAL_ERROR
            "[Kotonoha] Native vcpkg FFmpeg package was not resolved.")
    endif()

    # A stale cache or rogue environment path may still point to MSYS2.
    # Reject unexpected include/lib directories rather than mixing ABIs.
    file(TO_CMAKE_PATH "${_kotonoha_native_vcpkg}" _vcpkg_native_path)
    string(TOLOWER "${_vcpkg_native_path}/" _vcpkg_native_path_lower)
    foreach(_candidate IN LISTS FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARY_DIRS)
        file(TO_CMAKE_PATH "${_candidate}" _candidate_path)
        string(TOLOWER "${_candidate_path}/" _candidate_path_lower)
        string(FIND "${_candidate_path_lower}" "${_vcpkg_native_path_lower}" _position)
        if(NOT _position EQUAL 0)
            message(FATAL_ERROR
                "[Kotonoha] MSVC dependency outside native vcpkg triplet: "
                "${_candidate}. Remove stale CMake cache or configure "
                "a new MSVC build directory.")
        endif()
    endforeach()
    foreach(_candidate IN LISTS FFMPEG_LIBRARIES)
        if(IS_ABSOLUTE "${_candidate}")
            file(TO_CMAKE_PATH "${_candidate}" _candidate_path)
            string(TOLOWER "${_candidate_path}/" _candidate_path_lower)
            string(FIND "${_candidate_path_lower}" "${_vcpkg_native_path_lower}" _position)
            if(NOT _position EQUAL 0)
                message(FATAL_ERROR
                    "[Kotonoha] MSVC FFmpeg library is not from vcpkg: "
                    "${_candidate}")
            endif()
        endif()
    endforeach()
    add_library(kotonoha_ffmpeg INTERFACE)
    target_include_directories(kotonoha_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
    if(FFMPEG_LIBRARY_DIRS)
        target_link_directories(kotonoha_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
    endif()
    target_link_libraries(kotonoha_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
    set(kotonoha_ffmpeg_target kotonoha_ffmpeg)

    find_package(json-c CONFIG REQUIRED
        PATHS "${_kotonoha_native_vcpkg}" NO_DEFAULT_PATH)
    if(NOT TARGET json-c::json-c)
        message(FATAL_ERROR "[Kotonoha] Native vcpkg json-c::json-c target not found")
    endif()
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

include("${CMAKE_CURRENT_SOURCE_DIR}/../buildsystems/cmake/KotonohaImGui.cmake")

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
