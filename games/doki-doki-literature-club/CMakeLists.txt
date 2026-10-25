# Doki Doki Literature Club! (Ren'Py 6.99) proof-of-concept port: reads the
# original .rpa archives and runs the compiled .rpyc script.
find_package(ZLIB REQUIRED)

add_library(DdlcFormats STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Value.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Archive.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Script.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PyParse.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PyExec.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PyBuiltins.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Display.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Vm.cpp")
target_include_directories(DdlcFormats PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(DdlcFormats PUBLIC cxx_std_20)
target_link_libraries(DdlcFormats PUBLIC ${KOTONOHA_SDL3_TARGET} ZLIB::ZLIB)

if(ANDROID)
    add_library(DokiDoki SHARED "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
    set_target_properties(DokiDoki PROPERTIES OUTPUT_NAME DokiDoki)
    target_link_libraries(DokiDoki PRIVATE Kotonoha KotonohaRuntime DdlcFormats PkgConfig::FFMPEG)
elseif(NOT IOS)
    add_executable(DdlcScriptTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/DdlcScriptTest.cpp")
    target_link_libraries(DdlcScriptTest PRIVATE DdlcFormats)

    add_executable(DokiDoki "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
    target_link_libraries(DokiDoki PRIVATE KotonohaRuntime DdlcFormats PkgConfig::FFMPEG)
    kotonoha_configure_app(DokiDoki
        OUTPUT_NAME DokiDoki
        BUNDLE_ID "me.hirameki.kotonoha.ddlc"
        GAME_ASSET "doki-doki-literature-club"
        )
endif()