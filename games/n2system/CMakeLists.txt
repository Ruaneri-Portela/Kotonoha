cmake_minimum_required(VERSION 3.16)

project(N2SystemGame VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(CTest)
endif()

if(NOT TARGET kotonoha_provider_n2)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/n2"
      "${CMAKE_CURRENT_BINARY_DIR}/providers/n2")
endif()

if(NOT TARGET kotonoha_vm_n2system)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/n2system"
      "${CMAKE_CURRENT_BINARY_DIR}/vms/n2system")
endif()

# Reuse PONscripter without creating its standalone executable.
include("${CMAKE_CURRENT_SOURCE_DIR}/../ponscripter/PonscripterLibrary.cmake")

add_library(n2system_formats STATIC
  src/N2Runtime.cpp
)
target_include_directories(n2system_formats PUBLIC include)
target_link_libraries(n2system_formats PUBLIC
  KotonohaEngine
  kotonoha_provider_n2
  kotonoha_vm_n2system
  ponscripter_port
)
target_compile_features(n2system_formats PUBLIC cxx_std_17)
set_target_properties(n2system_formats PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(N2SystemGame src/main.cpp)
target_link_libraries(N2SystemGame PRIVATE n2system_formats)
set_target_properties(N2SystemGame PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(N2SystemGame
    BUNDLE_ID me.hirameki.kotonoha.n2system GAME n2system)

if(BUILD_TESTING)
  add_executable(KotonohaN2DialoguePresentationTests
      tests/DialoguePresentationTest.cpp)
  target_include_directories(KotonohaN2DialoguePresentationTests PRIVATE include)
  target_link_libraries(KotonohaN2DialoguePresentationTests PRIVATE
      kotonoha_vm_n2system)
  add_test(NAME Kotonoha.N2System.DialoguePresentation
      COMMAND KotonohaN2DialoguePresentationTests)
  set_tests_properties(Kotonoha.N2System.DialoguePresentation PROPERTIES
      LABELS synthetic)

  add_executable(N2SystemFormatTests tests/N2SystemTest.cpp)
  target_link_libraries(N2SystemFormatTests PRIVATE n2system_formats)
  add_test(NAME N2SystemFormatTests COMMAND N2SystemFormatTests)
endif()

if(BUILD_TESTING)
  kotonoha_label_directory_tests()
endif()
