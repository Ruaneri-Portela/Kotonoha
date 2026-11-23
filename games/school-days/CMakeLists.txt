cmake_minimum_required(VERSION 3.18)
project(SchoolDays VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()
if(NOT TARGET KotonohaEngine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

find_package(ZLIB REQUIRED)
if(NOT TARGET kotonoha_provider_gpk)
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
    src/SchoolDaysAtomicFile.cpp
    src/SchoolDaysSaveStorage.cpp
    src/SchoolDaysSaveProvenance.cpp
    src/SchoolDaysSaveMetadata.cpp
    src/SchoolDaysSaveMetadataState.cpp
    src/SchoolDaysSaveService.cpp
    src/SchoolDaysSaveServiceRead.cpp
    src/SchoolDaysBackLog.cpp
    src/SchoolDaysBackLogPresenter.cpp
    src/SchoolDaysSubtitleTypography.cpp
    src/SchoolDaysSystemUi.cpp
    src/SchoolDaysAssets.cpp
    src/SchoolDaysDisplayNarrativeMarkup.cpp
    src/SchoolDaysLxhPreference.cpp
    src/SchoolDaysTranslationCoverage.cpp
    src/SchoolDaysRuntime.cpp
    src/SchoolDaysScenes.cpp
    src/SchoolDaysLauncherLocale.cpp
    src/SchoolDaysLauncher.cpp
)
target_include_directories(school_days PUBLIC include src)
target_link_libraries(school_days PUBLIC KotonohaEngine kotonoha_provider_gpk
    PRIVATE ZLIB::ZLIB)
target_compile_features(school_days PUBLIC cxx_std_17)
if(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    target_compile_definitions(school_days PUBLIC KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY=1)
endif()
# Provider exposes only shared interface headers. The game-specific archive,
# translation and LxH implementations above have one owner in this target.
set_target_properties(school_days PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(SchoolDays src/main.cpp)
target_link_libraries(SchoolDays PRIVATE school_days)
kotonoha_configure_app(SchoolDays
    BUNDLE_ID me.hirameki.kotonoha.schooldays GAME school-days)

# The engine's DEV_CONTROL definition is PRIVATE, so propagate it explicitly
# to the School Days entry point as well. Otherwise its dev-startup.log trace
# is silently compiled out even when the developer bridge is enabled.
if(WIN32 AND NOT ANDROID AND KOTONOHA_DEV_CONTROL AND
   NOT KOTONOHA_DISTRIBUTION_BUILD)
    target_compile_definitions(SchoolDays PRIVATE KOTONOHA_DEV_CONTROL=1)
endif()

# A player-facing Windows build is a GUI application: the SDL callback entry
# point opens our launcher directly. Never attach a second console window just
# to display SDL logging. Keep the development executable's console for QA.
if(WIN32 AND NOT ANDROID AND KOTONOHA_DISTRIBUTION_BUILD)
    set_target_properties(SchoolDays PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# The Windows executable uses the black-and-white icon supplied specifically
# for School Days HQ. Only register the resource once the approved artwork has
# been converted to an .ico; a missing icon must not break existing L4 builds.
if(WIN32 AND NOT ANDROID)
    set(SCHOOL_DAYS_WINDOWS_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/SchoolDays.ico")
    if(NOT EXISTS "${SCHOOL_DAYS_WINDOWS_ICON}")
        get_filename_component(_school_days_repo_root
            "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
        if(EXISTS "${_school_days_repo_root}/assets/SchoolDays.ico")
            set(SCHOOL_DAYS_WINDOWS_ICON
                "${_school_days_repo_root}/assets/SchoolDays.ico")
        endif()
    endif()
    if(EXISTS "${SCHOOL_DAYS_WINDOWS_ICON}")
        get_filename_component(_school_days_icon_dir
            "${SCHOOL_DAYS_WINDOWS_ICON}" DIRECTORY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${SCHOOL_DAYS_WINDOWS_ICON}")
        enable_language(RC)
        set(SCHOOL_DAYS_WINDOWS_RC
            "${CMAKE_CURRENT_SOURCE_DIR}/windows/SchoolDays.rc")
        set_source_files_properties("${SCHOOL_DAYS_WINDOWS_RC}" PROPERTIES
            INCLUDE_DIRECTORIES "${_school_days_icon_dir}")
        target_sources(SchoolDays PRIVATE "${SCHOOL_DAYS_WINDOWS_RC}")
    else()
        message(STATUS "School Days Windows icon pending: ${SCHOOL_DAYS_WINDOWS_ICON}")
    endif()
endif()

# This font is a separately supplied local QA asset, not tracked game content.
# Stage it AFTER kotonoha_configure_app's generic asset POST_BUILD copy so a
# placeholder in the engine asset directory cannot overwrite the real font.
# A separate verify target is mandatory before assembling a private QA ZIP.
if(WIN32 AND NOT ANDROID)
    set(KOTONOHA_SCHOOL_DAYS_LAUNCHER_FONT_SOURCE "" CACHE FILEPATH
        "Path to approved local NotoSansJP-Regular.ttf for School Days Windows QA")
    set(_school_days_font_script
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/StageLauncherFont.cmake")
    set(_school_days_font_output
        "$<TARGET_FILE_DIR:SchoolDays>/assets/fonts/NotoSansJP-Regular.ttf")
    if(KOTONOHA_SCHOOL_DAYS_LAUNCHER_FONT_SOURCE)
        add_custom_command(TARGET SchoolDays POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DKOTONOHA_FONT_ACTION=stage"
                "-DKOTONOHA_FONT_SOURCE=${KOTONOHA_SCHOOL_DAYS_LAUNCHER_FONT_SOURCE}"
                "-DKOTONOHA_FONT_DESTINATION=${_school_days_font_output}"
                -P "${_school_days_font_script}"
            COMMENT "Stage and verify the approved School Days Windows launcher font"
            VERBATIM)
    endif()
    add_custom_target(SchoolDaysVerifyLauncherFont
        COMMAND "${CMAKE_COMMAND}"
            "-DKOTONOHA_FONT_ACTION=verify"
            "-DKOTONOHA_FONT_DESTINATION=${_school_days_font_output}"
            -P "${_school_days_font_script}"
        DEPENDS SchoolDays
        COMMENT "Fail closed if the School Days Windows QA launcher font is missing or a stub"
        VERBATIM)
endif()

# Use only the player's authorized School Days HQ key visual. Always copy it
# from the source tree to the binary folder, including asset-only rebuilds.
# The single SchoolDays.exe loads this PNG; no separate launcher EXE is needed.
if(WIN32 AND NOT ANDROID)
    set(KOTONOHA_SCHOOL_DAYS_LAUNCHER_ART_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/game-assets/launcher-art/SchoolDaysHQ-Launcher.png"
        CACHE FILEPATH "Private School Days HQ launcher artwork source")
    set(SCHOOL_DAYS_LAUNCHER_HERO "${KOTONOHA_SCHOOL_DAYS_LAUNCHER_ART_SOURCE}")
    if(NOT EXISTS "${SCHOOL_DAYS_LAUNCHER_HERO}")
        message(WARNING "School Days HQ launcher artwork missing: ${SCHOOL_DAYS_LAUNCHER_HERO}")
    endif()
    # The executable is emitted under outputs/<config>/SchoolDays, not the
    # CMake subdirectory. Read the configured target property rather than using
    # $<TARGET_FILE_DIR:SchoolDays>, which would create a CMake dependency cycle
    # because SchoolDays itself depends on this staging target.
    get_target_property(_school_days_runtime_output SchoolDays RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _school_days_runtime_output)
        message(FATAL_ERROR "School Days runtime output directory is undefined")
    endif()
    set(_school_days_hero_out "${_school_days_runtime_output}/assets")
    # Keep this target even when optional art is currently missing, so a new
    # file can be supplied and staged by the next build without reconfiguring.
    add_custom_target(SchoolDaysLauncherArtwork
        COMMAND "${CMAKE_COMMAND}"
            "-DKOTONOHA_ASSET_KIND=launcher"
            "-DKOTONOHA_ASSET_SOURCE=${SCHOOL_DAYS_LAUNCHER_HERO}"
            "-DKOTONOHA_ASSET_DESTINATION=${_school_days_hero_out}/SchoolDaysHQ-Launcher.png"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/windows/StageOptionalAssets.cmake"
        COMMENT "Sync School Days HQ launcher image (no retail GPK changes)"
        VERBATIM)
    add_dependencies(SchoolDays SchoolDaysLauncherArtwork)
endif()

# Ship the tracked route through the engine's platform-specific asset hook.
kotonoha_preload_asset(SchoolDays
    "${CMAKE_CURRENT_SOURCE_DIR}/game-assets/school-days-hq.ktnroute"
    "/game-assets/school-days-hq.ktnroute")

# Stage user-supplied options into the standard <exe>/overlay directory,
# independently of the commercial System.GPK. Accept the older private source
# tree when game-assets/overlay has not been supplied yet.
if(WIN32 AND NOT ANDROID)
    set(_school_days_overlay_source
        "${CMAKE_CURRENT_SOURCE_DIR}/game-assets/overlay")
    set(_school_days_overlay_legacy
        "${CMAKE_CURRENT_SOURCE_DIR}/game-assets/display-options")
    set(KOTONOHA_SCHOOL_DAYS_DISPLAY_OVERLAY_SOURCE
        "${_school_days_overlay_source}"
        CACHE PATH "Private School Days HQ option artwork source")
    set(SCHOOL_DAYS_DISPLAY_OVERLAY "${KOTONOHA_SCHOOL_DAYS_DISPLAY_OVERLAY_SOURCE}")
    # A previous CMake cache may still contain the old default source.
    # Migrate that known default while preserving other custom source paths.
    if(SCHOOL_DAYS_DISPLAY_OVERLAY STREQUAL _school_days_overlay_legacy)
        set(SCHOOL_DAYS_DISPLAY_OVERLAY "${_school_days_overlay_source}")
    endif()
    # Check the fallback on every build so newly supplied standard artwork
    # takes precedence without reconfiguring. Other custom cache paths win.
    set(_school_days_overlay_fallback "")
    if(SCHOOL_DAYS_DISPLAY_OVERLAY STREQUAL _school_days_overlay_source)
        set(_school_days_overlay_fallback "${_school_days_overlay_legacy}")
    endif()
    if((NOT EXISTS "${SCHOOL_DAYS_DISPLAY_OVERLAY}/System/Option/Option_Display.png" OR
        NOT EXISTS "${SCHOOL_DAYS_DISPLAY_OVERLAY}/System/Option/Option_Display.cmap") AND
       (NOT _school_days_overlay_fallback OR
        NOT EXISTS "${_school_days_overlay_fallback}/System/Option/Option_Display.png" OR
        NOT EXISTS "${_school_days_overlay_fallback}/System/Option/Option_Display.cmap"))
        message(WARNING "School Days HQ Display Options PNG/CMAP missing under: ${SCHOOL_DAYS_DISPLAY_OVERLAY}")
    endif()
    # Stage the custom options beside SchoolDays.exe as well, so a clean
    # distribution does not silently lose them when moved from its build tree.
    set(_school_days_overlay_output "${_school_days_runtime_output}/overlay")
    # Always sync assets, including edits made without recompiling C++.
    add_custom_target(SchoolDaysOptionAssets
        COMMAND "${CMAKE_COMMAND}"
            "-DKOTONOHA_ASSET_KIND=display-options"
            "-DKOTONOHA_ASSET_SOURCE=${SCHOOL_DAYS_DISPLAY_OVERLAY}"
            "-DKOTONOHA_ASSET_FALLBACK_SOURCE=${_school_days_overlay_fallback}"
            "-DKOTONOHA_ASSET_DESTINATION=${_school_days_overlay_output}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/windows/StageOptionalAssets.cmake"
        COMMENT "Syncing School Days option artwork (retail System.GPK unchanged)"
        VERBATIM)
    add_dependencies(SchoolDays SchoolDaysOptionAssets)
endif()

if(BUILD_TESTING AND NOT ANDROID)
    add_subdirectory(tests)
endif()
