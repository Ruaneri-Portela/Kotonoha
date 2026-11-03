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

set(SCHOOL_DAYS_DATA_DIR "" CACHE PATH "User-supplied School Days HQ installation")
if(SCHOOL_DAYS_DATA_DIR)
    add_executable(SchoolDaysSaveTest SchoolDaysSaveTest.cpp)
    target_link_libraries(SchoolDaysSaveTest PRIVATE school_days)
    kotonoha_copy_assets(SchoolDaysSaveTest)
    add_test(NAME SchoolDays.Save COMMAND SchoolDaysSaveTest "${SCHOOL_DAYS_DATA_DIR}" "${SCHOOL_DAYS_ROUTE}")
    set_tests_properties(SchoolDays.Save PROPERTIES TIMEOUT 90
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_executable(SchoolDaysRetailTest SchoolDaysRetailTest.cpp)
    target_link_libraries(SchoolDaysRetailTest PRIVATE school_days)
    add_test(NAME SchoolDays.Retail COMMAND SchoolDaysRetailTest "${SCHOOL_DAYS_DATA_DIR}" "${SCHOOL_DAYS_ROUTE}")
    set_tests_properties(SchoolDays.Retail PROPERTIES TIMEOUT 180)
endif()

add_executable(SchoolDaysLogoTest SchoolDaysLogoTest.cpp)
target_link_libraries(SchoolDaysLogoTest PRIVATE school_days)
add_test(NAME SchoolDays.Logo COMMAND SchoolDaysLogoTest "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
set_tests_properties(SchoolDays.Logo PROPERTIES FIXTURES_REQUIRED school_days_assets
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

set_property(TEST SchoolDaysMediaTest APPEND PROPERTY FAIL_REGULAR_EXPRESSION
    "Could not update timestamps for discarded samples" "Extra data: 8 bits left, value: 0")
if(SCHOOL_DAYS_DATA_DIR)
    set_property(TEST SchoolDays.Save APPEND PROPERTY FAIL_REGULAR_EXPRESSION
        "Could not update timestamps for discarded samples" "Extra data: 8 bits left, value: 0")
endif()
