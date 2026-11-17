cmake_minimum_required(VERSION 3.16)
project(SchoolDaysAtomicStandalone LANGUAGES CXX)

# This subproject deliberately does NOT add the Kotonoha engine, SDL or vcpkg.
add_executable(SchoolDaysAtomicStandaloneTest
  SchoolDaysAtomicStandaloneTest.cpp
  ../../src/SchoolDaysAtomicFile.cpp)
target_include_directories(SchoolDaysAtomicStandaloneTest PRIVATE ../../include)
target_compile_features(SchoolDaysAtomicStandaloneTest PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(SchoolDaysAtomicStandaloneTest PRIVATE /W4 /EHsc)
else()
  target_compile_options(SchoolDaysAtomicStandaloneTest PRIVATE -Wall -Wextra -Wpedantic)
endif()
enable_testing()
add_test(NAME SchoolDaysAtomicStandaloneTest COMMAND SchoolDaysAtomicStandaloneTest)
