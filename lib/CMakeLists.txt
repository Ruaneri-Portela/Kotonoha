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

if (APPLE AND NOT EMSCRIPTEN)
    enable_language(OBJCXX)
endif ()

# Inclusao de rotinas especificas de plataforma
include("${CMAKE_CURRENT_SOURCE_DIR}/../buildsystems/cmake/packaging/KotonohaOutputs.cmake")

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
# Root builds resolve the provider before add_subdirectory(lib). Standalone
# lib/ configurations must resolve it here, before SDL discovery runs.
include("${CMAKE_CURRENT_SOURCE_DIR}/../buildsystems/cmake/dependencies/KotonohaDepsProvider.cmake")
if(NOT DEFINED KOTONOHA_DEPS_PROVIDER_RESOLVED)
    kotonoha_resolve_deps_provider()
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../buildsystems/cmake/dependencies/KotonohaSDL.cmake")

option(KOTONOHA_BUNDLE_LOCAL_GAME_ASSETS
        "Include locally supplied game assets in application bundles" ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/../buildsystems/cmake/dependencies/KotonohaMedia.cmake")

# ==============================================================================
# Target: imgui
# ==============================================================================
option(KOTONOHA_STATIC_IMGUI
       "Link the ImGui backend statically into Kotonoha" OFF)
option(KOTONOHA_STATIC_ENGINE
       "Link Kotonoha engine statically into applications" OFF)
set(KOTONOHA_LIB_TYPE SHARED)
if (EMSCRIPTEN OR KOTONOHA_STATIC_ENGINE)
    set(KOTONOHA_LIB_TYPE STATIC)
endif ()

# Keep the upstream pinned ImGui submodule and the fork's static package option.
if(KOTONOHA_STATIC_IMGUI)
    set(_kotonoha_saved_lib_type "${KOTONOHA_LIB_TYPE}")
    set(KOTONOHA_LIB_TYPE STATIC)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../buildsystems/cmake/dependencies/KotonohaImGui.cmake")
if(KOTONOHA_STATIC_IMGUI)
    set(KOTONOHA_LIB_TYPE "${_kotonoha_saved_lib_type}")
    unset(_kotonoha_saved_lib_type)
endif()

# ==============================================================================
# Target principal: KotonohaEngine
# ==============================================================================
file(GLOB_RECURSE KOTONOHA_ENGINE_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# Optional integrations have their own targets and select a single platform
# backend. Do not also compile them into the shared engine.
list(FILTER KOTONOHA_ENGINE_SOURCES EXCLUDE REGEX "/src/integrations/")

# Preserve the Windows L4 public/dev split without reverting the upstream
# glob-based CMake and SDL/FFmpeg upgrades. No engine-specific School Days
# names are introduced here.
if(KOTONOHA_DISTRIBUTION_BUILD)
    list(FILTER KOTONOHA_ENGINE_SOURCES EXCLUDE REGEX
        "/src/(debug/DebugOverlay|mcp/MCPServer|debug/VoiceDubbingApplet|debug/DubbingOverlay)\\.cpp$")
    if(KOTONOHA_ENABLE_PLAYER_DUBBING)
        list(APPEND KOTONOHA_ENGINE_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/DubbingOverlay.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/VoiceDubbingApplet.cpp")
    endif()
else()
    list(FILTER KOTONOHA_ENGINE_SOURCES EXCLUDE REGEX
        "/src/debug/DubbingOverlay\\.cpp$")
endif()

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
if(KOTONOHA_DISTRIBUTION_BUILD AND KOTONOHA_ENABLE_PLAYER_DUBBING)
    target_compile_definitions(KotonohaEngine PUBLIC KOTONOHA_PLAYER_DUBBING=1)
endif()
if(KOTONOHA_DEV_CONTROL AND WIN32 AND NOT ANDROID)
    target_compile_definitions(KotonohaEngine PRIVATE KOTONOHA_DEV_CONTROL=1)
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

# Artefato final padronizado em todas as plataformas: libKotonohaEngine.dylib
# / libKotonohaEngine.so / KotonohaEngine.dll (o nome do target não muda).
set_target_properties(KotonohaEngine PROPERTIES OUTPUT_NAME KotonohaEngine)
if(WIN32)
    # MinGW prefixa DLLs/import libs com "lib"; o contrato de nomes exige
    # KotonohaEngine.dll + KotonohaEngine.lib em todas as toolchains Windows.
    set_target_properties(KotonohaEngine PROPERTIES
            PREFIX "" IMPORT_PREFIX "" IMPORT_SUFFIX ".lib")
endif()

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
if(APPLE)
    # The engine's Encoding.mm converts through CoreFoundation, but third-party
    # libraries may still call iconv_open/iconv/iconv_close. Link the SDK
    # libiconv explicitly for Apple CMake targets as well as Xcode targets.
    find_library(KOTONOHA_COREFOUNDATION CoreFoundation REQUIRED)
    target_link_libraries(KotonohaEngine PRIVATE "${KOTONOHA_COREFOUNDATION}")
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        # macOS já fornece iconv_open/iconv/iconv_close no libSystem. Em um
        # build vcpkg nao linkar nenhum libiconv externo: find_library
        # escolheria a copia do MacPorts/Homebrew (/opt/local), quebrando a
        # regra de que tudo vem do vcpkg.
        message(STATUS "[Kotonoha] vcpkg: usando iconv nativo do macOS (libSystem)")
    else()
        find_library(KOTONOHA_APPLE_ICONV NAMES iconv REQUIRED)
        target_link_libraries(KotonohaEngine PUBLIC "${KOTONOHA_APPLE_ICONV}")
    endif()
endif()

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

include("${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/games/KotonohaApp.cmake")

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

# Add modules to the build system. Format decoders are added before archive
# providers so providers can link the kotonoha_format_* libraries.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/formats" "${CMAKE_CURRENT_BINARY_DIR}/formats")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/providers/n2" "${CMAKE_CURRENT_BINARY_DIR}/n2archive")
# C++17 N2System/NsScript index (independent of the other game VMs).
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/vms/n2system" "${CMAKE_CURRENT_BINARY_DIR}/n2system")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/vms/meo" "${CMAKE_CURRENT_BINARY_DIR}/meo" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/vms/pmail" "${CMAKE_CURRENT_BINARY_DIR}/pmail" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../modules/providers/pmail" "${CMAKE_CURRENT_BINARY_DIR}/pmail_provider" EXCLUDE_FROM_ALL)
