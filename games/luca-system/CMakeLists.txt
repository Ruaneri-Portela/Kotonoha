cmake_minimum_required(VERSION 3.16)

project(LucaSystem VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(CTest)
endif()

# LucaSystem (Visual Art's / Key) archive + script format support.
# Implemented from the format decoded by wetor/LuckSystem (pak.go / script.go).
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/pak"
    "${CMAKE_CURRENT_BINARY_DIR}/providers/pak")
add_library(luca_system_formats STATIC
  src/LucaSystemScript.cpp
  src/LucaSystemRuntime.cpp
)
target_include_directories(luca_system_formats PUBLIC src include)
target_link_libraries(luca_system_formats PUBLIC KotonohaEngine kotonoha_provider_pak)
target_compile_features(luca_system_formats PUBLIC cxx_std_17)

if(KOTONOHA_BUILD_HOST_TOOLS)
    add_executable(LucaInspect tools/LucaInspect.cpp)
    target_link_libraries(LucaInspect PRIVATE luca_system_formats)
endif()

kotonoha_add_app(LucaSystem src/main.cpp)
target_link_libraries(LucaSystem PRIVATE luca_system_formats)
set_target_properties(LucaSystem PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(LucaSystem
    BUNDLE_ID me.hirameki.kotonoha.lucasystem GAME luca-system)

set(LUCA_SYSTEM_TEST_DATA "" CACHE PATH "LucaSystem original data for integration tests")

if(BUILD_TESTING)
  add_executable(LucaSystemFormatTests tests/LucaSystemTest.cpp)
  target_link_libraries(LucaSystemFormatTests PRIVATE luca_system_formats)
  add_test(NAME LucaSystemFormatTests COMMAND LucaSystemFormatTests)

  # Dependency-free C++17 test: no SDL, vcpkg, or retail AIR data required.
  add_executable(LucaSystemSaveCodecTests tests/SaveStateTest.cpp)
  target_include_directories(LucaSystemSaveCodecTests PRIVATE include)
  target_compile_features(LucaSystemSaveCodecTests PRIVATE cxx_std_17)
  add_test(NAME LucaSystem.SaveCodec COMMAND LucaSystemSaveCodecTests)

  # SDL-free scene staging and transition-clock regressions.
  add_executable(LucaSystemSceneLayersTests tests/SceneLayersTest.cpp)
  target_include_directories(LucaSystemSceneLayersTests PRIVATE include)
  target_compile_features(LucaSystemSceneLayersTests PRIVATE cxx_std_17)
  add_test(NAME LucaSystem.SceneLayers COMMAND LucaSystemSceneLayersTests)

  add_executable(LucaSystemRetailAirFlowTests tests/RetailAirFlowTest.cpp)
  target_link_libraries(LucaSystemRetailAirFlowTests PRIVATE luca_system_formats)
  if(LUCA_SYSTEM_TEST_DATA AND EXISTS "${LUCA_SYSTEM_TEST_DATA}")
    add_test(NAME LucaSystem.RetailAirFlow
        COMMAND LucaSystemRetailAirFlowTests "${LUCA_SYSTEM_TEST_DATA}")
    set_tests_properties(LucaSystem.RetailAirFlow PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        TIMEOUT 120)
  endif()
endif()

if(BUILD_TESTING)
    kotonoha_label_directory_tests(REAL LucaSystem.RetailAirFlow)
endif()
