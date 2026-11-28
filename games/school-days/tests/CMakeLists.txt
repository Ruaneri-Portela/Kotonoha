set(SCHOOL_DAYS_ROUTE "${CMAKE_CURRENT_SOURCE_DIR}/../game-assets/school-days-hq.ktnroute")

add_executable(SchoolDaysPresencePreferenceTest SchoolDaysPresencePreferenceTest.cpp)
target_link_libraries(SchoolDaysPresencePreferenceTest PRIVATE school_days)
add_test(NAME SchoolDays.PresencePreference COMMAND SchoolDaysPresencePreferenceTest)

add_executable(SchoolDaysPresencePolicyTest SchoolDaysPresencePolicyTest.cpp)
target_link_libraries(SchoolDaysPresencePolicyTest PRIVATE school_days)
add_test(NAME SchoolDays.PresencePolicy COMMAND SchoolDaysPresencePolicyTest)

add_executable(SchoolDaysEmbeddedAssetsTest SchoolDaysEmbeddedAssetsTest.cpp)
target_link_libraries(SchoolDaysEmbeddedAssetsTest PRIVATE school_days)
add_test(NAME SchoolDays.EmbeddedAssets COMMAND SchoolDaysEmbeddedAssetsTest
    "${KOTONOHA_SCHOOL_DAYS_RUNTIME_ASSETS_SOURCE}")
if(WIN32)
    set_tests_properties(SchoolDays.EmbeddedAssets PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
endif()

if(WIN32)
    add_test(NAME SchoolDays.LauncherFontStage
        COMMAND "${CMAKE_COMMAND}"
            "-DSTAGE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/../windows/StageLauncherFont.cmake"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/launcher-font-stage-test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/SchoolDaysLauncherFontStageTest.cmake")
endif()

# Shared launcher/asset-provider precedence with synthetic PNG/CMAP files.
add_executable(SchoolDaysOverlaySelectionTest SchoolDaysOverlaySelectionTest.cpp)
add_test(NAME SchoolDays.OverlaySelection COMMAND SchoolDaysOverlaySelectionTest)

add_executable(SchoolDaysPortTest SchoolDaysPortTest.cpp)
target_link_libraries(SchoolDaysPortTest PRIVATE school_days)
add_test(NAME SchoolDaysPortTest COMMAND SchoolDaysPortTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set_tests_properties(SchoolDaysPortTest PROPERTIES FIXTURES_SETUP school_days_assets)

if(KOTONOHA_DISTRIBUTION_BUILD)
    # Counterpart to the engine's DEV MCP command-line positive test. Never
    # skip that original test: this separate DIST negative test detects any
    # accidental return of developer MCP/log controls to public builds.
    add_executable(SchoolDaysDistDebugAbsenceTest
        SchoolDaysDistDebugAbsenceTest.cpp)
    # This contract only exercises the shared engine CLI; linking the game
    # runtime would introduce unrelated retail presentation dependencies.
    target_link_libraries(SchoolDaysDistDebugAbsenceTest PRIVATE KotonohaEngine)
    add_test(NAME SchoolDays.DistDebugAbsence
        COMMAND SchoolDaysDistDebugAbsenceTest)
    if(WIN32)
        set_tests_properties(SchoolDays.DistDebugAbsence PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
    endif()
endif()

add_executable(SchoolDaysSessionTest KtrfSchoolDaysSessionTest.cpp)
target_link_libraries(SchoolDaysSessionTest PRIVATE school_days)
add_test(NAME SchoolDaysSessionTest COMMAND SchoolDaysSessionTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures/Script/English")
set_tests_properties(SchoolDaysSessionTest PROPERTIES FIXTURES_REQUIRED school_days_assets)

add_executable(SchoolDaysBridgeTest KtrfSchoolDaysGameplayBridgeTest.cpp)
target_link_libraries(SchoolDaysBridgeTest PRIVATE school_days)
add_test(NAME SchoolDaysBridgeTest COMMAND SchoolDaysBridgeTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures/Script/English")
set_tests_properties(SchoolDaysBridgeTest PROPERTIES FIXTURES_REQUIRED school_days_assets)

add_executable(SchoolDaysSceneTimelineTest SchoolDaysSceneTimelineTest.cpp)
add_executable(SchoolDaysShaderPresetCatalogTest SchoolDaysShaderPresetCatalogTest.cpp)
target_link_libraries(SchoolDaysShaderPresetCatalogTest PRIVATE school_days)
add_test(NAME SchoolDaysShaderPresetCatalogTest COMMAND SchoolDaysShaderPresetCatalogTest)
add_executable(SchoolDaysGpkLanguageProfileTest SchoolDaysGpkLanguageProfileTest.cpp)
target_link_libraries(SchoolDaysGpkLanguageProfileTest PRIVATE school_days)
add_test(NAME SchoolDays.GpkLanguageProfile COMMAND SchoolDaysGpkLanguageProfileTest)
add_executable(SchoolDaysTranslationLayoutTest SchoolDaysTranslationLayoutTest.cpp)
target_link_libraries(SchoolDaysTranslationLayoutTest PRIVATE school_days)
add_test(NAME SchoolDays.TranslationLayout COMMAND SchoolDaysTranslationLayoutTest)
# School Days' native horizontal BackLog must preserve glyph aspect and stay
# outside the retail scroll controls and bottom skin band in every variant.
# The synthetic CMAP fixture and dialogue are generated inside the build tree.
add_executable(SchoolDaysBackLogGeometryTest SchoolDaysBackLogGeometryTest.cpp)
target_link_libraries(SchoolDaysBackLogGeometryTest PRIVATE school_days)
add_test(NAME SchoolDays.BackLogGeometry COMMAND SchoolDaysBackLogGeometryTest
    "${CMAKE_CURRENT_BINARY_DIR}/backlog-geometry-fixture")
# Exercises all 161 retail-inventoried selector IDs across the 16 pages and
# four screen variants with generated CMAPs. No original GPK/save is used.
add_executable(SchoolDaysRouteMapMatrixTest SchoolDaysRouteMapMatrixTest.cpp)
target_link_libraries(SchoolDaysRouteMapMatrixTest PRIVATE school_days)
add_test(NAME SchoolDays.RouteMapMatrix COMMAND SchoolDaysRouteMapMatrixTest
    "${CMAKE_CURRENT_BINARY_DIR}/route-map-matrix-fixture")
add_executable(SchoolDaysTranslatedGpkMountTest SchoolDaysTranslatedGpkMountTest.cpp)
target_link_libraries(SchoolDaysTranslatedGpkMountTest PRIVATE school_days ZLIB::ZLIB)
add_test(NAME SchoolDays.TranslatedGpkMount COMMAND SchoolDaysTranslatedGpkMountTest)
if(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    add_executable(SchoolDaysRetailOnlyGpkProfileTest
        SchoolDaysRetailOnlyGpkProfileTest.cpp)
    target_link_libraries(SchoolDaysRetailOnlyGpkProfileTest PRIVATE school_days)
    add_test(NAME SchoolDays.RetailOnlyGpkProfile
        COMMAND SchoolDaysRetailOnlyGpkProfileTest)
endif()
add_executable(SchoolDaysLxhRetailSaveProbe SchoolDaysLxhRetailSaveProbe.cpp)
target_link_libraries(SchoolDaysLxhRetailSaveProbe PRIVATE school_days)
# Not registered in CTest: opt-in only for private retail packs and saves.
add_executable(SchoolDaysExternalShaderGpuProbe SchoolDaysExternalShaderGpuProbe.cpp)
target_link_libraries(SchoolDaysExternalShaderGpuProbe PRIVATE school_days)
# Deliberately not registered as CTest: only opt-in machines with a real
# Windows/OpenGL driver should run this executable with a trusted shader file.
target_link_libraries(SchoolDaysSceneTimelineTest PRIVATE school_days)
add_test(NAME SchoolDaysSceneTimelineTest COMMAND SchoolDaysSceneTimelineTest)

# School Days-specific integration of Ruaneri's standard per-NODE JSON
# dictionary; all ORS/text in this fixture are synthetic.
add_executable(SchoolDaysJsonTranslationTest SchoolDaysJsonTranslationTest.cpp)
target_link_libraries(SchoolDaysJsonTranslationTest PRIVATE school_days)
add_test(NAME SchoolDays.JsonTranslation COMMAND SchoolDaysJsonTranslationTest
    "${SCHOOL_DAYS_ROUTE}")
set_tests_properties(SchoolDays.JsonTranslation PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

add_executable(SchoolDaysTranslationCoverageTest
    SchoolDaysTranslationCoverageTest.cpp)
target_link_libraries(SchoolDaysTranslationCoverageTest PRIVATE school_days)
add_test(NAME SchoolDays.TranslationCoverage
    COMMAND SchoolDaysTranslationCoverageTest)

add_executable(SchoolDaysTranslationContextTest
    SchoolDaysTranslationContextTest.cpp)
target_link_libraries(SchoolDaysTranslationContextTest PRIVATE school_days)
add_test(NAME SchoolDays.TranslationContext
    COMMAND SchoolDaysTranslationContextTest)

add_executable(SchoolDaysLxhLocaleTest SchoolDaysLxhLocaleTest.cpp)
target_link_libraries(SchoolDaysLxhLocaleTest PRIVATE school_days)
add_test(NAME SchoolDays.LxhLocale COMMAND SchoolDaysLxhLocaleTest)

add_executable(SchoolDaysDisplayNarrativeMarkupTest
    SchoolDaysDisplayNarrativeMarkupTest.cpp)
target_link_libraries(SchoolDaysDisplayNarrativeMarkupTest PRIVATE school_days)
add_test(NAME SchoolDays.DisplayNarrativeMarkup
    COMMAND SchoolDaysDisplayNarrativeMarkupTest)

# Real School Days subtitle code with generated styles.ass and multilingual
# speaker identifiers, using only a software renderer and synthetic cue text.
add_executable(SchoolDaysSubtitleSpeakerStyleTest
    SchoolDaysSubtitleSpeakerStyleTest.cpp)
target_link_libraries(SchoolDaysSubtitleSpeakerStyleTest PRIVATE school_days)
add_test(NAME SchoolDays.SubtitleSpeakerStyle
    COMMAND SchoolDaysSubtitleSpeakerStyleTest
    "${CMAKE_CURRENT_BINARY_DIR}/ass-speaker-synthetic")
set_tests_properties(SchoolDays.SubtitleSpeakerStyle PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

# Validate the actual packaged palette; no GPK, font download or GUI required.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME SchoolDays.SubtitleStylesAss
    COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_styles_ass.py")

# Optional private-content QA. It consumes user-provided GPKs and JSON at
# explicit CLI paths and is never automatically executed by CTest.
add_executable(SchoolDaysSpanishChoiceWrapProbe
    SchoolDaysSpanishChoiceWrapProbe.cpp)
target_link_libraries(SchoolDaysSpanishChoiceWrapProbe PRIVATE school_days)

add_executable(SchoolDaysAudioEventStateTest SchoolDaysAudioEventStateTest.cpp)
target_link_libraries(SchoolDaysAudioEventStateTest PRIVATE school_days)
add_test(NAME SchoolDaysAudioEventStateTest COMMAND SchoolDaysAudioEventStateTest)

# Synthetic ORS-only regression: LxH soundtrack is one PlaySe, with no
# independently selectable male voice stream. Never load proprietary media.
add_executable(SchoolDaysLxhMixedAudioTest SchoolDaysLxhMixedAudioTest.cpp)
target_link_libraries(SchoolDaysLxhMixedAudioTest PRIVATE school_days)
add_test(NAME SchoolDaysLxhMixedAudioTest COMMAND SchoolDaysLxhMixedAudioTest)

add_executable(SchoolDaysAssetsListTest SchoolDaysAssetsListTest.cpp)
target_link_libraries(SchoolDaysAssetsListTest PRIVATE school_days)
add_test(NAME SchoolDaysAssetsListTest COMMAND SchoolDaysAssetsListTest)

# User-supplied retail GPKs plus mandatory Packs/LxH.GPK are NEVER bundled or
# committed. This optional read-only validation binary requires explicit local
# paths and is deliberately not registered with CTest (no proprietary fixtures).
add_executable(SchoolDaysLxhPrivateMountProbe SchoolDaysLxhPrivateMountProbe.cpp)
target_link_libraries(SchoolDaysLxhPrivateMountProbe PRIVATE school_days)

# Immutable 7-ending / 28-path manifest; no LxH ON/OFF preference exists.
add_executable(SchoolDaysLxhContentTest SchoolDaysLxhContentTest.cpp)
target_link_libraries(SchoolDaysLxhContentTest PRIVATE school_days)
add_test(NAME SchoolDaysLxhContentTest COMMAND SchoolDaysLxhContentTest)

add_executable(SchoolDaysOptionalLxhTest SchoolDaysOptionalLxhTest.cpp)
target_link_libraries(SchoolDaysOptionalLxhTest PRIVATE school_days ZLIB::ZLIB)
add_test(NAME SchoolDays.OptionalLxh COMMAND SchoolDaysOptionalLxhTest
    "${SCHOOL_DAYS_ROUTE}")
set_tests_properties(SchoolDays.OptionalLxh PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

add_executable(SchoolDaysLauncherLocaleTest SchoolDaysLauncherLocaleTest.cpp)
target_link_libraries(SchoolDaysLauncherLocaleTest PRIVATE school_days)
add_test(NAME SchoolDays.LauncherLocale COMMAND SchoolDaysLauncherLocaleTest)

add_executable(SchoolDaysLxhFadeTest SchoolDaysLxhFadeTest.cpp)
target_link_libraries(SchoolDaysLxhFadeTest PRIVATE school_days)
add_test(NAME SchoolDaysLxhFadeTest COMMAND SchoolDaysLxhFadeTest)

# Pure wall-clock regression for the shared-tick WMV/OGG preparation stall.
# No retail packs, decoder, saves, or GUI are needed by this synthetic test.
add_executable(SchoolDaysMediaPreparationWatchdogTest
    SchoolDaysMediaPreparationWatchdogTest.cpp)
target_include_directories(SchoolDaysMediaPreparationWatchdogTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_compile_features(SchoolDaysMediaPreparationWatchdogTest PRIVATE cxx_std_17)
add_test(NAME SchoolDays.MediaPreparationWatchdog
    COMMAND SchoolDaysMediaPreparationWatchdogTest)

add_executable(SchoolDaysMediaCancellationPoolTest
    SchoolDaysMediaCancellationPoolTest.cpp)
target_include_directories(SchoolDaysMediaCancellationPoolTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_compile_features(SchoolDaysMediaCancellationPoolTest PRIVATE cxx_std_17)
add_test(NAME SchoolDays.MediaCancellationPool
    COMMAND SchoolDaysMediaCancellationPoolTest)

# No retail data, GUI, or real saves: all SLog/ORS and files are synthetic,
# isolated under a fresh temporary --documents-like directory.
add_executable(SchoolDaysSaveProvenanceTest SchoolDaysSaveProvenanceTest.cpp)
target_link_libraries(SchoolDaysSaveProvenanceTest PRIVATE school_days)
add_test(NAME SchoolDays.SaveProvenance
    COMMAND SchoolDaysSaveProvenanceTest "${SCHOOL_DAYS_ROUTE}")

# Synthetic GlobalFlag durability, recovery and fail-injection tests; never
# reads commercial packs or personal saves.
add_executable(SchoolDaysGlobalFlagAtomicTest
    SchoolDaysGlobalFlagAtomicTest.cpp)
target_link_libraries(SchoolDaysGlobalFlagAtomicTest PRIVATE school_days)
add_test(NAME SchoolDays.GlobalFlagAtomic COMMAND SchoolDaysGlobalFlagAtomicTest)

# Complete synthetic end-count boundary with an isolated FlgH restart; depends
# on the normal synthetic route fixture, never on retail files or saves.
add_executable(SchoolDaysAllClearSyntheticTest SchoolDaysAllClearSyntheticTest.cpp)
target_link_libraries(SchoolDaysAllClearSyntheticTest PRIVATE school_days)
add_test(NAME SchoolDays.AllClearSynthetic COMMAND SchoolDaysAllClearSyntheticTest
    "${SCHOOL_DAYS_ROUTE}" "${CMAKE_CURRENT_BINARY_DIR}/fixtures/Script/English")
set_tests_properties(SchoolDays.AllClearSynthetic PROPERTIES
    FIXTURES_REQUIRED school_days_assets)

# Pure, private CMAP regression: clicking the currently selected Save/Load tab
# must not clear the occupancy latch and disable an otherwise valid LOAD row.
add_executable(SchoolDaysSaveLoadSamePageTest SchoolDaysSaveLoadSamePageTest.cpp)
target_link_libraries(SchoolDaysSaveLoadSamePageTest PRIVATE school_days)
add_test(NAME SchoolDays.SaveLoadSamePage COMMAND SchoolDaysSaveLoadSamePageTest)

add_executable(SchoolDaysGpkStreamingTest SchoolDaysGpkStreamingTest.cpp)
target_link_libraries(SchoolDaysGpkStreamingTest PRIVATE school_days ZLIB::ZLIB)
add_test(NAME SchoolDaysGpkStreamingTest COMMAND SchoolDaysGpkStreamingTest)

add_executable(SchoolDaysTitleMovieClockTest SchoolDaysTitleMovieClockTest.cpp)
target_link_libraries(SchoolDaysTitleMovieClockTest PRIVATE school_days)
add_test(NAME SchoolDaysTitleMovieClockTest COMMAND SchoolDaysTitleMovieClockTest)

add_executable(SchoolDaysEpisodeOverlayTest SchoolDaysEpisodeOverlayTest.cpp)
target_link_libraries(SchoolDaysEpisodeOverlayTest PRIVATE school_days)
add_test(NAME SchoolDaysEpisodeOverlayTest COMMAND SchoolDaysEpisodeOverlayTest)

add_executable(SchoolDaysMenuBarRuntimeTest SchoolDaysMenuBarRuntimeTest.cpp)
target_link_libraries(SchoolDaysMenuBarRuntimeTest PRIVATE school_days)
add_test(NAME SchoolDaysMenuBarRuntimeTest COMMAND SchoolDaysMenuBarRuntimeTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set_tests_properties(SchoolDaysMenuBarRuntimeTest PROPERTIES
    FIXTURES_REQUIRED school_days_assets
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

foreach(_school_days_policy_test IN ITEMS
        SchoolDaysChoiceTextPlanTest
        SchoolDaysDisplayPolicyTest
        SchoolDaysConfigMenuDisplayTest
        SchoolDaysExitTransitionTest
        SchoolDaysFrontendTransitionTest
        SchoolDaysInputPolicyTest
        SchoolDaysMenuBarLayoutTest
        SchoolDaysPromptPolicyTest
        SchoolDaysSystemPanelTransitionTest
        SchoolDaysTopMeterModelTest)
    add_executable(${_school_days_policy_test} ${_school_days_policy_test}.cpp)
    target_link_libraries(${_school_days_policy_test} PRIVATE school_days)
    add_test(NAME ${_school_days_policy_test} COMMAND ${_school_days_policy_test})
endforeach()

add_executable(SchoolDaysNextChoiceRuntimeTest SchoolDaysNextChoiceRuntimeTest.cpp)
target_link_libraries(SchoolDaysNextChoiceRuntimeTest PRIVATE school_days)
add_test(NAME SchoolDaysNextChoiceRuntimeTest COMMAND SchoolDaysNextChoiceRuntimeTest
    "${SCHOOL_DAYS_ROUTE}")
set_tests_properties(SchoolDaysNextChoiceRuntimeTest PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

add_executable(SchoolDaysBackRuntimeTest SchoolDaysBackRuntimeTest.cpp)
target_link_libraries(SchoolDaysBackRuntimeTest PRIVATE school_days)
kotonoha_copy_assets(SchoolDaysBackRuntimeTest)
add_test(NAME SchoolDaysBackRuntimeTest COMMAND SchoolDaysBackRuntimeTest
    "${SCHOOL_DAYS_ROUTE}")
set_tests_properties(SchoolDaysBackRuntimeTest PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
add_test(NAME SchoolDays.BackLogFallback COMMAND SchoolDaysBackRuntimeTest
    "${SCHOOL_DAYS_ROUTE}" "--fallback-only")
set_tests_properties(SchoolDays.BackLogFallback PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

add_executable(SchoolDaysConfigStorageTest SchoolDaysConfigStorageTest.cpp)
target_link_libraries(SchoolDaysConfigStorageTest PRIVATE school_days)
add_test(NAME SchoolDaysConfigStorageTest COMMAND SchoolDaysConfigStorageTest)

# This test inspects Engine::GetDebug(), removed from public DIST builds.
# It remains fully compiled and registered in DEV/non-DIST builds.
if(NOT KOTONOHA_DISTRIBUTION_BUILD)
add_executable(SchoolDaysRuntimeConfigStorageTest SchoolDaysRuntimeConfigStorageTest.cpp)
target_link_libraries(SchoolDaysRuntimeConfigStorageTest PRIVATE school_days)
add_test(NAME SchoolDaysRuntimeConfigStorageTest COMMAND SchoolDaysRuntimeConfigStorageTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set_tests_properties(SchoolDaysRuntimeConfigStorageTest PROPERTIES
    FIXTURES_REQUIRED school_days_assets
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()

add_executable(SchoolDaysHandoffRuntimeTest SchoolDaysHandoffRuntimeTest.cpp)
target_link_libraries(SchoolDaysHandoffRuntimeTest PRIVATE school_days)
add_test(NAME SchoolDaysHandoffRuntimeTest COMMAND SchoolDaysHandoffRuntimeTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set_tests_properties(SchoolDaysHandoffRuntimeTest PROPERTIES
    FIXTURES_REQUIRED school_days_assets
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

add_executable(SchoolDaysReplayScriptOracleTest SchoolDaysReplayScriptOracleTest.cpp)
target_link_libraries(SchoolDaysReplayScriptOracleTest PRIVATE school_days)
add_test(NAME SchoolDaysReplayScriptOracleTest COMMAND SchoolDaysReplayScriptOracleTest)

add_executable(SchoolDaysReplayPlaybackSessionTest SchoolDaysReplayPlaybackSessionTest.cpp)
target_link_libraries(SchoolDaysReplayPlaybackSessionTest PRIVATE school_days)
add_test(NAME SchoolDaysReplayPlaybackSessionTest COMMAND SchoolDaysReplayPlaybackSessionTest)

add_executable(SchoolDaysPlayDataSessionTest SchoolDaysPlayDataSessionTest.cpp)
target_link_libraries(SchoolDaysPlayDataSessionTest PRIVATE school_days)
add_test(NAME SchoolDaysPlayDataSessionTest COMMAND SchoolDaysPlayDataSessionTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures/Script/English")
set_tests_properties(SchoolDaysPlayDataSessionTest PROPERTIES
    FIXTURES_REQUIRED school_days_assets)

add_executable(SchoolDaysReplayRuntimeIsolationTest SchoolDaysReplayRuntimeIsolationTest.cpp)
target_link_libraries(SchoolDaysReplayRuntimeIsolationTest PRIVATE school_days)
add_test(NAME SchoolDaysReplayRuntimeIsolationTest COMMAND SchoolDaysReplayRuntimeIsolationTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set_tests_properties(SchoolDaysReplayRuntimeIsolationTest PROPERTIES
    FIXTURES_REQUIRED school_days_assets)

add_executable(SchoolDaysReplayRuntimeUiTest SchoolDaysReplayRuntimeUiTest.cpp)
target_link_libraries(SchoolDaysReplayRuntimeUiTest PRIVATE school_days)
add_test(NAME SchoolDaysReplayRuntimeUiTest COMMAND SchoolDaysReplayRuntimeUiTest
    "${SCHOOL_DAYS_ROUTE}")
set_tests_properties(SchoolDaysReplayRuntimeUiTest PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

if(KOTONOHA_DISTRIBUTION_BUILD)
    # Public build: exercise the real headless game loop, without scene-jump
    # or KTRF export options that correctly do not exist in this variant.
    # DEV keeps the stronger original route/export assertions below.
    set(_school_days_runtime_smoke_args
        --no-config --no-launcher --renderer software --frames 4)
else()
    set(_school_days_runtime_smoke_args
        --no-config --renderer software --frames 4 --node 0
        --dump-ktrf "${CMAKE_CURRENT_BINARY_DIR}/smoke.ktnroute")
endif()
add_test(NAME SchoolDaysRuntimeSmoke COMMAND SchoolDays
    --school-days "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
    --documents "${CMAKE_CURRENT_BINARY_DIR}/documents"
    ${_school_days_runtime_smoke_args})
unset(_school_days_runtime_smoke_args)
set_tests_properties(SchoolDaysRuntimeSmoke PROPERTIES
    FIXTURES_REQUIRED school_days_assets
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

add_executable(SchoolDaysLauncherTest SchoolDaysLauncherTest.cpp)
target_link_libraries(SchoolDaysLauncherTest PRIVATE school_days)
add_dependencies(SchoolDaysLauncherTest SchoolDays SchoolDaysPortTest)
add_test(NAME SchoolDaysLauncherTest COMMAND SchoolDaysLauncherTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
    "$<TARGET_FILE:SchoolDays>")
set_tests_properties(SchoolDaysLauncherTest PROPERTIES
    FIXTURES_REQUIRED school_days_assets
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

# The media QA test uses Engine::GetDebug() and debugJumpToNode().
if(NOT KOTONOHA_DISTRIBUTION_BUILD)
add_executable(SchoolDaysMediaTest SchoolDaysMediaTest.cpp)
target_link_libraries(SchoolDaysMediaTest PRIVATE school_days)
kotonoha_copy_assets(SchoolDaysMediaTest)
add_test(NAME SchoolDaysMediaTest COMMAND SchoolDaysMediaTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    "${CMAKE_CURRENT_BINARY_DIR}/media-documents")
set_tests_properties(SchoolDaysMediaTest PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()

add_executable(SchoolDaysUiModelTest SchoolDaysUiModelTest.cpp)
target_link_libraries(SchoolDaysUiModelTest PRIVATE school_days)
add_test(NAME SchoolDaysUiModelTest COMMAND SchoolDaysUiModelTest)

add_executable(SchoolDaysSystemUiCommentEditorTest SchoolDaysSystemUiCommentEditorTest.cpp)
target_link_libraries(SchoolDaysSystemUiCommentEditorTest PRIVATE school_days)
add_test(NAME SchoolDaysSystemUiCommentEditorTest COMMAND SchoolDaysSystemUiCommentEditorTest)
set_tests_properties(SchoolDaysSystemUiCommentEditorTest PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

# On Windows the test executables are emitted in a directory separate from the
# shared Kotonoha runtime.  Make CTest resolve the same runtime DLLs that the
# packaged application receives instead of blocking on an invisible loader
# error dialog when a test is launched directly by CTest.
if(WIN32)
    set(_school_days_test_runtime_env
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
    set(_school_days_dev_only_ctests)
    if(NOT KOTONOHA_DISTRIBUTION_BUILD)
        list(APPEND _school_days_dev_only_ctests
            SchoolDaysRuntimeConfigStorageTest SchoolDaysMediaTest)
    endif()
    set_tests_properties(
        SchoolDaysPortTest
        SchoolDaysSessionTest
        SchoolDaysBridgeTest
        SchoolDaysSceneTimelineTest
        SchoolDaysAudioEventStateTest
        SchoolDaysAssetsListTest
        SchoolDaysLxhFadeTest
        SchoolDaysLxhContentTest
        SchoolDaysGpkStreamingTest
        SchoolDaysTitleMovieClockTest
        SchoolDaysEpisodeOverlayTest
        SchoolDays.DisplayNarrativeMarkup
        SchoolDays.SubtitleSpeakerStyle
        SchoolDaysMenuBarRuntimeTest
        SchoolDaysChoiceTextPlanTest
        SchoolDaysDisplayPolicyTest
        SchoolDaysConfigMenuDisplayTest
        SchoolDaysExitTransitionTest
        SchoolDaysFrontendTransitionTest
        SchoolDaysInputPolicyTest
        SchoolDaysMenuBarLayoutTest
        SchoolDaysPromptPolicyTest
        SchoolDaysTopMeterModelTest
        SchoolDaysNextChoiceRuntimeTest
        SchoolDaysBackRuntimeTest
        SchoolDaysConfigStorageTest
        SchoolDaysHandoffRuntimeTest
        SchoolDaysReplayScriptOracleTest
        SchoolDaysReplayPlaybackSessionTest
        SchoolDaysPlayDataSessionTest
        SchoolDaysReplayRuntimeIsolationTest
        SchoolDaysReplayRuntimeUiTest
        SchoolDays.GlobalFlagAtomic
        SchoolDays.AllClearSynthetic
        SchoolDaysRuntimeSmoke
        SchoolDaysLauncherTest
        ${_school_days_dev_only_ctests}
        SchoolDaysUiModelTest
        SchoolDaysSystemUiCommentEditorTest
        PROPERTIES ENVIRONMENT_MODIFICATION "${_school_days_test_runtime_env}")
endif()

# Manual visual check with retail data (see SchoolDaysPanelCapture.cpp).
# Its debugJumpToNode entry point cannot exist in DIST; never ship this tool.
if(NOT KOTONOHA_DISTRIBUTION_BUILD)
add_executable(SchoolDaysPanelCapture SchoolDaysPanelCapture.cpp)
target_link_libraries(SchoolDaysPanelCapture PRIVATE school_days)
kotonoha_copy_assets(SchoolDaysPanelCapture)
endif()

set(SCHOOL_DAYS_DATA_DIR "" CACHE PATH "User-supplied School Days HQ installation")
if(SCHOOL_DAYS_DATA_DIR)
    add_executable(SchoolDaysExitRuntimeTest SchoolDaysExitRuntimeTest.cpp)
    target_link_libraries(SchoolDaysExitRuntimeTest PRIVATE school_days)
    kotonoha_copy_assets(SchoolDaysExitRuntimeTest)
    add_test(NAME SchoolDays.ExitRuntime COMMAND SchoolDaysExitRuntimeTest
        "${SCHOOL_DAYS_DATA_DIR}" "${SCHOOL_DAYS_ROUTE}")
    set_tests_properties(SchoolDays.ExitRuntime PROPERTIES TIMEOUT 180
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    if(WIN32)
        set_property(TEST SchoolDays.ExitRuntime APPEND PROPERTY
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
    endif()
    add_executable(SchoolDaysSaveTest SchoolDaysSaveTest.cpp)
    target_link_libraries(SchoolDaysSaveTest PRIVATE school_days)
    kotonoha_copy_assets(SchoolDaysSaveTest)
    add_test(NAME SchoolDays.Save COMMAND SchoolDaysSaveTest
      "${SCHOOL_DAYS_DATA_DIR}" "${SCHOOL_DAYS_ROUTE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../display-options")
    set_tests_properties(SchoolDays.Save PROPERTIES TIMEOUT 90
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    if(WIN32)
      set_property(TEST SchoolDays.Save APPEND PROPERTY ENVIRONMENT_MODIFICATION
          "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
    endif()
    add_executable(SchoolDaysRetailTest SchoolDaysRetailTest.cpp)
    target_link_libraries(SchoolDaysRetailTest PRIVATE school_days)
    add_test(NAME SchoolDays.Retail COMMAND SchoolDaysRetailTest "${SCHOOL_DAYS_DATA_DIR}" "${SCHOOL_DAYS_ROUTE}")
    set_tests_properties(SchoolDays.Retail PROPERTIES TIMEOUT 180)
    if(WIN32)
        set_tests_properties(SchoolDays.Retail PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
    endif()
endif()

add_executable(SchoolDaysLogoTest SchoolDaysLogoTest.cpp)
target_link_libraries(SchoolDaysLogoTest PRIVATE school_days)
add_test(NAME SchoolDays.Logo COMMAND SchoolDaysLogoTest "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set_tests_properties(SchoolDays.Logo PROPERTIES FIXTURES_REQUIRED school_days_assets
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
if(WIN32)
  set_property(TEST SchoolDays.Logo APPEND PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
endif()

if(NOT KOTONOHA_DISTRIBUTION_BUILD)
    set_property(TEST SchoolDaysMediaTest APPEND PROPERTY FAIL_REGULAR_EXPRESSION
        "Could not update timestamps for discarded samples" "Extra data: 8 bits left, value: 0")
endif()
if(SCHOOL_DAYS_DATA_DIR)
    set_property(TEST SchoolDays.Save APPEND PROPERTY FAIL_REGULAR_EXPRESSION
        "Could not update timestamps for discarded samples" "Extra data: 8 bits left, value: 0")
endif()

# Retail game-assets route and fixture-dependent cases are not run in CI.
kotonoha_label_directory_tests(REAL
    SchoolDaysPortTest
    SchoolDaysSessionTest
    SchoolDaysBridgeTest
    SchoolDaysMenuBarRuntimeTest
    SchoolDaysNextChoiceRuntimeTest
    SchoolDaysBackRuntimeTest
    SchoolDaysRuntimeConfigStorageTest
    SchoolDaysHandoffRuntimeTest
    SchoolDaysPlayDataSessionTest
    SchoolDaysReplayRuntimeIsolationTest
    SchoolDaysReplayRuntimeUiTest
    SchoolDaysRuntimeSmoke
    SchoolDaysLauncherTest
    SchoolDaysMediaTest
    SchoolDays.Logo
    SchoolDays.ExitRuntime
    SchoolDays.Save
    SchoolDays.Retail
)
