if(TARGET kotonoha_provider_amagami)
    return()
endif()

add_library(kotonoha_provider_amagami STATIC
    src/Assets.cpp
    src/VitaCpk.cpp
    src/VitaGraphicsAssets.cpp
)

target_include_directories(kotonoha_provider_amagami PUBLIC include)
target_link_libraries(kotonoha_provider_amagami PUBLIC KotonohaEngine
    AmagamiFormats kotonoha_format_tim2 kotonoha_format_ui_atlas
    kotonoha_format_cri kotonoha_format_spu)
target_compile_features(kotonoha_provider_amagami PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_amagami PROPERTIES CXX_EXTENSIONS OFF)
