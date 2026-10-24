cmake_minimum_required(VERSION 3.18)
project(KtrfSchoolDaysGameplayBridgeGate4 LANGUAGES C CXX)

find_package(SDL3 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIBED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIBED ON)

get_filename_component(KOTONOHA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include_directories("${KOTONOHA_ROOT}/src/Kotonoha/routing")
include_directories("${KOTONOHA_ROOT}/games/school-days/include")

add_executable(KtrfSchoolDaysGameplayBridgeTest
    KtrfSchoolDaysGameplayBridgeTest.cpp
    "${KOTONOHA_ROOT}/src/Kotonoha/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/parsers/KtrfTyped.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/routing/KtrfRuntime.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/routing/KtrfRouter.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/routing/KtrfAdapter.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/routing/KtrfAssetResolver.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/runtime/KtrfStateStore.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysHistory.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysPersistenceCodec.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysPersistence.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysBackRules.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysKtrfSession.cpp"
    "${KOTONOHA_ROOT}/games/school-days/tests/ktrf_common/ScriptDirectoryStub.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysKtrfGameplayBridge.cpp"
)

add_executable(SchoolDaysFlowControllerTest
    SchoolDaysFlowControllerTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysFlowController.cpp"
)

add_executable(SchoolDaysTopMeterModelTest
    SchoolDaysTopMeterModelTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysTopMeterModel.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysTopMeterRenderPlan.cpp"
)

add_executable(SchoolDaysMenuBarLayoutTest
    SchoolDaysMenuBarLayoutTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysMenuBarLayout.cpp"
)
target_link_libraries(SchoolDaysMenuBarLayoutTest PRIVATE SDL3::SDL3)

add_executable(SchoolDaysDisplayPolicyTest
    SchoolDaysDisplayPolicyTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysDisplayPolicy.cpp"
)

add_executable(SchoolDaysRetailConfigPolicyTest
    SchoolDaysRetailConfigPolicyTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysRetailConfigPolicy.cpp"
)

add_executable(SchoolDaysConfigStorageTest
    SchoolDaysConfigStorageTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysConfigStorage.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysRetailConfigPolicy.cpp"
)
target_link_libraries(SchoolDaysConfigStorageTest PRIVATE ZLIB::ZLIB)
if(WIN32)
    get_filename_component(_sd_zlib_prefix "${ZLIB_INCLUDE_DIRS}" DIRECTORY)
    find_file(_sd_zlib_runtime
        NAMES z.dll zlib1.dll
        HINTS "${_sd_zlib_prefix}/bin"
        NO_DEFAULT_PATH
    )
    if(_sd_zlib_runtime)
        add_custom_command(TARGET SchoolDaysConfigStorageTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_sd_zlib_runtime}"
                "$<TARGET_FILE_DIR:SchoolDaysConfigStorageTest>"
            VERBATIM
        )
    endif()
endif()

add_executable(SchoolDaysCmapTest
    SchoolDaysCmapTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
)

add_executable(SchoolDaysSaveFeelModelTest
    SchoolDaysSaveFeelModelTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveFeelModel.cpp"
)

add_executable(SchoolDaysSaveFeelRenderPlanTest
    SchoolDaysSaveFeelRenderPlanTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveFeelModel.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveFeelRenderPlan.cpp"
)

add_executable(SchoolDaysSaveLoadModelTest
    SchoolDaysSaveLoadModelTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveLoadModel.cpp"
)

add_executable(SchoolDaysSaveLoadRenderPlanTest
    SchoolDaysSaveLoadRenderPlanTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveLoadModel.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveLoadRenderPlan.cpp"
)

add_executable(SchoolDaysSaveMetadataTest
    SchoolDaysSaveMetadataTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveMetadata.cpp"
)

add_executable(SchoolDaysSaveServiceReadTest
    SchoolDaysSaveServiceReadTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveServiceRead.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveMetadata.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveProfile.cpp"
)

add_executable(SchoolDaysFontDataTest
    SchoolDaysFontDataTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysFontData.cpp"
)

add_executable(SchoolDaysTitleModelTest
    SchoolDaysTitleModelTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysTitleModel.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysTitleRenderPlan.cpp"
)

add_executable(SchoolDaysConfigExitModelTest
    SchoolDaysConfigExitModelTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysConfigMenuModel.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysExitViewModel.cpp"
)

add_executable(SchoolDaysConfigExitRenderPlanTest
    SchoolDaysConfigExitRenderPlanTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysConfigMenuModel.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysConfigMenuRenderPlan.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysExitViewModel.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysExitViewRenderPlan.cpp"
)

add_executable(SchoolDaysReplayModelTest
    SchoolDaysReplayModelTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysReplayModel.cpp"
)

add_executable(SchoolDaysReplayRenderPlanTest
    SchoolDaysReplayRenderPlanTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysReplayModel.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysReplayRenderPlan.cpp"
)

add_executable(SchoolDaysReplayScriptOracleTest
    SchoolDaysReplayScriptOracleTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysReplayScriptOracle.cpp"
)

add_executable(SchoolDaysReplayPlaybackSessionTest
    SchoolDaysReplayPlaybackSessionTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysReplayScriptOracle.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysReplayPlaybackSession.cpp"
)

add_executable(SchoolDaysPlayDataSessionTest
    SchoolDaysPlayDataSessionTest.cpp
    "${KOTONOHA_ROOT}/src/Kotonoha/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/parsers/KtrfTyped.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/routing/KtrfRuntime.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/routing/KtrfRouter.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/routing/KtrfAdapter.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/routing/KtrfAssetResolver.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/runtime/KtrfStateStore.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysHistory.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysPersistenceCodec.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysPersistence.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysBackRules.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysKtrfSession.cpp"
    "${KOTONOHA_ROOT}/games/school-days/tests/ktrf_common/ScriptDirectoryStub.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysPlayDataSession.cpp"
)

add_executable(SchoolDaysRouteMapModelTest
    SchoolDaysRouteMapModelTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysRouteMapModel.cpp"
)

add_executable(SchoolDaysRouteMapRenderPlanTest
    SchoolDaysRouteMapRenderPlanTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysRouteMapModel.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysRouteMapRenderPlan.cpp"
)

add_executable(SchoolDaysRouteMapOracleTest
    SchoolDaysRouteMapOracleTest.cpp
    "${KOTONOHA_ROOT}/src/Kotonoha/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/parsers/KtrfTyped.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/routing/KtrfRuntime.c"
    "${KOTONOHA_ROOT}/src/Kotonoha/runtime/KtrfStateStore.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysRouteMapOracle.cpp"
)

add_executable(SchoolDaysTitleProfileTest
    SchoolDaysTitleProfileTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysTitleProfile.cpp"
)

add_executable(SchoolDaysTitleMovieClockTest
    SchoolDaysTitleMovieClockTest.cpp
)

add_executable(SchoolDaysSaveStorageTest
    SchoolDaysSaveStorageTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveProfile.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveStorage.cpp"
)

add_executable(SchoolDaysPromptPolicyTest
    SchoolDaysPromptPolicyTest.cpp
)
target_link_libraries(SchoolDaysPromptPolicyTest PRIVATE SDL3::SDL3)

add_executable(SchoolDaysChoiceTextPlanTest
    SchoolDaysChoiceTextPlanTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysChoiceTextPlan.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysFontData.cpp"
)

add_executable(SchoolDaysBackLogTest
    SchoolDaysBackLogTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysCmap.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysBackLog.cpp"
)

add_executable(SchoolDaysGpkIndexTest
    SchoolDaysGpkIndexTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysGpkIndex.cpp"
)

add_executable(SchoolDaysGpkArchiveTest
    SchoolDaysGpkArchiveTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysGpkIndex.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysGpkArchive.cpp"
)
target_link_libraries(SchoolDaysGpkArchiveTest PRIVATE ZLIB::ZLIB)
if(WIN32 AND _sd_zlib_runtime)
    add_custom_command(TARGET SchoolDaysGpkArchiveTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_sd_zlib_runtime}"
            "$<TARGET_FILE_DIR:SchoolDaysGpkArchiveTest>"
        VERBATIM
    )
endif()

add_executable(SchoolDaysAssetResolverTest
    SchoolDaysAssetResolverTest.cpp
)
target_link_libraries(SchoolDaysAssetResolverTest PRIVATE SDL3::SDL3)

target_include_directories(KtrfSchoolDaysGameplayBridgeTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysFlowControllerTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysDisplayPolicyTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysRetailConfigPolicyTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysConfigStorageTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysTopMeterModelTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysMenuBarLayoutTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysCmapTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysSaveFeelModelTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysSaveFeelRenderPlanTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysSaveLoadModelTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysSaveLoadRenderPlanTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysSaveMetadataTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysSaveServiceReadTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysFontDataTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysTitleModelTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysConfigExitModelTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysConfigExitRenderPlanTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysReplayModelTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysReplayRenderPlanTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysReplayScriptOracleTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysReplayPlaybackSessionTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysPlayDataSessionTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysRouteMapModelTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysRouteMapRenderPlanTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysRouteMapOracleTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysTitleProfileTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysTitleMovieClockTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysSaveStorageTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysPromptPolicyTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysChoiceTextPlanTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysBackLogTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysGpkIndexTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(SchoolDaysGpkArchiveTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

target_include_directories(SchoolDaysAssetResolverTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

if(MSVC)
    target_compile_options(KtrfSchoolDaysGameplayBridgeTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysFlowControllerTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysRetailConfigPolicyTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysConfigStorageTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysTopMeterModelTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysMenuBarLayoutTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysCmapTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysSaveFeelModelTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysSaveFeelRenderPlanTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysSaveLoadModelTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysSaveLoadRenderPlanTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysSaveMetadataTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysSaveServiceReadTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysFontDataTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysTitleModelTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysConfigExitModelTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysConfigExitRenderPlanTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysReplayScriptOracleTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysReplayPlaybackSessionTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysPlayDataSessionTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysRouteMapRenderPlanTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysRouteMapOracleTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysTitleProfileTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysTitleMovieClockTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysSaveStorageTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysPromptPolicyTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysChoiceTextPlanTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysBackLogTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysGpkIndexTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysGpkArchiveTest PRIVATE /W4 /permissive-)
    target_compile_options(SchoolDaysAssetResolverTest PRIVATE /W4 /permissive-)
else()
    target_compile_options(KtrfSchoolDaysGameplayBridgeTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysFlowControllerTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysRetailConfigPolicyTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysConfigStorageTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysTopMeterModelTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysMenuBarLayoutTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysCmapTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysSaveFeelModelTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysSaveFeelRenderPlanTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysSaveLoadModelTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysSaveLoadRenderPlanTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysSaveMetadataTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysSaveServiceReadTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysFontDataTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysTitleModelTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysConfigExitModelTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysConfigExitRenderPlanTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysReplayScriptOracleTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysReplayPlaybackSessionTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysPlayDataSessionTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysRouteMapRenderPlanTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysRouteMapOracleTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysTitleProfileTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysTitleMovieClockTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysSaveStorageTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysPromptPolicyTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysChoiceTextPlanTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysBackLogTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysGpkIndexTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysGpkArchiveTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(SchoolDaysAssetResolverTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()
