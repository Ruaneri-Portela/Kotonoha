cmake_minimum_required(VERSION 3.18)
project(KtrfSchoolDaysSessionGate3 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(KOTONOHA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_executable(KtrfSchoolDaysSessionTest
    KtrfSchoolDaysSessionTest.cpp
    "${KOTONOHA_ROOT}/src/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/parsers/KtrfTyped.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRuntime.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRouter.c"
    "${KOTONOHA_ROOT}/src/routing/SchoolDaysKtrfAdapter.c"
    "${KOTONOHA_ROOT}/src/routing/SchoolDaysKtrfAssetResolver.c"
    "${KOTONOHA_ROOT}/src/SchoolDaysKtrfSession.cpp"
)

target_include_directories(KtrfSchoolDaysSessionTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

if(MSVC)
    target_compile_options(KtrfSchoolDaysSessionTest PRIVATE /W4 /permissive-)
else()
    target_compile_options(KtrfSchoolDaysSessionTest PRIVATE -Wall -Wextra -Wpedantic)
endif()
