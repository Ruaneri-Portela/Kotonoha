if(TARGET Kotonoha::Modules::Providers::Amagami)
    return()
endif()

add_library(kotonoha_provider_amagami STATIC src/Assets.cpp)
add_library(Kotonoha::Modules::Providers::Amagami ALIAS kotonoha_provider_amagami)
add_library(Kotonoha::ProviderAmagami ALIAS kotonoha_provider_amagami)

target_include_directories(kotonoha_provider_amagami PUBLIC include)
target_link_libraries(kotonoha_provider_amagami PUBLIC Kotonoha::Engine AmagamiFormats)
target_compile_features(kotonoha_provider_amagami PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_amagami PROPERTIES CXX_EXTENSIONS OFF)
