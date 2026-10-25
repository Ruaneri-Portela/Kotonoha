# School Days regression tests. Added by ../CMakeLists.txt when
# KOTONOHA_ROUTER_TESTS or KOTONOHA_ORS_PARSER_TESTS is enabled; CTest and
# KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT are configured by the root CMakeLists.
# The ktrf_* folders are standalone CMake projects (cmake -S <folder>) and
# ktrf/ holds the Python tests for tools/ktrf.

include_directories("${SCHOOL_DAYS_INCLUDE_DIR}")

    add_executable(SchoolDaysSceneTimelineTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysSceneTimelineTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/Ors.c
    )
    target_include_directories(SchoolDaysSceneTimelineTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )
    target_link_libraries(SchoolDaysSceneTimelineTest PRIVATE ${KOTONOHA_SDL3_TARGET})
    set_target_properties(SchoolDaysSceneTimelineTest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysSceneTimelineTest COMMAND SchoolDaysSceneTimelineTest)

    add_executable(SchoolDaysAudioEventTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysAudioEventTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/Ors.c
    )
    target_include_directories(SchoolDaysAudioEventTest PRIVATE ${KOTONOHA_INCLUDE_DIR})
    target_link_libraries(SchoolDaysAudioEventTest PRIVATE ${KOTONOHA_SDL3_TARGET})
    set_target_properties(SchoolDaysAudioEventTest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysAudioEventTest COMMAND SchoolDaysAudioEventTest)

    add_executable(SchoolDaysBgmEventTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysBgmEventTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/components/Audio.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/components/Sound.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/AudioRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Demuxer.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/FFmpeg.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Time.c
    )
    target_include_directories(SchoolDaysBgmEventTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysBgmEventTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysBgmEventTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysBgmEventTest COMMAND SchoolDaysBgmEventTest)

    add_executable(SchoolDaysGpkDirectRuntimeTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysGpkDirectRuntimeTest.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysAssetMount.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysGpkArchive.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysGpkIndex.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/AudioRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/ImageRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/VideoRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Demuxer.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/FFmpeg.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Time.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/VoicePcm.c
    )
    target_include_directories(SchoolDaysGpkDirectRuntimeTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysGpkDirectRuntimeTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG ZLIB::ZLIB)
    set_target_properties(SchoolDaysGpkDirectRuntimeTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    add_executable(SchoolDaysAbcRuntimeTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysAbcRuntimeTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/audio/VoicePcm.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/graphics/TextureAlpha.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/components/Upscaling.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
        ${KOTONOHA_RUNTIME_SOURCE_DIR}/components/Image.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/ImageRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/FFmpeg.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Time.c
    )
    target_include_directories(SchoolDaysAbcRuntimeTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysAbcRuntimeTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysAbcRuntimeTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysAbcRuntimeTest COMMAND
        SchoolDaysAbcRuntimeTest ${CMAKE_SOURCE_DIR}/assets)

    add_executable(SchoolDaysAbcOverlayHotfixTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysAbcOverlayHotfixTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/graphics/TextureAlpha.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/components/Upscaling.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
        ${KOTONOHA_RUNTIME_SOURCE_DIR}/components/Image.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/ImageRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/FFmpeg.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Time.c
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
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysAbcEdgeSeamTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/graphics/TextureAlpha.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/components/Upscaling.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
        ${KOTONOHA_RUNTIME_SOURCE_DIR}/components/Image.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/ImageRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/FFmpeg.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Time.c
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
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysMovieEventTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/components/Video.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/VideoRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Demuxer.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/FFmpeg.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Time.c
    )
    target_include_directories(SchoolDaysMovieEventTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysMovieEventTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysMovieEventTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysMovieEventTest COMMAND
        SchoolDaysMovieEventTest ${CMAKE_SOURCE_DIR}/assets)

    add_executable(SchoolDaysTitleMovieRuntimeTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysTitleMovieRuntimeTest.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysCmap.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysFontData.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysSaveFeelModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysSaveFeelRenderPlan.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysSaveLoadModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysSaveLoadRenderPlan.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysConfigMenuModel.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysConfigMenuRenderPlan.cpp
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
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/ImageRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/VideoRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Demuxer.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/FFmpeg.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Time.c
    )
    target_include_directories(SchoolDaysTitleMovieRuntimeTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysTitleMovieRuntimeTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysTitleMovieRuntimeTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    add_executable(SchoolDaysRemainingOrsTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysRemainingOrsTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/Ors.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/components/Fade.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/graphics/TextureAlpha.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/components/Upscaling.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
        ${KOTONOHA_RUNTIME_SOURCE_DIR}/components/Image.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/renders/ImageRender.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/FFmpeg.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Time.c
    )
    target_include_directories(SchoolDaysRemainingOrsTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(SchoolDaysRemainingOrsTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(SchoolDaysRemainingOrsTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME SchoolDaysRemainingOrsTest COMMAND
        SchoolDaysRemainingOrsTest ${CMAKE_SOURCE_DIR}/assets)

    add_executable(SchoolDaysRuntimeCorpusAudit
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysRuntimeCorpusAudit.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysRuntimeParityTest.cpp
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

if(KOTONOHA_ROUTER_TESTS)
    add_executable(SchoolDaysHandoffRouterTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysHandoffRouterTest.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysRouter.cpp
    )
    target_include_directories(SchoolDaysHandoffRouterTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )

    add_executable(SchoolDaysFullRouterTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysFullRouterTest.cpp
        ${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysRouter.cpp
    )
    target_include_directories(SchoolDaysFullRouterTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )

    add_executable(SchoolDaysDevCheckpointTest
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysDevCheckpointTest.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysOracleTrace.cpp
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
