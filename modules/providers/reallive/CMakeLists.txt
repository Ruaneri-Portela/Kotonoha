if(TARGET Kotonoha::ProviderRealLive)
    return()
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(kotonoha_provider_reallive STATIC src/RealLiveAssets.cpp)
add_library(Kotonoha::ProviderRealLive ALIAS kotonoha_provider_reallive)

target_include_directories(kotonoha_provider_reallive PUBLIC include)
target_link_libraries(kotonoha_provider_reallive PUBLIC Kotonoha::Engine RealLiveFormats)
target_compile_features(kotonoha_provider_reallive PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_reallive PROPERTIES CXX_EXTENSIONS OFF)
