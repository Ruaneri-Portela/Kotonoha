# TLG5/TLG6 image codec (format: tlg).
if(TARGET kotonoha_format_tlg)
    return()
endif()
add_library(kotonoha_format_tlg STATIC
    src/TlgImage.cpp
    src/Tlg6Image.cpp
)
target_include_directories(kotonoha_format_tlg PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
target_compile_features(kotonoha_format_tlg PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_tlg PROPERTIES CXX_EXTENSIONS OFF)
