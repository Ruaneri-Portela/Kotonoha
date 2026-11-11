cmake_minimum_required(VERSION 3.16)

project(KotonohaEngine VERSION 0.1.0 LANGUAGES CXX)
enable_language(C)
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/MSVC.cmake")

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(SDL_SHARED ON CACHE BOOL "Build shared SDL3")
set(SDL_STATIC OFF CACHE BOOL "Build static SDL3")
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Disable SDL test library")
set(SDL_TESTS OFF CACHE BOOL "Disable SDL tests")
set(SDL_EXAMPLES OFF CACHE BOOL "Disable SDL examples")
# Kotonoha::ShiftJisToUtf8 needs CP932 (libc iconv on macOS/Linux).
# Windows uses its native CP932 mapping and SDL's converter for other encodings.
set(SDL_SYSTEM_ICONV ON CACHE BOOL "Use iconv() from system-installed libraries" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "Disable SDL_ttf examples")
set(SDLTTF_VENDORED OFF CACHE BOOL "Use system FreeType/HarfBuzz/PlutoSVG")
set(SDLTTF_STRICT ON CACHE BOOL "Require enabled SDL_ttf dependencies")
set(SDLTTF_PLUTOSVG ON CACHE BOOL "Optional SVG emoji support")
set(KOTONOHA_SDL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/submodules/SDL"
    CACHE PATH "SDL3 sources (Android uses a pinned remote Git checkout)")

# SDL_ttf discovers HarfBuzz through pkg-config while its subdirectory is
# configured.  In Android manifest mode vcpkg installs pkgconf for the build
# host under <installed>/<host-triplet>/tools/pkgconf, which is not guaranteed
# to be on CMAKE_PROGRAM_PATH.  Find it before adding SDL_ttf so HarfBuzz's
# required C++ runtime is propagated as -lc++ instead of being omitted.
if(ANDROID AND NOT PKG_CONFIG_EXECUTABLE AND VCPKG_INSTALLED_DIR)
    if(CMAKE_HOST_WIN32)
        set(kotonoha_pkgconf_name "pkgconf.exe")
    else()
        set(kotonoha_pkgconf_name "pkgconf")
    endif()
    file(GLOB kotonoha_pkgconf_candidates LIST_DIRECTORIES FALSE
        "${VCPKG_INSTALLED_DIR}/*/tools/pkgconf/${kotonoha_pkgconf_name}")
    foreach(kotonoha_pkgconf_candidate IN LISTS kotonoha_pkgconf_candidates)
        if(NOT kotonoha_pkgconf_candidate MATCHES "[/\\]android[/\\]")
            set(PKG_CONFIG_EXECUTABLE "${kotonoha_pkgconf_candidate}" CACHE FILEPATH
                "pkg-config compatible executable" FORCE)
            break()
        endif()
    endforeach()
endif()

add_subdirectory("${KOTONOHA_SDL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/SDL" EXCLUDE_FROM_ALL)
if(ANDROID)
    # Use vcpkg's CMake target rather than SDL_ttf's pkg-config fallback,
    # which can resolve -lc++ to the NDK's Windows host library.
    find_package(harfbuzz CONFIG REQUIRED)
endif()
add_subdirectory(submodules/SDL_ttf EXCLUDE_FROM_ALL)
if(ANDROID)
    # SDL_ttf is C, but its static HarfBuzz dependency contains C++ code.
    # Let the NDK C++ driver select the runtime for the active ABI/STL.
    get_target_property(_ttf_target SDL3_ttf::SDL3_ttf ALIASED_TARGET)
    set_property(TARGET ${_ttf_target} PROPERTY LINKER_LANGUAGE CXX)
endif()
find_package(PkgConfig REQUIRED)
if(ANDROID)
    # FindPkgConfig resolves HarfBuzz's `-lc++` to the NDK host archive
    # (prebuilt/windows-x86_64/lib/libc++.a) when an IMPORTED_TARGET is
    # created while cross-compiling.  That archive is not an Android ELF
    # library and makes the final libkotonoha.so link fail.  Use pkg-config
    # only for libass metadata on Android, then bind the static Android
    # libraries and their CMake targets explicitly.  The Android clang++
    # driver remains responsible for selecting c++_shared.
    find_path(LIBASS_INCLUDE_DIRS NAMES ass/ass.h
        PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include" NO_DEFAULT_PATH REQUIRED)
    find_package(harfbuzz CONFIG REQUIRED)
    find_package(Freetype CONFIG REQUIRED)
    find_package(Threads REQUIRED)

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(kotonoha_vcpkg_library_dir
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib")
    else()
        set(kotonoha_vcpkg_library_dir
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
    endif()
    find_library(KOTONOHA_LIBASS_LIBRARY NAMES ass
        PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_FRIBIDI_LIBRARY NAMES fribidi
        PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_UNIBREAK_LIBRARY NAMES unibreak
        PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_FONTCONFIG_LIBRARY NAMES fontconfig
        PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_EXPAT_LIBRARY NAMES expat
        PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
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
        m)
    set(kotonoha_ass_target kotonoha_ass)

    find_package(FFMPEG REQUIRED)
    find_package(json-c CONFIG REQUIRED)
    add_library(kotonoha_ffmpeg INTERFACE)
    target_include_directories(kotonoha_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(kotonoha_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
    set(kotonoha_ffmpeg_target kotonoha_ffmpeg)
    set(kotonoha_json_target json-c::json-c)
    # Android uses the kotonoha_ass INTERFACE target above, which already
    # links the static libass and all explicit private dependencies. Do not
    # touch PkgConfig::LIBASS here: the metadata-only pkg_check_modules call
    # deliberately does not create an IMPORTED_TARGET in this branch.
else()
    pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass>=0.17)
    set(kotonoha_ass_target PkgConfig::LIBASS)
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
    src/core/AssetOverlay.cpp
    src/core/Clock.cpp
    src/core/CommandLine.cpp
    src/core/DiscImage.cpp
    src/core/Engine.cpp
    src/core/Launcher.cpp
    src/core/GameLauncher.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/core/OpenGL.cpp
    src/core/PresentedFrame.cpp
    src/core/VisualTransition.cpp
    src/core/Settings.cpp
    src/core/Runtime.cpp
    src/core/UI.cpp
    src/components/ChoiceQuestionScene.cpp
    src/components/DisplayModeCatalog.cpp
    src/components/FadeScene.cpp
    src/components/GameMenu.cpp
    src/components/InGameMenu.cpp
    src/components/SettingsMenu.cpp
    src/components/SplashScreen.cpp
    src/components/SubtitleScene.cpp
    src/components/ForcedSubtitleRenderer.cpp
    src/components/TextInputForm.cpp
    src/components/TextInputDialog.cpp
    src/components/VNBacklog.cpp
    src/components/VNManager.cpp
    src/components/VNSave.cpp
    src/components/VNSaveMenu.cpp
    src/components/VNTextBox.cpp
    src/events/BackAction.cpp
    src/events/BacklogEvents.cpp
    src/events/InputActions.cpp
    src/debug/DebugOverlay.cpp
    src/debug/VoiceDubbingApplet.cpp
    src/debug/ImGuiTheme.cpp
    src/localization/Encoding.cpp
    src/localization/Translation.cpp
    src/media/MediaDecoder.cpp
    src/media/MediaPlayer.cpp
    src/media/AudioMixer.cpp
    src/media/PcmPlayer.cpp
    src/media/TextureAlpha.cpp
    src/media/Ps2Tim2.cpp
    src/media/CriAudio.cpp
    src/media/Ps2SoundBank.cpp
    src/media/upscaling/Upscaler.cpp
    src/media/upscaling/MPVCompatibleUpscaler.cpp
    src/media/upscaling/MetalFXUpscaler.mm
    src/mobile/AndroidDisplay.cpp
    src/mcp/MCPServer.cpp
    src/script/Timeline.cpp
    src/script/TimelinePlayer.cpp
    src/script/KtrfScript.cpp
    src/parsers/Ktrf.cpp
    src/parsers/KtrfTyped.cpp
    src/parsers/Ors.cpp
    src/parsers/AssStyle.cpp
    src/routing/KtrfRuntime.cpp
    src/routing/KtrfRouter.cpp
    src/routing/KtrfAdapter.cpp
)
add_library(Kotonoha::Engine ALIAS KotonohaEngine)
add_library(kotonoha ALIAS KotonohaEngine)
target_compile_features(KotonohaEngine PUBLIC cxx_std_17)
set_target_properties(KotonohaEngine kotonoha_imgui PROPERTIES
    CXX_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(ANDROID)
    # Kotonoha (the example app) and kotonoha collide on Windows/macOS
    # case-insensitive filesystems when both are Android shared libraries.
    set_target_properties(KotonohaEngine PROPERTIES OUTPUT_NAME kotonoha_engine)
else()
    set_target_properties(KotonohaEngine PROPERTIES OUTPUT_NAME kotonoha)
endif()
if(KOTONOHA_DEV_CONTROL AND WIN32 AND NOT ANDROID)
    target_compile_definitions(KotonohaEngine PRIVATE KOTONOHA_DEV_CONTROL=1)
endif()
target_include_directories(KotonohaEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(KotonohaEngine PUBLIC SDL3::SDL3 SDL3_ttf::SDL3_ttf
    ${kotonoha_ffmpeg_target} kotonoha_imgui PRIVATE
    ${kotonoha_json_target} ${kotonoha_ass_target})
if(WIN32 AND NOT ANDROID)
    target_link_libraries(KotonohaEngine PRIVATE user32)
endif()

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
            COMMAND "${CMAKE_COMMAND}"
                "-DASSET_SOURCE=${KOTONOHA_ENGINE_ASSETS}"
                "-DASSET_DESTINATION=${destination}/assets"
                -P "${KOTONOHA_PROJECT_ROOT}/cmake/CopyAssetDirectory.cmake"
            VERBATIM)
    endif()
endfunction()

include("${KOTONOHA_PROJECT_ROOT}/cmake/KotonohaApp.cmake")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
