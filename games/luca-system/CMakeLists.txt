cmake_minimum_required(VERSION 3.16)

project(LucaSystem VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(CTest)
endif()

if(NOT TARGET Kotonoha::Engine)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
      "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

# LucaSystem (Visual Art's / Key) archive + script format support.
# Implemented from the format decoded by wetor/LuckSystem (pak.go / script.go).
add_library(luca_system_formats STATIC
  src/CzImage.cpp
  src/LucaSystemAssets.cpp
  src/LucaSystemScript.cpp
  src/LucaSystemRuntime.cpp
)
target_include_directories(luca_system_formats PUBLIC src include)
target_link_libraries(luca_system_formats PUBLIC Kotonoha::Engine)
target_compile_features(luca_system_formats PUBLIC cxx_std_17)

add_executable(LucaInspect tools/LucaInspect.cpp)
target_link_libraries(LucaInspect PRIVATE luca_system_formats)

kotonoha_add_app(LucaSystem src/main.cpp)
target_link_libraries(LucaSystem PRIVATE luca_system_formats)
kotonoha_configure_app(LucaSystem
    BUNDLE_ID me.hirameki.kotonoha.luca_system GAME luca-system)
set_target_properties(LucaSystem PROPERTIES CXX_EXTENSIONS OFF)

set(LUCA_SYSTEM_TEST_DATA "" CACHE PATH "LucaSystem original data for integration tests")

if(BUILD_TESTING)
  add_executable(LucaSystemFormatTests tests/LucaSystemTest.cpp)
  target_link_libraries(LucaSystemFormatTests PRIVATE luca_system_formats)
  add_test(NAME LucaSystemFormatTests COMMAND LucaSystemFormatTests)
endif()
