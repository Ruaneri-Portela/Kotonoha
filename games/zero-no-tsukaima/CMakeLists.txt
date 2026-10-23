# Zero no Tsukaima (PS2) support: disc/archive/image format library and tools.
# Pure C++17 + SDL3 (no engine runtime needed yet); assets stay in the user's
# disc image and are never copied into the repository.
add_library(ZeroNoTsukaimaFormats STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Disc.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Lz.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Tim2.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Scene.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Source.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Catalog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Vag.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Pss.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Game.cpp")
target_include_directories(ZeroNoTsukaimaFormats PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ZeroNoTsukaimaFormats PUBLIC ${KOTONOHA_SDL3_TARGET})

if(ANDROID)
    add_library(ZeroNoTsukaima SHARED
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ZeroMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Frontend.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/MoviePlayer.cpp")
    set_target_properties(ZeroNoTsukaima PROPERTIES OUTPUT_NAME ZeroNoTsukaima)
    target_link_libraries(ZeroNoTsukaima PRIVATE KotonohaRuntime ZeroNoTsukaimaFormats)
elseif(NOT IOS)
    add_executable(ZeroExtract "${CMAKE_CURRENT_SOURCE_DIR}/tools/ZeroExtract.cpp")
    target_link_libraries(ZeroExtract PRIVATE ZeroNoTsukaimaFormats)

    add_executable(ZeroRun "${CMAKE_CURRENT_SOURCE_DIR}/tools/ZeroRun.cpp")
    target_link_libraries(ZeroRun PRIVATE ZeroNoTsukaimaFormats)

    # Registered with CTest from the root CMakeLists (CTest is enabled there).
    add_executable(ZeroFormatsTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/ZeroFormatsTest.cpp")
    target_link_libraries(ZeroFormatsTest PRIVATE ZeroNoTsukaimaFormats)

    # Playable application (desktop): ZeroNoTsukaima --zero <disc.bin>
    add_executable(ZeroNoTsukaima
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ZeroMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Frontend.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/MoviePlayer.cpp")
    target_link_libraries(ZeroNoTsukaima PRIVATE KotonohaRuntime ZeroNoTsukaimaFormats)
    kotonoha_configure_app(ZeroNoTsukaima
        OUTPUT_NAME ZeroNoTsukaima
        BUNDLE_ID "me.hirameki.kotonoha.zero")
endif()
