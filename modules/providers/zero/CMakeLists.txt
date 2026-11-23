if(TARGET kotonoha_provider_zero)
    return()
endif()

add_library(kotonoha_provider_zero STATIC src/DiscAssetProvider.cpp)

target_include_directories(kotonoha_provider_zero PUBLIC include)
target_link_libraries(kotonoha_provider_zero PUBLIC KotonohaEngine
    ZeroNoTsukaimaFormats kotonoha_format_tim2)
target_compile_features(kotonoha_provider_zero PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_zero PROPERTIES CXX_EXTENSIONS OFF)
