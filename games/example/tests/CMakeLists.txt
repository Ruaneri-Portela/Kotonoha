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

add_executable(KotonohaImGuiTests ImGuiTests.cpp)
target_link_libraries(KotonohaImGuiTests PRIVATE Kotonoha::Engine imgui)
add_test(NAME Kotonoha.ImGuiHiDPI COMMAND KotonohaImGuiTests)
set_tests_properties(Kotonoha.ImGuiHiDPI PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

kotonoha_register_media_fixture_tests(
    KotonohaTests
    KotonohaAudioChannelsTests
    KotonohaAssetProviderTests
    "${CMAKE_CURRENT_BINARY_DIR}")

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME Kotonoha.MCP COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/MCPTests.py" "$<TARGET_FILE:Kotonoha>")
set_tests_properties(Kotonoha.MCP PROPERTIES TIMEOUT 30)
