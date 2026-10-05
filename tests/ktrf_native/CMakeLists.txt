cmake_minimum_required(VERSION 3.18)
project(KtrfNativeReaderTest LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(KOTONOHA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_executable(KtrfNativeReaderTest
    KtrfNativeReaderTest.cpp
    "${KOTONOHA_ROOT}/src/parsers/Ktrf.c"
)

add_executable(KtrfNativeTypedReaderTest
    KtrfNativeTypedReaderTest.cpp
    "${KOTONOHA_ROOT}/src/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/parsers/KtrfTyped.c"
)

add_executable(KtrfRoutingRuntimeTest
    KtrfRoutingRuntimeTest.cpp
    "${KOTONOHA_ROOT}/src/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/parsers/KtrfTyped.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRuntime.c"
)

add_executable(KtrfRoutingRuntimeGate2Test
    KtrfRoutingRuntimeGate2Test.cpp
    "${KOTONOHA_ROOT}/src/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/parsers/KtrfTyped.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRuntime.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRouter.c"
)

target_include_directories(KtrfNativeReaderTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(KtrfNativeTypedReaderTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(KtrfRoutingRuntimeTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_include_directories(KtrfRoutingRuntimeGate2Test PRIVATE
    "${KOTONOHA_ROOT}/include"
)

if(MSVC)
    target_compile_options(KtrfNativeReaderTest PRIVATE /W4 /permissive-)
    target_compile_options(KtrfNativeTypedReaderTest PRIVATE /W4 /permissive-)
    target_compile_options(KtrfRoutingRuntimeTest PRIVATE /W4 /permissive-)
    target_compile_options(KtrfRoutingRuntimeGate2Test PRIVATE /W4 /permissive-)
else()
    target_compile_options(KtrfNativeReaderTest PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(KtrfNativeTypedReaderTest PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(KtrfRoutingRuntimeTest PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(KtrfRoutingRuntimeGate2Test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(SchoolDaysOracleTrace
    "${KOTONOHA_ROOT}/tests/SchoolDaysOracleTrace.cpp"
    "${KOTONOHA_ROOT}/src/SchoolDaysRouter.cpp"
)
target_include_directories(SchoolDaysOracleTrace PRIVATE
    "${KOTONOHA_ROOT}/include"
)
target_compile_definitions(SchoolDaysOracleTrace PRIVATE
    KOTONOHA_ROUTER_TEST_ACCESS=1
)

add_executable(KtrfNativeRouterTrace
    KtrfNativeRouterTrace.cpp
    "${KOTONOHA_ROOT}/src/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/parsers/KtrfTyped.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRuntime.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRouter.c"
)
target_include_directories(KtrfNativeRouterTrace PRIVATE
    "${KOTONOHA_ROOT}/include"
)

if(MSVC)
    target_compile_options(SchoolDaysOracleTrace PRIVATE /W4 /permissive-)
    target_compile_options(KtrfNativeRouterTrace PRIVATE /W4 /permissive-)
else()
    target_compile_options(SchoolDaysOracleTrace PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(KtrfNativeRouterTrace PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(KtrfNativeStateMatrixTrace
    KtrfNativeStateMatrixTrace.cpp
    "${KOTONOHA_ROOT}/src/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/parsers/KtrfTyped.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRuntime.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRouter.c"
)
target_include_directories(KtrfNativeStateMatrixTrace PRIVATE
    "${KOTONOHA_ROOT}/include"
)

if(MSVC)
    target_compile_options(KtrfNativeStateMatrixTrace PRIVATE /W4 /permissive-)
else()
    target_compile_options(KtrfNativeStateMatrixTrace PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(KtrfSchoolDaysAdapterTest
    KtrfSchoolDaysAdapterTest.cpp
    "${KOTONOHA_ROOT}/src/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/parsers/KtrfTyped.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRuntime.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRouter.c"
    "${KOTONOHA_ROOT}/src/routing/SchoolDaysKtrfAdapter.c"
)
target_include_directories(KtrfSchoolDaysAdapterTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

if(MSVC)
    target_compile_options(KtrfSchoolDaysAdapterTest PRIVATE /W4 /permissive-)
else()
    target_compile_options(KtrfSchoolDaysAdapterTest PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(KtrfSchoolDaysAssetResolverTest
    KtrfSchoolDaysAssetResolverTest.cpp
    "${KOTONOHA_ROOT}/src/parsers/Ktrf.c"
    "${KOTONOHA_ROOT}/src/parsers/KtrfTyped.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRuntime.c"
    "${KOTONOHA_ROOT}/src/routing/KtrfRouter.c"
    "${KOTONOHA_ROOT}/src/routing/SchoolDaysKtrfAdapter.c"
    "${KOTONOHA_ROOT}/src/routing/SchoolDaysKtrfAssetResolver.c"
)
target_include_directories(KtrfSchoolDaysAssetResolverTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

if(MSVC)
    target_compile_options(KtrfSchoolDaysAssetResolverTest PRIVATE /W4 /permissive-)
else()
    target_compile_options(KtrfSchoolDaysAssetResolverTest PRIVATE -Wall -Wextra -Wpedantic)
endif()
