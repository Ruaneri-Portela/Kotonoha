set(SCHOOL_DAYS_ROUTE "${CMAKE_CURRENT_SOURCE_DIR}/../game-assets/school-days-hq.ktnroute")

add_executable(SchoolDaysPortTest SchoolDaysPortTest.cpp)
target_link_libraries(SchoolDaysPortTest PRIVATE school_days)
add_test(NAME SchoolDaysPortTest COMMAND SchoolDaysPortTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set_tests_properties(SchoolDaysPortTest PROPERTIES FIXTURES_SETUP school_days_assets)

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
target_link_libraries(SchoolDaysSceneTimelineTest PRIVATE school_days)
add_test(NAME SchoolDaysSceneTimelineTest COMMAND SchoolDaysSceneTimelineTest)

add_executable(SchoolDaysAudioEventStateTest SchoolDaysAudioEventStateTest.cpp)
target_link_libraries(SchoolDaysAudioEventStateTest PRIVATE school_days)
add_test(NAME SchoolDaysAudioEventStateTest COMMAND SchoolDaysAudioEventStateTest)

add_executable(SchoolDaysAssetsListTest SchoolDaysAssetsListTest.cpp)
target_link_libraries(SchoolDaysAssetsListTest PRIVATE school_days)
add_test(NAME SchoolDaysAssetsListTest COMMAND SchoolDaysAssetsListTest)

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

add_executable(SchoolDaysConfigStorageTest SchoolDaysConfigStorageTest.cpp)
target_link_libraries(SchoolDaysConfigStorageTest PRIVATE school_days)
add_test(NAME SchoolDaysConfigStorageTest COMMAND SchoolDaysConfigStorageTest)

add_executable(SchoolDaysRuntimeConfigStorageTest SchoolDaysRuntimeConfigStorageTest.cpp)
target_link_libraries(SchoolDaysRuntimeConfigStorageTest PRIVATE school_days)
add_test(NAME SchoolDaysRuntimeConfigStorageTest COMMAND SchoolDaysRuntimeConfigStorageTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set_tests_properties(SchoolDaysRuntimeConfigStorageTest PROPERTIES
    FIXTURES_REQUIRED school_days_assets
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

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

add_test(NAME SchoolDaysRuntimeSmoke COMMAND SchoolDays
    --school-days "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
    --documents "${CMAKE_CURRENT_BINARY_DIR}/documents"
    --no-config --renderer software --frames 4 --node 0
    --dump-ktrf "${CMAKE_CURRENT_BINARY_DIR}/smoke.ktnroute")
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

add_executable(SchoolDaysMediaTest SchoolDaysMediaTest.cpp)
target_link_libraries(SchoolDaysMediaTest PRIVATE school_days)
kotonoha_copy_assets(SchoolDaysMediaTest)
add_test(NAME SchoolDaysMediaTest COMMAND SchoolDaysMediaTest
    "${SCHOOL_DAYS_ROUTE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    "${CMAKE_CURRENT_BINARY_DIR}/media-documents")
set_tests_properties(SchoolDaysMediaTest PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

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
    set_tests_properties(
        SchoolDaysPortTest
        SchoolDaysSessionTest
        SchoolDaysBridgeTest
        SchoolDaysSceneTimelineTest
        SchoolDaysAudioEventStateTest
        SchoolDaysAssetsListTest
        SchoolDaysGpkStreamingTest
        SchoolDaysTitleMovieClockTest
        SchoolDaysEpisodeOverlayTest
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
        SchoolDaysRuntimeConfigStorageTest
        SchoolDaysHandoffRuntimeTest
        SchoolDaysReplayScriptOracleTest
        SchoolDaysReplayPlaybackSessionTest
        SchoolDaysPlayDataSessionTest
        SchoolDaysReplayRuntimeIsolationTest
        SchoolDaysReplayRuntimeUiTest
        SchoolDaysRuntimeSmoke
        SchoolDaysLauncherTest
        SchoolDaysMediaTest
        SchoolDaysUiModelTest
        SchoolDaysSystemUiCommentEditorTest
        PROPERTIES ENVIRONMENT_MODIFICATION "${_school_days_test_runtime_env}")
endif()

# Manual visual check with retail data (see SchoolDaysPanelCapture.cpp).
add_executable(SchoolDaysPanelCapture SchoolDaysPanelCapture.cpp)
target_link_libraries(SchoolDaysPanelCapture PRIVATE school_days)
kotonoha_copy_assets(SchoolDaysPanelCapture)

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

# Parallel CTest may launch several independent School Days Runtime hosts.
# DevBridge normally listens on 127.0.0.1:47321, so without a per-test port
# these unrelated tests can fail Engine construction with a socket bind error.
# An exception after GameScene construction also used to expose an SDL_ttf
# teardown lifetime bug. Keep the normal DEV default for interactive sessions;
# isolate test processes (including the LauncherTest's child SchoolDays.exe)
# without serializing otherwise independent CTest cases.
if(WIN32 AND KOTONOHA_DEV_CONTROL)
  set(_school_days_dev_port 47520)
  foreach(_school_days_runtime_test IN ITEMS
      SchoolDaysPortTest
      SchoolDaysMenuBarRuntimeTest
      SchoolDaysNextChoiceRuntimeTest
      SchoolDaysBackRuntimeTest
      SchoolDaysRuntimeConfigStorageTest
      SchoolDaysHandoffRuntimeTest
      SchoolDaysReplayRuntimeIsolationTest
      SchoolDaysReplayRuntimeUiTest
      SchoolDaysRuntimeSmoke
      SchoolDaysLauncherTest
      SchoolDaysMediaTest
      SchoolDays.Save
      SchoolDays.ExitRuntime)
    if(TEST ${_school_days_runtime_test})
      set_property(TEST ${_school_days_runtime_test} APPEND PROPERTY ENVIRONMENT
          "KOTONOHA_DEV_PORT=${_school_days_dev_port}")
      math(EXPR _school_days_dev_port "${_school_days_dev_port} + 1")
    endif()
  endforeach()
endif()

set_property(TEST SchoolDaysMediaTest APPEND PROPERTY FAIL_REGULAR_EXPRESSION
    "Could not update timestamps for discarded samples" "Extra data: 8 bits left, value: 0")
if(SCHOOL_DAYS_DATA_DIR)
    set_property(TEST SchoolDays.Save APPEND PROPERTY FAIL_REGULAR_EXPRESSION
        "Could not update timestamps for discarded samples" "Extra data: 8 bits left, value: 0")
endif()
