# LucaSystem CZ0-CZ4 image codec (format: cz).
if(TARGET kotonoha_format_cz)
    return()
endif()
add_library(kotonoha_format_cz STATIC
    src/CzImage.cpp
)
target_include_directories(kotonoha_format_cz PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
target_compile_features(kotonoha_format_cz PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_cz PROPERTIES CXX_EXTENSIONS OFF)
