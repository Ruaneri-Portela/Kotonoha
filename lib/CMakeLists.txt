cmake_minimum_required(VERSION 3.16)

project(KotonohaEngine VERSION 0.1.0 LANGUAGES CXX)
enable_language(C)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(SDL_SHARED ON CACHE BOOL "Build shared SDL3")
set(SDL_STATIC OFF CACHE BOOL "Build static SDL3")
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Disable SDL test library")
set(SDL_TESTS OFF CACHE BOOL "Disable SDL tests")
set(SDL_EXAMPLES OFF CACHE BOOL "Disable SDL examples")
# SDL disables system iconv on Apple/Windows; Kotonoha::ShiftJisToUtf8 needs
# its CP932 converter, so it is forced on (libc iconv on macOS/Linux).
set(SDL_SYSTEM_ICONV ON CACHE BOOL "Use iconv() from system-installed libraries" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "Disable SDL_ttf examples")
set(SDLTTF_VENDORED OFF CACHE BOOL "Use system FreeType/HarfBuzz/PlutoSVG")
set(SDLTTF_STRICT ON CACHE BOOL "Require enabled SDL_ttf dependencies")
set(SDLTTF_PLUTOSVG ON CACHE BOOL "Optional SVG emoji support")
set(KOTONOHA_SDL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/submodules/SDL"
    CACHE PATH "SDL3 sources (Android uses a pinned remote Git checkout)")
add_subdirectory("${KOTONOHA_SDL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/SDL" EXCLUDE_FROM_ALL)
add_subdirectory(submodules/SDL_ttf EXCLUDE_FROM_ALL)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass>=0.17)
set(kotonoha_ass_target PkgConfig::LIBASS)
if(ANDROID)
    find_package(FFMPEG REQUIRED)
    find_package(json-c CONFIG REQUIRED)
    add_library(kotonoha_ffmpeg INTERFACE)
    target_include_directories(kotonoha_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(kotonoha_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
    set(kotonoha_ffmpeg_target kotonoha_ffmpeg)
    set(kotonoha_json_target json-c::json-c)
    # libass is a static vcpkg library on Android; include its private
    # FreeType/HarfBuzz/FriBidi dependencies in the final shared engine.
    target_link_directories(PkgConfig::LIBASS INTERFACE ${LIBASS_STATIC_LIBRARY_DIRS})
    target_link_libraries(PkgConfig::LIBASS INTERFACE ${LIBASS_STATIC_LIBRARIES})
    target_link_options(PkgConfig::LIBASS INTERFACE ${LIBASS_STATIC_LDFLAGS_OTHER})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale libswresample)
    pkg_check_modules(JSON_C REQUIRED IMPORTED_TARGET json-c>=0.15)
    set(kotonoha_ffmpeg_target PkgConfig::FFMPEG)
    set(kotonoha_json_target PkgConfig::JSON_C)
endif()

add_library(kotonoha_imgui SHARED
    submodules/imgui/imgui.cpp
    submodules/imgui/imgui_draw.cpp
    submodules/imgui/imgui_tables.cpp
    submodules/imgui/imgui_widgets.cpp
    submodules/imgui/backends/imgui_impl_sdl3.cpp
    submodules/imgui/backends/imgui_impl_sdlrenderer3.cpp
    submodules/imgui/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(kotonoha_imgui PUBLIC
    submodules/imgui submodules/imgui/backends submodules/imgui/misc/cpp)
target_link_libraries(kotonoha_imgui PUBLIC SDL3::SDL3)

add_library(KotonohaEngine SHARED
    src/core/AssetProvider.cpp
    src/core/Clock.cpp
    src/core/CommandLine.cpp
    src/core/DiscImage.cpp
    src/core/Engine.cpp
    src/core/Launcher.cpp
    src/core/GameLauncher.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/core/OpenGL.cpp
    src/core/Settings.cpp
    src/core/Runtime.cpp
    src/core/UI.cpp
    src/componentes/ChoiceQuestionScene.cpp
    src/componentes/FadeScene.cpp
    src/componentes/GameMenu.cpp
    src/componentes/InGameMenu.cpp
    src/componentes/SettingsMenu.cpp
    src/componentes/SplashScreen.cpp
    src/componentes/SubtitleScene.cpp
    src/componentes/TextInputDialog.cpp
    src/componentes/VNBacklog.cpp
    src/componentes/VNManager.cpp
    src/componentes/VNSave.cpp
    src/componentes/VNSaveMenu.cpp
    src/componentes/VNTextBox.cpp
    src/debug/DebugOverlay.cpp
    src/debug/ImGuiTheme.cpp
    src/localization/Encoding.cpp
    src/localization/Translation.cpp
    src/media/MediaDecoder.cpp
    src/media/MediaPlayer.cpp
    src/media/AudioMixer.cpp
    src/media/PcmPlayer.cpp
    src/media/upscaling/Upscaler.cpp
    src/media/upscaling/MPVCompatibleUpscaler.cpp
    src/media/upscaling/MetalFXUpscaler.mm
    src/mcp/MCPServer.cpp
    src/script/Timeline.cpp
    src/script/TimelinePlayer.cpp
    src/script/KtrfScript.cpp
    src/parsers/Ktrf.c
    src/parsers/KtrfTyped.c
    src/parsers/Ors.c
    src/parsers/AssStyle.cpp
    src/routing/KtrfRuntime.c
    src/routing/KtrfRouter.c
    src/routing/KtrfAdapter.c
)
add_library(Kotonoha::Engine ALIAS KotonohaEngine)
add_library(kotonoha ALIAS KotonohaEngine)
target_compile_features(KotonohaEngine PUBLIC cxx_std_17)
set_target_properties(KotonohaEngine kotonoha_imgui PROPERTIES
    CXX_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
set_target_properties(KotonohaEngine PROPERTIES OUTPUT_NAME kotonoha)
target_include_directories(KotonohaEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(KotonohaEngine PUBLIC SDL3::SDL3 SDL3_ttf::SDL3_ttf
    ${kotonoha_ffmpeg_target} kotonoha_imgui PRIVATE
    ${kotonoha_json_target} ${kotonoha_ass_target})

if(ANDROID)
    target_link_libraries(KotonohaEngine PRIVATE GLESv2)
elseif(APPLE AND CMAKE_SYSTEM_NAME MATCHES "iOS|tvOS")
    find_library(OPENGLES_FRAMEWORK OpenGLES REQUIRED)
    target_link_libraries(KotonohaEngine PRIVATE "${OPENGLES_FRAMEWORK}")
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(KotonohaEngine PRIVATE OpenGL::GL)
endif()

if(APPLE)
    set_source_files_properties(src/media/upscaling/MetalFXUpscaler.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    find_library(METALFX_FRAMEWORK MetalFX)
    if(METALFX_FRAMEWORK)
        target_compile_definitions(KotonohaEngine PRIVATE KOTONOHA_HAS_METALFX)
        target_link_libraries(KotonohaEngine PRIVATE
            "${METALFX_FRAMEWORK}" "-framework Metal" "-framework Foundation")
    endif()
else()
    # Without Objective-C++ the same file builds as the unavailable C++ stub.
    set_source_files_properties(src/media/upscaling/MetalFXUpscaler.mm
        PROPERTIES LANGUAGE CXX
        COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/TP,-xc++>")
endif()

get_filename_component(kotonoha_project_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(KOTONOHA_PROJECT_ROOT "${kotonoha_project_root}"
    CACHE INTERNAL "Kotonoha source root" FORCE)
set(KOTONOHA_ENGINE_ASSETS "${KOTONOHA_PROJECT_ROOT}/assets"
    CACHE INTERNAL "Kotonoha engine assets" FORCE)
option(KOTONOHA_BUNDLE_LOCAL_GAME_ASSETS
    "Include locally supplied game assets in application bundles" ON)

# Tests and ordinary executables use the same resource layout as app bundles.
function(kotonoha_copy_assets target)
    if(ANDROID)
        return() # Gradle packages assets and game-assets in the APK.
    endif()
    cmake_parse_arguments(ASSETS "" "GAME" "" ${ARGN})
    get_target_property(is_bundle ${target} MACOSX_BUNDLE)
    if(APPLE AND is_bundle)
        set(destination "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources")
    else()
        set(destination "$<TARGET_FILE_DIR:${target}>")
    endif()
    if(ASSETS_GAME)
        set(include_game ON)
        if(APPLE AND is_bundle AND NOT ASSETS_GAME STREQUAL "example")
            set(include_game "${KOTONOHA_BUNDLE_LOCAL_GAME_ASSETS}")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DRESOURCE_SOURCE=${KOTONOHA_ENGINE_ASSETS}"
                "-DGAME_SOURCE=${KOTONOHA_PROJECT_ROOT}/games/${ASSETS_GAME}/game-assets"
                "-DRESOURCE_DESTINATION=${destination}"
                "-DINCLUDE_GAME_ASSETS=${include_game}"
                -P "${KOTONOHA_PROJECT_ROOT}/cmake/PackageAssets.cmake"
            VERBATIM)
    else()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${KOTONOHA_ENGINE_ASSETS}" "${destination}/assets"
            VERBATIM)
    endif()
endfunction()

include("${KOTONOHA_PROJECT_ROOT}/cmake/KotonohaApp.cmake")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
