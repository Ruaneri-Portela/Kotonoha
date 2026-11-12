add_library(kotonoha_provider_pmail STATIC "${CMAKE_CURRENT_LIST_DIR}/src/PmailAssets.cpp")
add_library(Kotonoha::ProviderPmail ALIAS kotonoha_provider_pmail)
message(STATUS "[PmailProvider] Target Kotonoha::ProviderPmail created")

target_include_directories(kotonoha_provider_pmail PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_provider_pmail PUBLIC Kotonoha::Engine)
target_compile_features(kotonoha_provider_pmail PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_pmail PROPERTIES CXX_EXTENSIONS OFF)

# tests subdirectory removed from tracked build
# if(BUILD_TESTING)
#     add_subdirectory(tests)
# endif()
