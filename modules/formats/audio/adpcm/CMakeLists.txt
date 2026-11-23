# ADPS/ADP4/IMA ADPCM audio codecs (format: adpcm).
if(TARGET kotonoha_format_adpcm)
    return()
endif()
add_library(kotonoha_format_adpcm STATIC
    src/PackAudio.cpp
    src/Adpcm.cpp
)
target_include_directories(kotonoha_format_adpcm PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
target_compile_features(kotonoha_format_adpcm PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_adpcm PROPERTIES CXX_EXTENSIONS OFF)
