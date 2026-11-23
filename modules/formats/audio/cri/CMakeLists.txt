# CRI ADX/AHX decryption and decoding (format: cri).
if(TARGET kotonoha_format_cri)
    return()
endif()
add_library(kotonoha_format_cri STATIC src/CriAudio.cpp)
target_include_directories(kotonoha_format_cri PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
# TODO(neutral): CriAudio still routes through MediaDecoder/AudioClip.
target_link_libraries(kotonoha_format_cri PUBLIC KotonohaEngine)
target_compile_features(kotonoha_format_cri PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_cri PROPERTIES CXX_EXTENSIONS OFF)
