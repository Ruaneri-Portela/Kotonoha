cmake_minimum_required(VERSION 3.18)
project(KtrfSchoolDaysPersistenceGate6 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(KOTONOHA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include_directories("${KOTONOHA_ROOT}/src/Kotonoha/routing")
include_directories("${KOTONOHA_ROOT}/games/school-days/include")

add_executable(KtrfSchoolDaysPersistenceCodecTest
    KtrfSchoolDaysPersistenceCodecTest.cpp
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysPersistenceCodec.cpp"
)

add_executable(KtrfSchoolDaysPersistenceRuntimeTest
    KtrfSchoolDaysPersistenceRuntimeTest.cpp
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
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveProfile.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysSaveService.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysEndBackgroundResolver.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysBackRules.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysKtrfSession.cpp"
    "${KOTONOHA_ROOT}/games/school-days/tests/ktrf_common/ScriptDirectoryStub.cpp"
    "${KOTONOHA_ROOT}/games/school-days/src/SchoolDaysKtrfGameplayBridge.cpp"
)

target_include_directories(KtrfSchoolDaysPersistenceCodecTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(KtrfSchoolDaysPersistenceRuntimeTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

if(MSVC)
    target_compile_options(KtrfSchoolDaysPersistenceCodecTest PRIVATE /W4 /permissive-)
    target_compile_options(KtrfSchoolDaysPersistenceRuntimeTest PRIVATE /W4 /permissive-)
else()
    target_compile_options(KtrfSchoolDaysPersistenceCodecTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(KtrfSchoolDaysPersistenceRuntimeTest PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()
