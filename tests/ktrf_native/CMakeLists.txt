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

target_include_directories(KtrfNativeReaderTest PRIVATE
    "${KOTONOHA_ROOT}/include"
)

if(MSVC)
    target_compile_options(KtrfNativeReaderTest PRIVATE /W4 /permissive-)
else()
    target_compile_options(KtrfNativeReaderTest PRIVATE -Wall -Wextra -Wpedantic)
endif()
