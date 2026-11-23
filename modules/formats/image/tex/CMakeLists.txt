# TEX/TEXB/CHR/BGD/CGD/ALP image and byte codecs (format: tex).
if(TARGET kotonoha_format_tex)
    return()
endif()
add_library(kotonoha_format_tex STATIC
    src/Codec.cpp
    src/Images.cpp
)
target_include_directories(kotonoha_format_tex PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
target_compile_features(kotonoha_format_tex PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_tex PROPERTIES CXX_EXTENSIONS OFF)
