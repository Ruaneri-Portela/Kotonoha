# CRGB-family image/animation codecs (format: crgb).
if(TARGET kotonoha_format_crgb)
    return()
endif()
add_library(kotonoha_format_crgb STATIC
    src/LargePonPonCodecs.cpp
)
target_include_directories(kotonoha_format_crgb PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
target_compile_features(kotonoha_format_crgb PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_crgb PROPERTIES CXX_EXTENSIONS OFF)
