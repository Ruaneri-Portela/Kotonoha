# SMFD MIDI stream codec (format: smfd).
if(TARGET kotonoha_format_smfd)
    return()
endif()
add_library(kotonoha_format_smfd STATIC
    src/Smfd.cpp
)
target_include_directories(kotonoha_format_smfd PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
target_compile_features(kotonoha_format_smfd PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_smfd PROPERTIES CXX_EXTENSIONS OFF)
