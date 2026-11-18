if(TARGET kotonoha_provider_rpa)
    return()
endif()

add_library(kotonoha_provider_rpa STATIC src/DdlcAssets.cpp)

target_include_directories(kotonoha_provider_rpa PUBLIC include)
target_link_libraries(kotonoha_provider_rpa PUBLIC KotonohaEngine DdlcFormats)
target_compile_features(kotonoha_provider_rpa PUBLIC cxx_std_20)
set_target_properties(kotonoha_provider_rpa PROPERTIES CXX_EXTENSIONS OFF)
