if(TARGET kotonoha_provider_cmp)
    return()
endif()

add_library(kotonoha_provider_cmp STATIC src/Pack.cpp src/SummerAssets.cpp)

target_include_directories(kotonoha_provider_cmp PUBLIC include)
target_link_libraries(kotonoha_provider_cmp PUBLIC KotonohaEngine)
target_compile_features(kotonoha_provider_cmp PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_cmp PROPERTIES CXX_EXTENSIONS OFF)
