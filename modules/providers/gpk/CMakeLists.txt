if(TARGET Kotonoha::ProviderGPK)
  return()
endif()
find_package(ZLIB REQUIRED)
if(NOT TARGET Kotonoha::Engine)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
      "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()
add_library(kotonoha_provider_gpk STATIC
  src/SchoolDaysGpkArchive.cpp
  src/SchoolDaysGpkIndex.cpp
  src/SchoolDaysAssets.cpp
  src/SchoolDaysAssetMount.cpp)
add_library(Kotonoha::ProviderGPK ALIAS kotonoha_provider_gpk)
target_include_directories(kotonoha_provider_gpk PUBLIC include)
target_link_libraries(kotonoha_provider_gpk PUBLIC Kotonoha::Engine PRIVATE ZLIB::ZLIB)
target_compile_features(kotonoha_provider_gpk PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_gpk PROPERTIES CXX_EXTENSIONS OFF)
