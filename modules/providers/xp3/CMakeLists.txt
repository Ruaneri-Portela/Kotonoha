if(TARGET kotonoha_provider_xp3)
    return()
endif()

find_package(ZLIB REQUIRED)

add_library(kotonoha_provider_xp3 STATIC
    src/Xp3Archive.cpp
    src/KirikiriAssets.cpp
    src/TjsBootstrap.cpp
)

target_include_directories(kotonoha_provider_xp3 PUBLIC include)
target_link_libraries(kotonoha_provider_xp3 PUBLIC KotonohaEngine
    kotonoha_format_tlg kotonoha_format_psb
    PRIVATE ZLIB::ZLIB)
target_compile_features(kotonoha_provider_xp3 PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_xp3 PROPERTIES CXX_EXTENSIONS OFF)
