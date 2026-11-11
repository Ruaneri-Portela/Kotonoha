if(TARGET Kotonoha::ProviderAmagami)
    return()
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(kotonoha_provider_amagami STATIC src/Assets.cpp)
add_library(Kotonoha::ProviderAmagami ALIAS kotonoha_provider_amagami)

target_include_directories(kotonoha_provider_amagami PUBLIC include)
target_link_libraries(kotonoha_provider_amagami PUBLIC Kotonoha::Engine AmagamiFormats)
target_compile_features(kotonoha_provider_amagami PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_amagami PROPERTIES CXX_EXTENSIONS OFF)
