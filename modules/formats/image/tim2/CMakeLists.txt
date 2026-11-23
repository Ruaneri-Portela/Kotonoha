# TIM2/GIM texture and palette codec (format: tim2).
if(TARGET kotonoha_format_tim2)
    return()
endif()
add_library(kotonoha_format_tim2 STATIC
    src/VitaGraphics.cpp
    src/Ps2Tim2.cpp
)
target_include_directories(kotonoha_format_tim2 PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
target_compile_features(kotonoha_format_tim2 PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_tim2 PROPERTIES CXX_EXTENSIONS OFF)
