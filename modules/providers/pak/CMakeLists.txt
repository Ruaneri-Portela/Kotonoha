if(TARGET kotonoha_provider_pak)
    return()
endif()

add_library(kotonoha_provider_pak STATIC src/LucaSystemAssets.cpp)

target_include_directories(kotonoha_provider_pak PUBLIC include)
target_link_libraries(kotonoha_provider_pak PUBLIC KotonohaEngine kotonoha_format_cz)
target_compile_features(kotonoha_provider_pak PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_pak PROPERTIES CXX_EXTENSIONS OFF)
