# SPU ADPCM sound-bank waveforms (format: spu).
if(TARGET kotonoha_format_spu)
    return()
endif()
add_library(kotonoha_format_spu STATIC src/Ps2SoundBank.cpp)
target_include_directories(kotonoha_format_spu PUBLIC
    include
    "${CMAKE_CURRENT_LIST_DIR}/../../internal/include"
)
# TODO(neutral): decoder still returns AudioClip; convert to DecodedPcm.
target_link_libraries(kotonoha_format_spu PUBLIC KotonohaEngine)
target_compile_features(kotonoha_format_spu PUBLIC cxx_std_17)
set_target_properties(kotonoha_format_spu PROPERTIES CXX_EXTENSIONS OFF)
if(BUILD_TESTING AND NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(KotonohaPs2SoundBankTests tests/Ps2SoundBankTests.cpp)
    target_link_libraries(KotonohaPs2SoundBankTests PRIVATE kotonoha_format_spu)
    add_test(NAME Kotonoha.Ps2SoundBank COMMAND KotonohaPs2SoundBankTests)
endif()
