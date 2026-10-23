# Snow Radish Vacation!! (Overflow ADV3, Win32) pack-format library.
add_library(SnowRadishFormats STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Pack.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Vm.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Game.cpp")
target_include_directories(SnowRadishFormats PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(SnowRadishFormats PUBLIC cxx_std_17)
target_link_libraries(SnowRadishFormats PUBLIC ${KOTONOHA_SDL3_TARGET})

if(NOT ANDROID AND NOT IOS)
    add_executable(SnowRun "${CMAKE_CURRENT_SOURCE_DIR}/tools/SnowRun.cpp")
    target_link_libraries(SnowRun PRIVATE SnowRadishFormats)
    add_executable(SnowExtract "${CMAKE_CURRENT_SOURCE_DIR}/tools/SnowExtract.cpp")
    target_link_libraries(SnowExtract PRIVATE SnowRadishFormats)

    add_executable(SnowRadishPackTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/PackTest.cpp")
    target_link_libraries(SnowRadishPackTest PRIVATE SnowRadishFormats)
endif()

# Playable application (desktop): SnowRadish --snow <dir with SCRIPT.DAT/GRAPHIC.DAT>
if(NOT ANDROID AND NOT IOS)
    add_executable(SnowRadish "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
    target_link_libraries(SnowRadish PRIVATE KotonohaRuntime SnowRadishFormats)
    kotonoha_configure_app(SnowRadish
        OUTPUT_NAME SnowRadish
        BUNDLE_ID "me.hirameki.kotonoha.snowradish")
endif()
