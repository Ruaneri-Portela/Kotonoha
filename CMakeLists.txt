cmake_minimum_required(VERSION 3.18)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
        "Minimum macOS version (Sequoia)" FORCE)
endif()

# ==============================================================================
# Android / vcpkg target triplet
#
# O Android Gradle Plugin/NDK define ANDROID_ABI. O triplet é definido antes
# de resolver dependências vcpkg para corresponder à ABI que está sendo gerada.
# ==============================================================================

if (ANDROID_ABI STREQUAL "arm64-v8a")
    set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "" FORCE)
elseif (ANDROID_ABI STREQUAL "x86_64")
    set(VCPKG_TARGET_TRIPLET "x64-android" CACHE STRING "" FORCE)
elseif (ANDROID_ABI STREQUAL "armeabi-v7a")
    set(VCPKG_TARGET_TRIPLET "arm-android" CACHE STRING "" FORCE)
elseif (ANDROID_ABI STREQUAL "x86")
    set(VCPKG_TARGET_TRIPLET "x86-android" CACHE STRING "" FORCE)
endif ()

# ==============================================================================
# Project
# ==============================================================================
project(Kotonoha VERSION 0.1.0 LANGUAGES C CXX)

option(KOTONOHA_DEV_CHECKPOINTS
    "Enable non-release School Days routing checkpoints and state dumps"
    OFF
)

option(KOTONOHA_ROUTER_TESTS
    "Build standalone School Days routing regression tests"
    OFF
)

option(KOTONOHA_ORS_PARSER_TESTS
    "Build standalone School Days ORS parser fidelity tests"
    OFF
)

option(KOTONOHA_ANIME4K_RUNTIME_TESTS
    "Build the standalone Anime4K OpenGL runtime regression test"
    OFF
)

# ==============================================================================
# Languages and language standards
# ==============================================================================
if (APPLE)
    # Necessário para arquivos Objective-C / Objective-C++ usados em macOS.
    enable_language(OBJC OBJCXX)
endif ()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # MSVC requires this switch for the C11 <stdatomic.h> implementation.
    # It is accepted in C++ translation units as well, so the directory-level
    # option also covers the mixed C/C++ test targets.
    add_compile_options(/experimental:c11atomics)
endif()

# MSVC exposes <stdatomic.h> for C only when its C11 atomics mode is enabled.
# The upstream thread-safe Kotonoha_Config intentionally uses C atomics.


# ==============================================================================
# Paths
# ==============================================================================
set(KOTONOHA_INCLUDE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

set(KOTONOHA_ASSETS_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

set(SCHOOL_DAYS_INCLUDE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/games/school-days/include"
)


# ==============================================================================
# Source roots
# ==============================================================================
set(KOTONOHA_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(KOTONOHA_RUNTIME_SOURCE_DIR "${KOTONOHA_ENGINE_SOURCE_DIR}/runtime")
set(SCHOOL_DAYS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/games/school-days/src")


# ==============================================================================
# Common dependencies
# ==============================================================================
find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec
        libavformat
        libavutil
        libswscale
        libswresample
)

pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET
        libass
)

# Prefer the repository submodules so IDE/CMake builds use the same SDL/ImGui
# revisions as upstream. System/vcpkg packages remain a supported fallback.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3/CMakeLists.txt")
    message(STATUS "Using SDL3 submodule")
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/sdl3 EXCLUDE_FROM_ALL)
else()
    find_package(SDL3 CONFIG QUIET)
    if(NOT TARGET SDL3::SDL3 AND NOT TARGET SDL3::SDL3-shared)
        pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3_ttf/CMakeLists.txt")
    message(STATUS "Using SDL3_ttf submodule")
    set(SDL3TTF_VENDORED ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/sdl3_ttf EXCLUDE_FROM_ALL)
else()
    find_package(SDL3_ttf CONFIG QUIET)
    if(NOT TARGET SDL3_ttf::SDL3_ttf AND NOT TARGET SDL3_ttf::SDL3_ttf-shared)
        pkg_check_modules(SDL3_TTF REQUIRED IMPORTED_TARGET sdl3-ttf)
    endif()
endif()

if(TARGET SDL3::SDL3-shared)
    set(KOTONOHA_SDL3_TARGET SDL3::SDL3-shared)
elseif(TARGET SDL3::SDL3)
    set(KOTONOHA_SDL3_TARGET SDL3::SDL3)
else()
    set(KOTONOHA_SDL3_TARGET ${KOTONOHA_SDL3_TARGET})
endif()

if(TARGET SDL3_ttf::SDL3_ttf-shared)
    set(KOTONOHA_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf-shared)
elseif(TARGET SDL3_ttf::SDL3_ttf)
    set(KOTONOHA_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
else()
    set(KOTONOHA_SDL3_TTF_TARGET ${KOTONOHA_SDL3_TTF_TARGET})
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp")
    message(STATUS "Using ImGui submodule")
    add_library(imgui STATIC
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/misc/cpp/imgui_stdlib.cpp
        third_party/imgui/backends/imgui_impl_sdl3.cpp
        third_party/imgui/backends/imgui_impl_sdlrenderer3.cpp
        third_party/imgui/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/misc/cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends"
    )
    target_link_libraries(imgui PUBLIC ${KOTONOHA_SDL3_TARGET})
    add_library(imgui::imgui ALIAS imgui)
else()
    find_package(imgui CONFIG REQUIRED)
endif()


# ==============================================================================
# Engine, runtime library, and SDL entry executable targets
# ==============================================================================
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KotonohaTargets.cmake")



if(KOTONOHA_DEV_CHECKPOINTS)
    target_compile_definitions(Kotonoha PRIVATE KOTONOHA_DEV_CHECKPOINTS=1)
    message(STATUS "KOTONOHA_DEV_CHECKPOINTS=ON (development-only routing helpers)")
endif()

if((KOTONOHA_ROUTER_TESTS OR KOTONOHA_ORS_PARSER_TESTS) AND NOT ANDROID)
    include(CTest)
    include_directories("${SCHOOL_DAYS_INCLUDE_DIR}")
    set(KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        CACHE PATH
        "Optional extracted School Days HQ retail fixture root used by corpus-dependent tests")

    add_executable(KotonohaEngineLinkSmokeTest
        tests/KotonohaEngineLinkSmokeTest.cpp
    )
    target_link_libraries(KotonohaEngineLinkSmokeTest PRIVATE Kotonoha::Engine)
    add_test(NAME KotonohaEngineLinkSmokeTest COMMAND KotonohaEngineLinkSmokeTest)

    add_executable(UpscalingTextureLifecycleTest
        tests/UpscalingTextureLifecycleTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
    )
    target_include_directories(UpscalingTextureLifecycleTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR})
    target_link_libraries(UpscalingTextureLifecycleTest PRIVATE
        ${KOTONOHA_SDL3_TARGET})
    set_target_properties(UpscalingTextureLifecycleTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME UpscalingTextureLifecycleTest
        COMMAND UpscalingTextureLifecycleTest)

    add_executable(OrsParserFidelityTest
        tests/OrsParserFidelityTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/Ors.c
    )
    target_include_directories(OrsParserFidelityTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )
    target_link_libraries(OrsParserFidelityTest PRIVATE ${KOTONOHA_SDL3_TARGET})
    set_target_properties(OrsParserFidelityTest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME OrsParserFidelityTest COMMAND OrsParserFidelityTest)

    add_executable(SchoolDaysSceneTimelineTest
        tests/SchoolDaysSceneTimelineTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/Ors.c
    )
    target_include_directories(SchoolDaysSceneTimelineTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )
    target_link_libraries(SchoolDaysSceneTimelineTest PRIVATE ${KOTONOHA_SDL3_TARGET})
    set_target_properties(SchoolDaysSceneTimelineTest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysSceneTimelineTest COMMAND SchoolDaysSceneTimelineTest)

    add_executable(SchoolDaysAudioEventTest
        tests/SchoolDaysAudioEventTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/Ors.c
    )
    target_include_directories(SchoolDaysAudioEventTest PRIVATE ${KOTONOHA_INCLUDE_DIR})
    target_link_libraries(SchoolDaysAudioEventTest PRIVATE ${KOTONOHA_SDL3_TARGET})
    set_target_properties(SchoolDaysAudioEventTest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysAudioEventTest COMMAND SchoolDaysAudioEventTest)

    add_executable(SchoolDaysBgmEventTest
        tests/SchoolDaysBgmEventTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/components/Audio.cpp
        src/components/Sound.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/AudioRender.c
        src/utils/Demuxer.c
        src/utils/FFmpeg.c
        src/utils/Time.c
    )
    target_include_directories(SchoolDaysBgmEventTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysBgmEventTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysBgmEventTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysBgmEventTest COMMAND SchoolDaysBgmEventTest)

    add_executable(FFmpegInputSourceTest
        tests/FFmpegInputSourceTest.cpp
        src/utils/Demuxer.c
        src/utils/FFmpeg.c
        src/utils/VoicePcm.c
    )
    target_include_directories(FFmpegInputSourceTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(FFmpegInputSourceTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(FFmpegInputSourceTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME FFmpegInputSourceTest COMMAND FFmpegInputSourceTest)

    add_executable(SchoolDaysGpkDirectRuntimeTest
        tests/SchoolDaysGpkDirectRuntimeTest.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysAssetMount.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysGpkArchive.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysGpkIndex.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/AudioRender.c
        src/renders/ImageRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/VideoRender.c
        src/utils/Demuxer.c
        src/utils/FFmpeg.c
        src/utils/Time.c
        src/utils/VoicePcm.c
    )
    target_include_directories(SchoolDaysGpkDirectRuntimeTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysGpkDirectRuntimeTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG ZLIB::ZLIB)
    set_target_properties(SchoolDaysGpkDirectRuntimeTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    add_executable(SchoolDaysAbcRuntimeTest
        tests/SchoolDaysAbcRuntimeTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/audio/VoicePcm.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/graphics/TextureAlpha.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
        ${KOTONOHA_RUNTIME_SOURCE_DIR}/components/Image.cpp
        src/renders/ImageRender.c
        src/utils/FFmpeg.c
        src/utils/Time.c
    )
    target_include_directories(SchoolDaysAbcRuntimeTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysAbcRuntimeTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysAbcRuntimeTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysAbcRuntimeTest COMMAND
        SchoolDaysAbcRuntimeTest ${CMAKE_CURRENT_SOURCE_DIR}/assets)

    add_executable(SchoolDaysAbcOverlayHotfixTest
        tests/SchoolDaysAbcOverlayHotfixTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/graphics/TextureAlpha.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
        ${KOTONOHA_RUNTIME_SOURCE_DIR}/components/Image.cpp
        src/renders/ImageRender.c
        src/utils/FFmpeg.c
        src/utils/Time.c
    )
    target_include_directories(SchoolDaysAbcOverlayHotfixTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysAbcOverlayHotfixTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysAbcOverlayHotfixTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    if(EXISTS "${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT}/Event00/00-00/00-00-A01/00-00-A01-004.PNG"
       AND EXISTS "${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT}/Event00/00-00/00-00-A02/00-00-A02-001B.PNG")
        add_test(NAME SchoolDaysAbcOverlayHotfixTest COMMAND
            SchoolDaysAbcOverlayHotfixTest ${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT})
    endif()

    add_executable(SchoolDaysAbcEdgeSeamTest
        tests/SchoolDaysAbcEdgeSeamTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/graphics/TextureAlpha.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
        ${KOTONOHA_RUNTIME_SOURCE_DIR}/components/Image.cpp
        src/renders/ImageRender.c
        src/utils/FFmpeg.c
        src/utils/Time.c
    )
    target_include_directories(SchoolDaysAbcEdgeSeamTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysAbcEdgeSeamTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysAbcEdgeSeamTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    if(EXISTS "${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT}/Event00/00-00/00-00-A01/00-00-A01-004.PNG"
       AND EXISTS "${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT}/Event00/00-00/00-00-A02/00-00-A02-001B.PNG"
       AND EXISTS "${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT}/Event04/04-SB/04-SB-E00/04-SB-E00-020.PNG")
        add_test(NAME SchoolDaysAbcEdgeSeamTest COMMAND
            SchoolDaysAbcEdgeSeamTest ${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT}
            ${CMAKE_BINARY_DIR})
    endif()

    add_executable(SchoolDaysMovieEventTest
        tests/SchoolDaysMovieEventTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/components/Video.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/VideoRender.c
        src/utils/Demuxer.c
        src/utils/FFmpeg.c
        src/utils/Time.c
    )
    target_include_directories(SchoolDaysMovieEventTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysMovieEventTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysMovieEventTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysMovieEventTest COMMAND
        SchoolDaysMovieEventTest ${CMAKE_CURRENT_SOURCE_DIR}/assets)

    add_executable(SchoolDaysTitleMovieRuntimeTest
        tests/SchoolDaysTitleMovieRuntimeTest.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysCmap.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysFontData.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysSaveFeelModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysSaveFeelRenderPlan.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysSaveLoadModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysSaveLoadRenderPlan.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysConfigMenuModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysConfigMenuRenderPlan.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysSomConSetModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysExitViewModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysExitViewRenderPlan.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysReplayModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysReplayRenderPlan.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysRouteMapModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysRouteMapRenderPlan.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysMenuBarLayout.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysMenuBarPresenter.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysTopMeterModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysTopMeterRenderPlan.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysSystemUi.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysTitleModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysTitleRenderPlan.cpp
        src/renders/ImageRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/VideoRender.c
        src/utils/Demuxer.c
        src/utils/FFmpeg.c
        src/utils/Time.c
    )
    target_include_directories(SchoolDaysTitleMovieRuntimeTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysTitleMovieRuntimeTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysTitleMovieRuntimeTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    add_executable(SchoolDaysRemainingOrsTest
        tests/SchoolDaysRemainingOrsTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/Ors.c
        src/components/Fade.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/graphics/TextureAlpha.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
        ${KOTONOHA_RUNTIME_SOURCE_DIR}/components/Image.cpp
        src/renders/ImageRender.c
        src/utils/FFmpeg.c
        src/utils/Time.c
    )
    target_include_directories(SchoolDaysRemainingOrsTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysRemainingOrsTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysRemainingOrsTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysRemainingOrsTest COMMAND
        SchoolDaysRemainingOrsTest ${CMAKE_CURRENT_SOURCE_DIR}/assets)

    add_executable(SchoolDaysRuntimeCorpusAudit
        tests/SchoolDaysRuntimeCorpusAudit.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/Ors.c
    )
    target_include_directories(SchoolDaysRuntimeCorpusAudit PRIVATE
        ${KOTONOHA_INCLUDE_DIR})
    target_link_libraries(SchoolDaysRuntimeCorpusAudit PRIVATE ${KOTONOHA_SDL3_TARGET})
    set_target_properties(SchoolDaysRuntimeCorpusAudit PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    file(GLOB_RECURSE KOTONOHA_SCHOOL_DAYS_ORS_FIXTURES
        CONFIGURE_DEPENDS
        "${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT}/*.ORS")
    if(KOTONOHA_SCHOOL_DAYS_ORS_FIXTURES)
        add_test(NAME SchoolDaysRuntimeCorpusAudit COMMAND
            SchoolDaysRuntimeCorpusAudit ${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT})
    endif()

    add_executable(SchoolDaysRuntimeParityTest
        tests/SchoolDaysRuntimeParityTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/Ors.c
    )
    target_include_directories(SchoolDaysRuntimeParityTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR})
    target_link_libraries(SchoolDaysRuntimeParityTest PRIVATE ${KOTONOHA_SDL3_TARGET})
    set_target_properties(SchoolDaysRuntimeParityTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    if(EXISTS "${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT}/SysSe/SE_all/school/chime.OGG"
       AND EXISTS "${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT}/Voice05/05-SD/05-SD-A01/05-SD-A01-0740hutaba.OGG")
        add_test(NAME SchoolDaysRuntimeParityTest COMMAND
            SchoolDaysRuntimeParityTest ${KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT})
    endif()
endif()

if(KOTONOHA_ANIME4K_RUNTIME_TESTS AND NOT ANDROID)
    include(CTest)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        message(FATAL_ERROR
            "KOTONOHA_ANIME4K_RUNTIME_TESTS currently requires Windows")
    endif()
    if(NOT OpenGL_FOUND)
        message(FATAL_ERROR
            "KOTONOHA_ANIME4K_RUNTIME_TESTS requires OpenGL")
    endif()

    add_executable(Anime4KRuntimeTest
        tests/Anime4KRuntimeTest.cpp
        src/specials/Anime4K.cpp
        src/specials/OpenGL.c
        src/utils/Log.c
    )
    target_include_directories(Anime4KRuntimeTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )
    target_compile_definitions(Anime4KRuntimeTest PRIVATE
        KOTONOHA_HAS_ANIME4K=1
    )
    target_link_libraries(Anime4KRuntimeTest PRIVATE
        ${KOTONOHA_SDL3_TARGET}
        OpenGL::GL
    )
    set_target_properties(Anime4KRuntimeTest PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    add_custom_command(TARGET Anime4KRuntimeTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${KOTONOHA_ASSETS_DIR}/Anime4K"
        "$<TARGET_FILE_DIR:Anime4KRuntimeTest>/assets/Anime4K"
        VERBATIM
    )
    add_test(NAME Anime4KRuntimeTest COMMAND Anime4KRuntimeTest)
endif()

if(KOTONOHA_ROUTER_TESTS AND NOT ANDROID)
    add_executable(SchoolDaysHandoffRouterTest
        tests/SchoolDaysHandoffRouterTest.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysRouter.cpp
    )
    target_include_directories(SchoolDaysHandoffRouterTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )

    add_executable(SchoolDaysFullRouterTest
        tests/SchoolDaysFullRouterTest.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysRouter.cpp
    )
    target_include_directories(SchoolDaysFullRouterTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )

    add_executable(SchoolDaysDevCheckpointTest
        tests/SchoolDaysDevCheckpointTest.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysRouter.cpp
    )
    target_include_directories(SchoolDaysDevCheckpointTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )
    target_compile_definitions(SchoolDaysDevCheckpointTest PRIVATE
        KOTONOHA_DEV_CHECKPOINTS=1
    )

    # Machine-readable trace adapter used only for KTRF differential
    # conformance. It drives the frozen SchoolDaysRouter routing logic and has
    # test-only access to the state object so targeted pre-states can be
    # injected without changing ResolveNext semantics.
    add_executable(SchoolDaysOracleTrace
        tests/SchoolDaysOracleTrace.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysRouter.cpp
    )
    target_include_directories(SchoolDaysOracleTrace PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )
    target_compile_definitions(SchoolDaysOracleTrace PRIVATE
        KOTONOHA_ROUTER_TEST_ACCESS=1
    )

    add_test(NAME SchoolDaysHandoffRouterTest COMMAND SchoolDaysHandoffRouterTest)
    add_test(NAME SchoolDaysFullRouterTest COMMAND SchoolDaysFullRouterTest)
    add_test(NAME SchoolDaysDevCheckpointTest COMMAND SchoolDaysDevCheckpointTest)
endif()
