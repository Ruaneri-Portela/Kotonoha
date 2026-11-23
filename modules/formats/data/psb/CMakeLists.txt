# PSB/MDF structured binary data (format: psb).
find_package(ZLIB REQUIRED)
if(TARGET kotonoha_format_psb)
    return()
endif()
add_library(kotonoha_format_psb STATIC
    src/PsbFile.cpp
)
target_include_directories(kotonoha_format_psb PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
target_link_libraries(kotonoha_format_psb PRIVATE ZLIB::ZLIB)
target_compile_features(kotonoha_format_psb PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_psb PROPERTIES CXX_EXTENSIONS OFF)
