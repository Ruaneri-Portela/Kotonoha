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
