cmake_minimum_required(VERSION 3.18)
project(SchoolDays VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()
if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

find_package(ZLIB REQUIRED)
add_library(school_days STATIC
    src/KtrfAssetResolver.cpp
    src/KtrfStateStore.cpp
    src/SchoolDaysHistory.cpp
    src/SchoolDaysPersistenceCodec.cpp
    src/SchoolDaysPersistence.cpp
    src/SchoolDaysBackRules.cpp
    src/SchoolDaysKtrfProfile.cpp
    src/SchoolDaysKtrfSession.cpp
    src/SchoolDaysKtrfGameplayBridge.cpp
    src/SchoolDaysGpkArchive.cpp
    src/SchoolDaysGpkIndex.cpp
    src/SchoolDaysAssetMount.cpp
    src/SchoolDaysCmap.cpp
    src/SchoolDaysTitleModel.cpp
    src/SchoolDaysTitleRenderPlan.cpp
    src/SchoolDaysTitleProfile.cpp
    src/SchoolDaysEndBackgroundResolver.cpp
    src/SchoolDaysRouteMapModel.cpp
    src/SchoolDaysRouteMapOracle.cpp
    src/SchoolDaysConfigMenuModel.cpp
    src/SchoolDaysConfigMenuRenderPlan.cpp
    src/SchoolDaysRetailConfigPolicy.cpp
    src/SchoolDaysSaveLoadModel.cpp
    src/SchoolDaysSaveLoadRenderPlan.cpp
    src/SchoolDaysSaveFeelModel.cpp
    src/SchoolDaysSaveFeelRenderPlan.cpp
    src/SchoolDaysExitViewModel.cpp
    src/SchoolDaysExitViewRenderPlan.cpp
    src/SchoolDaysReplayModel.cpp
    src/SchoolDaysReplayRenderPlan.cpp
    src/SchoolDaysRouteMapRenderPlan.cpp
    src/SchoolDaysFontData.cpp
    src/SchoolDaysSaveProfile.cpp
    src/SchoolDaysSaveStorage.cpp
    src/SchoolDaysSaveMetadata.cpp
    src/SchoolDaysSaveMetadataState.cpp
    src/SchoolDaysSaveService.cpp
    src/SchoolDaysSaveServiceRead.cpp
    src/SchoolDaysBackLog.cpp
    src/SchoolDaysBackLogPresenter.cpp
    src/SchoolDaysSubtitleTypography.cpp
    src/SchoolDaysSystemUi.cpp
    src/SchoolDaysAssets.cpp
    src/SchoolDaysRuntime.cpp
    src/SchoolDaysScenes.cpp
    src/SchoolDaysLauncher.cpp
)
target_include_directories(school_days PUBLIC include src)
target_link_libraries(school_days PUBLIC Kotonoha::Engine PRIVATE ZLIB::ZLIB)
target_compile_features(school_days PUBLIC cxx_std_17)
set_target_properties(school_days PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(SchoolDays src/main.cpp)
target_link_libraries(SchoolDays PRIVATE school_days)
kotonoha_configure_app(SchoolDays
    BUNDLE_ID me.hirameki.kotonoha.schooldays GAME school-days)

if(BUILD_TESTING AND NOT ANDROID)
    add_subdirectory(tests)
endif()
