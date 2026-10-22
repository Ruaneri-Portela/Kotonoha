cmake_minimum_required(VERSION 3.18)
project(KtrfSchoolDaysSessionGate3 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(KOTONOHA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include_directories("${KOTONOHA_ROOT}/src/routing")
include_directories("${KOTONOHA_ROOT}/games/school-days/include")

add_executable(KtrfSchoolDaysSessionTest
    KtrfSchoolDaysSessionTest.cpp
    "${KOTONOHA_ROOT}/src/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/parsers/KtrfTyped.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRuntime.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRouter.c"
    "${KOTONOHA_ROOT}/games/school-days/src/routing/SchoolDaysKtrfAdapter.c"
    "${KOTONOHA_ROOT}/games/school-days/src/routing/SchoolDaysKtrfAssetResolver.c"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysStateStore.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysHistory.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysPersistenceCodec.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysPersistence.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysBackRules.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysKtrfSession.cpp"
)

target_include_directories(KtrfSchoolDaysSessionTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

if(MSVC)
    target_compile_options(KtrfSchoolDaysSessionTest PRIVATE /W4 /permissive-)
else()
    target_compile_options(KtrfSchoolDaysSessionTest PRIVATE -Wall -Wextra -Wpedantic)
endif()
