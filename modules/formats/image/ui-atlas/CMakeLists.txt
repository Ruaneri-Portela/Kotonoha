# Font/UI atlas image codec (format: ui-atlas).
if(TARGET kotonoha_format_ui_atlas)
    return()
endif()
add_library(kotonoha_format_ui_atlas STATIC
    src/VitaSystemGraphics.cpp
)
target_include_directories(kotonoha_format_ui_atlas PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
    "${CMAKE_CURRENT_LIST_DIR}/../tim2/include"
)
target_compile_features(kotonoha_format_ui_atlas PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_ui_atlas PROPERTIES CXX_EXTENSIONS OFF)
