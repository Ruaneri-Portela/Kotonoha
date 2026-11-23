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
  src/DebugCatalog.cpp
  src/LucaSystemRuntime.cpp
)
target_include_directories(luca_system_formats PUBLIC src include)
target_link_libraries(luca_system_formats PUBLIC KotonohaEngine kotonoha_provider_pak)
target_compile_features(luca_system_formats PUBLIC cxx_std_17)

if(KOTONOHA_BUILD_HOST_TOOLS)
    add_executable(LucaInspect tools/LucaInspect.cpp)
    target_link_libraries(LucaInspect PRIVATE luca_system_formats)
    add_executable(LucaPak tools/LucaPak.cpp)
    target_link_libraries(LucaPak PRIVATE luca_system_formats)
endif()

kotonoha_add_app(LucaSystem src/main.cpp)
target_link_libraries(LucaSystem PRIVATE luca_system_formats)
set_target_properties(LucaSystem PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(LucaSystem
    BUNDLE_ID me.hirameki.kotonoha.lucasystem GAME luca-system)

set(LUCA_SYSTEM_TEST_DATA "" CACHE PATH "LucaSystem original data for integration tests")

if(BUILD_TESTING)
  # Standalone LuckSystem OPCODE.txt compatibility (no retail assets or SDL).
  add_executable(LucaSystemOpcodeTableTests tests/OpcodeTableTest.cpp)
  target_include_directories(LucaSystemOpcodeTableTests PRIVATE include)
  target_compile_features(LucaSystemOpcodeTableTests PRIVATE cxx_std_17)
  add_test(NAME LucaSystem.OpcodeTable COMMAND LucaSystemOpcodeTableTests)

  add_executable(LucaSystemFormatTests tests/LucaSystemTest.cpp)
  target_link_libraries(LucaSystemFormatTests PRIVATE luca_system_formats)
  add_test(NAME LucaSystemFormatTests COMMAND LucaSystemFormatTests)
  if(KOTONOHA_BUILD_HOST_TOOLS)
    add_test(NAME LucaSystem.FontOverlay
        COMMAND LucaSystemFormatTests --font-overlay)
    set_tests_properties(LucaSystem.FontOverlay PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
  endif()

  # Debug scenario/instruction catalog is independently testable without
  # SDL, vcpkg or commercial PAK files.
  add_executable(LucaSystemDebugCatalogTests
      tests/DebugCatalogTest.cpp src/DebugCatalog.cpp)
  target_include_directories(LucaSystemDebugCatalogTests PRIVATE include)
  target_compile_features(LucaSystemDebugCatalogTests PRIVATE cxx_std_17)
  add_test(NAME LucaSystem.DebugCatalog COMMAND LucaSystemDebugCatalogTests)

  # Dependency-free C++17 test: no SDL, vcpkg, or retail AIR data required.
  add_executable(LucaSystemSaveCodecTests tests/SaveStateTest.cpp)
  target_include_directories(LucaSystemSaveCodecTests PRIVATE include)
  target_compile_features(LucaSystemSaveCodecTests PRIVATE cxx_std_17)
  add_test(NAME LucaSystem.SaveCodec COMMAND LucaSystemSaveCodecTests)

  # Portable CZ3/CZ4 delta and RGB24 reference regressions; no SDL needed.
  add_executable(LucaSystemCzParityTests
      tests/CzImageParityTest.cpp
      ../../modules/formats/image/cz/src/CzImage.cpp)
  target_include_directories(LucaSystemCzParityTests PRIVATE
      ../../modules/formats/image/cz/include)
  target_compile_features(LucaSystemCzParityTests PRIVATE cxx_std_17)
  add_test(NAME LucaSystem.CzParity COMMAND LucaSystemCzParityTests)

  # SDL-backed pixel assertions for the actual GPU scene compositor.
  add_executable(LucaSystemGpuSceneTests tests/GpuSceneTest.cpp)
  target_include_directories(LucaSystemGpuSceneTests PRIVATE include)
  target_compile_features(LucaSystemGpuSceneTests PRIVATE cxx_std_17)
  target_link_libraries(LucaSystemGpuSceneTests PRIVATE SDL3::SDL3)
  add_test(NAME LucaSystem.GpuScene COMMAND LucaSystemGpuSceneTests)
  set_tests_properties(LucaSystem.GpuScene PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy"
      LABELS synthetic)

  # SDL-free scene staging and transition-clock regressions.
  add_executable(LucaSystemSceneLayersTests tests/SceneLayersTest.cpp)
  target_include_directories(LucaSystemSceneLayersTests PRIVATE include)
  target_compile_features(LucaSystemSceneLayersTests PRIVATE cxx_std_17)
  add_test(NAME LucaSystem.SceneLayers COMMAND LucaSystemSceneLayersTests)

  # seen170 sprite base: INIT(2,ff) followed by an expression crop must keep
  # the displayed portrait underneath. Retail mode is opt-in via env var.
  add_executable(LucaSystemSpriteBaseTests tests/AirSpriteBaseTest.cpp)
  target_link_libraries(LucaSystemSpriteBaseTests PRIVATE luca_system_formats)
  add_test(NAME LucaSystem.SpriteBase COMMAND LucaSystemSpriteBaseTests)
  # Registered unconditionally: without retail data the executable exits 77
  # (CTest Skipped) instead of silently passing an empty integration test.
  add_test(NAME LucaSystem.RetailSpriteBase
      COMMAND LucaSystemSpriteBaseTests --require-retail)
  set_tests_properties(LucaSystem.RetailSpriteBase PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
      WORKING_DIRECTORY "${KOTONOHA_PROJECT_ROOT}"
      TIMEOUT 120
      SKIP_RETURN_CODE 77)

  add_executable(LucaSystemRetailAirFlowTests tests/RetailAirFlowTest.cpp)
  target_link_libraries(LucaSystemRetailAirFlowTests PRIVATE luca_system_formats)
  # The path argument comes from the configure-time option when present; an
  # empty argument still lets the runtime LUCA_SYSTEM_TEST_DATA env win.
  add_test(NAME LucaSystem.RetailAirFlow
      COMMAND LucaSystemRetailAirFlowTests "$ENV{LUCA_SYSTEM_TEST_DATA}")
  add_test(NAME LucaSystem.RetailAirGallery
      COMMAND LucaSystemRetailAirFlowTests "$ENV{LUCA_SYSTEM_TEST_DATA}" --gallery)
  add_test(NAME LucaSystem.RetailAirSaveGrid
      COMMAND LucaSystemRetailAirFlowTests "$ENV{LUCA_SYSTEM_TEST_DATA}" --save-grid)
  add_test(NAME LucaSystem.RetailAirSettings
      COMMAND LucaSystemRetailAirFlowTests "$ENV{LUCA_SYSTEM_TEST_DATA}" --settings)
  set_tests_properties(LucaSystem.RetailAirFlow LucaSystem.RetailAirGallery
      LucaSystem.RetailAirSaveGrid LucaSystem.RetailAirSettings PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
      WORKING_DIRECTORY "${KOTONOHA_PROJECT_ROOT}"
      TIMEOUT 120
      SKIP_RETURN_CODE 77)
endif()

if(BUILD_TESTING)
    kotonoha_label_directory_tests(REAL LucaSystem.RetailAirFlow
        LucaSystem.RetailAirGallery LucaSystem.RetailAirSaveGrid
        LucaSystem.RetailAirSettings LucaSystem.RetailSpriteBase)
endif()
