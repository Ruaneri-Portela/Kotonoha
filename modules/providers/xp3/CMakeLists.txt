cmake_minimum_required(VERSION 3.16)
project(KotonohaProviderXP3 LANGUAGES C CXX)

if(TARGET Kotonoha::ProviderXP3)
  return()
endif()
if(NOT TARGET Kotonoha::Engine)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
      "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()
find_package(ZLIB REQUIRED)
add_library(kotonoha_provider_xp3 STATIC
  src/Xp3Archive.cpp
  src/KirikiriAssets.cpp)
add_library(Kotonoha::ProviderXP3 ALIAS kotonoha_provider_xp3)
target_include_directories(kotonoha_provider_xp3 PUBLIC include)
target_link_libraries(kotonoha_provider_xp3 PUBLIC Kotonoha::Engine PRIVATE ZLIB::ZLIB)
target_compile_features(kotonoha_provider_xp3 PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_xp3 PROPERTIES CXX_EXTENSIONS OFF)
