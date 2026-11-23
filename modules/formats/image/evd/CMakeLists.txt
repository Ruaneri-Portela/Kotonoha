# EVD picture and alpha-mask codec (format: evd).
if(TARGET kotonoha_format_evd)
    return()
endif()
add_library(kotonoha_format_evd STATIC
    src/PackImage.cpp
)
target_include_directories(kotonoha_format_evd PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
target_compile_features(kotonoha_format_evd PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_evd PROPERTIES CXX_EXTENSIONS OFF)
