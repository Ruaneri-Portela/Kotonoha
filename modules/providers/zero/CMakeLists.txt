if(TARGET Kotonoha::Modules::Providers::Zero)
    return()
endif()

add_library(kotonoha_provider_zero STATIC src/DiscAssetProvider.cpp)
add_library(Kotonoha::Modules::Providers::Zero ALIAS kotonoha_provider_zero)
add_library(Kotonoha::ProviderZero ALIAS kotonoha_provider_zero)

target_include_directories(kotonoha_provider_zero PUBLIC include)
target_link_libraries(kotonoha_provider_zero PUBLIC Kotonoha::Engine ZeroNoTsukaimaFormats)
target_compile_features(kotonoha_provider_zero PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_zero PROPERTIES CXX_EXTENSIONS OFF)
