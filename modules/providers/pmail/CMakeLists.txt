if(TARGET kotonoha_provider_pmail)
    return()
endif()

add_library(kotonoha_provider_pmail STATIC
    "${CMAKE_CURRENT_LIST_DIR}/src/PmailAssets.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/GcPack.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/LargePonPonAssets.cpp"
)

target_include_directories(kotonoha_provider_pmail PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_provider_pmail PUBLIC KotonohaEngine kotonoha_format_crgb kotonoha_format_smfd kotonoha_format_adpcm)
target_compile_features(kotonoha_provider_pmail PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_pmail PROPERTIES CXX_EXTENSIONS OFF)

# tests subdirectory removed from tracked build
# if(BUILD_TESTING)
#     add_subdirectory(tests)
# endif()
