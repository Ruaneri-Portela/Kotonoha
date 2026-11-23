# Block-based PCM audio codec (format: block-pcm).
if(TARGET kotonoha_format_block_pcm)
    return()
endif()
add_library(kotonoha_format_block_pcm STATIC
    src/MeoAudio.cpp
)
target_include_directories(kotonoha_format_block_pcm PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
target_compile_features(kotonoha_format_block_pcm PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_block_pcm PROPERTIES CXX_EXTENSIONS OFF)
