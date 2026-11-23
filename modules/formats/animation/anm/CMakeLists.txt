# ANM/CHD/CHAR sprite animation codecs (format: anm).
if(TARGET kotonoha_format_anm)
    return()
endif()
add_library(kotonoha_format_anm STATIC
    src/PackAnimation.cpp
)
target_include_directories(kotonoha_format_anm PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
target_compile_features(kotonoha_format_anm PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_anm PROPERTIES CXX_EXTENSIONS OFF)
