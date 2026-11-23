add_executable(KotonohaTests EngineTests.cpp)
target_link_libraries(KotonohaTests PRIVATE kotonoha_demo)
add_test(NAME Kotonoha.Runtime COMMAND KotonohaTests)
set_tests_properties(Kotonoha.Runtime PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaDemoTests DemoTests.cpp)
target_link_libraries(KotonohaDemoTests PRIVATE kotonoha_demo)
add_test(NAME Kotonoha.Demo COMMAND KotonohaDemoTests)
set_tests_properties(Kotonoha.Demo PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
# The demo test runs from its own executable directory, not the Kotonoha
# game's packaged asset directory. Stage the generated splash from the
# versioned brand source and the real engine font; a test-only SVG font has
# no Latin glyphs and must not replace the default font shared by every test
# binary.
add_custom_command(TARGET KotonohaDemoTests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:KotonohaDemoTests>/assets/fonts"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${KOTONOHA_BRAND_OUTPUT}/splash/SPLASH.PNG"
        "$<TARGET_FILE_DIR:KotonohaDemoTests>/assets/SPLASH.PNG"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${KOTONOHA_PROJECT_ROOT}/assets/fonts/NotoSansJP-Regular.ttf"
        "$<TARGET_FILE_DIR:KotonohaDemoTests>/assets/fonts/NotoSansJP-Regular.ttf"
    VERBATIM)

add_executable(KotonohaImGuiTests ImGuiTests.cpp)
target_link_libraries(KotonohaImGuiTests PRIVATE KotonohaEngine ${KOTONOHA_IMGUI_TARGET})
add_test(NAME Kotonoha.ImGuiHiDPI COMMAND KotonohaImGuiTests)
set_tests_properties(Kotonoha.ImGuiHiDPI PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

find_program(FFMPEG_EXECUTABLE ffmpeg)
if(FFMPEG_EXECUTABLE)
    add_test(NAME Kotonoha.GenerateTestMedia COMMAND "${FFMPEG_EXECUTABLE}"
        -hide_banner -loglevel error
        -f lavfi -i testsrc2=size=320x180:rate=30
        -f lavfi -i sine=frequency=440:sample_rate=44100
        -t 1 -c:v mpeg4 -bf 2 -c:a aac -y
        "${CMAKE_CURRENT_BINARY_DIR}/test-media.mp4")
    set_tests_properties(Kotonoha.GenerateTestMedia PROPERTIES FIXTURES_SETUP Media)
    add_test(NAME Kotonoha.Media COMMAND KotonohaTests
        "${CMAKE_CURRENT_BINARY_DIR}/test-media.mp4")
    set_tests_properties(Kotonoha.Media PROPERTIES
        FIXTURES_REQUIRED Media
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME Kotonoha.AudioChannelsMedia COMMAND KotonohaAudioChannelsTests
        "${CMAKE_CURRENT_BINARY_DIR}/test-media.mp4")
    set_tests_properties(Kotonoha.AudioChannelsMedia PROPERTIES
        FIXTURES_REQUIRED Media
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME Kotonoha.AssetProviderMedia COMMAND KotonohaAssetProviderTests
        "${CMAKE_CURRENT_BINARY_DIR}/test-media.mp4")
    set_tests_properties(Kotonoha.AssetProviderMedia PROPERTIES
        FIXTURES_REQUIRED Media
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
else()
    message(WARNING "ffmpeg CLI not found: generated-media tests are disabled")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(KOTONOHA_ENABLE_MCP)
    add_test(NAME Kotonoha.MCP COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/MCPTests.py" "$<TARGET_FILE:Kotonoha>"
        "${KOTONOHA_PROJECT_ROOT}/lib/tests/fixtures/SvgTestFont.ttf")
    set_tests_properties(Kotonoha.MCP PROPERTIES TIMEOUT 30)
else()
    # Test OFF behavior explicitly instead of silently skipping the module.
    add_test(NAME Kotonoha.MCPDisabled COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/MCPDisabledTests.py" "$<TARGET_FILE:Kotonoha>")
    set_tests_properties(Kotonoha.MCPDisabled PROPERTIES TIMEOUT 30)
endif()

kotonoha_label_directory_tests()
