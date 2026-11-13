cmake_minimum_required(VERSION 3.16)

project(SchoolDays VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

find_package(ZLIB REQUIRED)
if(NOT TARGET Kotonoha::Modules::Providers::GPK)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/gpk"
        "${CMAKE_CURRENT_BINARY_DIR}/providers/gpk")
endif()
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
    src/SchoolDaysDisplayPolicy.cpp
    src/SchoolDaysConfigStorage.cpp
    src/SchoolDaysMenuBarPresenter.cpp
    src/SchoolDaysMenuBarLayout.cpp
    src/SchoolDaysTopMeterModel.cpp
    src/SchoolDaysTopMeterRenderPlan.cpp
    src/SchoolDaysChoiceTextPlan.cpp
    src/SchoolDaysSaveLoadModel.cpp
    src/SchoolDaysSaveLoadRenderPlan.cpp
    src/SchoolDaysSaveFeelModel.cpp
    src/SchoolDaysSaveFeelRenderPlan.cpp
    src/SchoolDaysExitViewModel.cpp
    src/SchoolDaysExitViewRenderPlan.cpp
    src/SchoolDaysReplayModel.cpp
    src/SchoolDaysReplayRenderPlan.cpp
    src/SchoolDaysReplayScriptOracle.cpp
    src/SchoolDaysReplayPlaybackSession.cpp
    src/SchoolDaysPlayDataSession.cpp
    src/SchoolDaysRouteMapRenderPlan.cpp
    src/SchoolDaysFontData.cpp
    src/SchoolDaysSaveProfile.cpp
    src/SchoolDaysSaveStorage.cpp
    src/SchoolDaysSaveMetadata.cpp
    src/SchoolDaysSaveMetadataState.cpp
    src/SchoolDaysSaveService.cpp
    src/SchoolDaysSaveServiceRead.cpp
    src/SchoolDaysBackLog.cpp
    src/SchoolDaysBackLogEngine.cpp
    src/SchoolDaysBackLogPresenter.cpp
    src/SchoolDaysSubtitleTypography.cpp
    src/SchoolDaysSystemUi.cpp
    src/SchoolDaysRuntime.cpp
    src/SchoolDaysScenes.cpp
    src/SchoolDaysLauncher.cpp
)
target_include_directories(school_days PUBLIC include src)
target_compile_features(school_days PUBLIC cxx_std_17)
target_link_libraries(school_days PUBLIC Kotonoha::Engine Kotonoha::Modules::Providers::GPK PRIVATE ZLIB::ZLIB)
set_target_properties(school_days PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(SchoolDays src/main.cpp)
target_link_libraries(SchoolDays PRIVATE school_days)
kotonoha_configure_app(SchoolDays
    BUNDLE_ID me.hirameki.kotonoha.schooldays GAME school-days)
set_target_properties(SchoolDays PROPERTIES CXX_EXTENSIONS OFF)

# Ship the user's replacement General, Sound and Display Options artwork outside
# the commercial System.GPK. The School Days-only overlay is mounted by default
# when no explicit --asset-overlay is selected. Other games are unaffected.
if(WIN32 AND NOT ANDROID)
    set(SCHOOL_DAYS_DISPLAY_OVERLAY "${CMAKE_CURRENT_SOURCE_DIR}/display-options")
    # The tracked README keeps this directory present even in source-only
    # checkouts. Create the target unconditionally so adding the external
    # artwork *after configuration* needs only another build, not reconfigure.
    if(EXISTS "${SCHOOL_DAYS_DISPLAY_OVERLAY}")
        get_property(_school_days_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
        if(_school_days_multi_config)
            set(_school_days_overlay_output "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/display-options")
        else()
            set(_school_days_overlay_output "${CMAKE_CURRENT_BINARY_DIR}/display-options")
        endif()
        # Always sync assets, including edits made without recompiling C++.
        # POST_BUILD alone does not run when the executable is already current.
        add_custom_target(SchoolDaysOptionAssets
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SCHOOL_DAYS_DISPLAY_OVERLAY}"
                "${_school_days_overlay_output}"
            COMMENT "Syncing School Days option artwork (retail System.GPK unchanged)"
            VERBATIM)
        add_dependencies(SchoolDays SchoolDaysOptionAssets)
    endif()
endif()

if(BUILD_TESTING AND NOT ANDROID)
    add_subdirectory(tests)
endif()
