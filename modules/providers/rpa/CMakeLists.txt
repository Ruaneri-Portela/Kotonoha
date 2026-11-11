if(TARGET Kotonoha::Modules::Providers::RPA)
    return()
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(kotonoha_provider_rpa STATIC src/DdlcAssets.cpp)
add_library(Kotonoha::Modules::Providers::RPA ALIAS kotonoha_provider_rpa)
add_library(Kotonoha::ProviderRPA ALIAS kotonoha_provider_rpa)

target_include_directories(kotonoha_provider_rpa PUBLIC include)
target_link_libraries(kotonoha_provider_rpa PUBLIC Kotonoha::Engine DdlcFormats)
target_compile_features(kotonoha_provider_rpa PUBLIC cxx_std_20)
set_target_properties(kotonoha_provider_rpa PROPERTIES CXX_EXTENSIONS OFF)
