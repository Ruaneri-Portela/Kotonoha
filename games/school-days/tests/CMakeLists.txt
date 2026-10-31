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

# Manual visual check with retail data (see SchoolDaysPanelCapture.cpp).
add_executable(SchoolDaysPanelCapture SchoolDaysPanelCapture.cpp)
target_link_libraries(SchoolDaysPanelCapture PRIVATE school_days)
kotonoha_copy_assets(SchoolDaysPanelCapture)
