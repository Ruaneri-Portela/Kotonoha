if(TARGET kotonoha_provider_gpk)
    return()
endif()

find_package(ZLIB REQUIRED)

add_library(kotonoha_provider_gpk STATIC
    src/SchoolDaysGpkArchive.cpp
    src/SchoolDaysGpkIndex.cpp
    src/SchoolDaysAssets.cpp
    src/SchoolDaysAssetMount.cpp
)

target_include_directories(kotonoha_provider_gpk PUBLIC include)
target_link_libraries(kotonoha_provider_gpk PUBLIC KotonohaEngine PRIVATE ZLIB::ZLIB)
target_compile_features(kotonoha_provider_gpk PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_gpk PROPERTIES CXX_EXTENSIONS OFF)
