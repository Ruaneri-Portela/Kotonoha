if(TARGET kotonoha_provider_adv)
    return()
endif()

add_library(kotonoha_provider_adv STATIC src/Pack.cpp src/SnowAssets.cpp)

target_include_directories(kotonoha_provider_adv PUBLIC include)
target_link_libraries(kotonoha_provider_adv PUBLIC KotonohaEngine
    kotonoha_format_evd kotonoha_format_anm
    kotonoha_format_adpcm)
target_compile_features(kotonoha_provider_adv PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_adv PROPERTIES CXX_EXTENSIONS OFF)
