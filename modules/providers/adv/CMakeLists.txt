if(TARGET Kotonoha::Modules::Providers::ADV)
    return()
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(kotonoha_provider_adv STATIC src/Pack.cpp src/SnowAssets.cpp)
add_library(Kotonoha::Modules::Providers::ADV ALIAS kotonoha_provider_adv)
add_library(Kotonoha::ProviderADV ALIAS kotonoha_provider_adv)

target_include_directories(kotonoha_provider_adv PUBLIC include)
target_link_libraries(kotonoha_provider_adv PUBLIC Kotonoha::Engine)
target_compile_features(kotonoha_provider_adv PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_adv PROPERTIES CXX_EXTENSIONS OFF)
