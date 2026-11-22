add_executable(KotonohaInfrastructureTests InfrastructureTests.cpp)
target_link_libraries(KotonohaInfrastructureTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.Infrastructure COMMAND KotonohaInfrastructureTests)
set_tests_properties(Kotonoha.Infrastructure PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
if(WIN32)
    set_tests_properties(Kotonoha.Infrastructure PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
endif()

# The shared CLI owns launcher/headless decisions for every game port.
add_executable(KotonohaCommandLineTests CommandLineTests.cpp)
target_link_libraries(KotonohaCommandLineTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.CommandLine COMMAND KotonohaCommandLineTests)

add_executable(KotonohaVideoSkipGestureTests VideoSkipGestureTests.cpp)
target_link_libraries(KotonohaVideoSkipGestureTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.VideoSkipGesture COMMAND KotonohaVideoSkipGestureTests)
if(WIN32)
    set_tests_properties(Kotonoha.CommandLine PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
endif()

add_executable(KotonohaVmTests VmTests.cpp)
target_link_libraries(KotonohaVmTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.Vm COMMAND KotonohaVmTests)

add_executable(KotonohaDisplayModeCatalogTests DisplayModeCatalogTests.cpp)
target_link_libraries(KotonohaDisplayModeCatalogTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.DisplayModeCatalog COMMAND KotonohaDisplayModeCatalogTests)

add_executable(KotonohaDirectoryTests DirectoryTests.cpp)
target_link_libraries(KotonohaDirectoryTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.Directories COMMAND KotonohaDirectoryTests)

# General filesystem access: cross-platform layout, providers, overlays, saves.
# Synthetic game-source layout checks: case-insensitive VFS lookups and
# matching archive groups without requiring commercial game data.
add_executable(KotonohaGameSourceProbeTests GameSourceProbeTests.cpp)
target_link_libraries(KotonohaGameSourceProbeTests PRIVATE KotonohaEngine)
target_compile_features(KotonohaGameSourceProbeTests PRIVATE cxx_std_17)
add_test(NAME Kotonoha.GameSourceProbe COMMAND KotonohaGameSourceProbeTests)

add_executable(KotonohaEngineDetectorTests EngineDetectorTests.cpp)
target_link_libraries(KotonohaEngineDetectorTests PRIVATE KotonohaEngine)
target_compile_features(KotonohaEngineDetectorTests PRIVATE cxx_std_17)
add_test(NAME Kotonoha.EngineDetector COMMAND KotonohaEngineDetectorTests)

# Shared SDL-backed atomic persistence, including Unicode filenames and
# interruption/replacement isolation. Synthetic fixtures only.
add_executable(KotonohaAtomicFileTests AtomicFileTests.cpp)
target_link_libraries(KotonohaAtomicFileTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.AtomicFile COMMAND KotonohaAtomicFileTests)

add_executable(KotonohaFilesystemV2Tests FilesystemV2Tests.cpp)
target_link_libraries(KotonohaFilesystemV2Tests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.FilesystemV2 COMMAND KotonohaFilesystemV2Tests)

add_executable(KotonohaKtrfScriptTests KtrfScriptTests.cpp)
target_link_libraries(KotonohaKtrfScriptTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.KtrfScript COMMAND KotonohaKtrfScriptTests)

add_test(NAME Kotonoha.PackageAssets COMMAND "${CMAKE_COMMAND}"
    "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
    "-DPACKAGE_SCRIPT=${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/PackageAssets.cmake"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/PackageAssetsTest.cmake")
add_test(NAME Kotonoha.BundleContents COMMAND "${CMAKE_COMMAND}"
    "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
    "-DCHECK_SCRIPT=${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/CheckBundleContents.cmake"
    "-DBUNDLE_SCRIPT=${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/BundleApp.cmake"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/BundleContentsTest.cmake")

if(SDLTTF_PLUTOSVG)
    add_executable(KotonohaPlatformSmokeTests PlatformSmokeTests.cpp)
    target_link_libraries(KotonohaPlatformSmokeTests PRIVATE KotonohaEngine)
    target_compile_definitions(KotonohaPlatformSmokeTests PRIVATE
        KOTONOHA_SVG_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/SvgTestFont.ttf")
    add_test(NAME Kotonoha.PlatformSmoke COMMAND KotonohaPlatformSmokeTests)
endif()

add_executable(KotonohaAudioChannelsTests AudioChannelsTests.cpp)
target_link_libraries(KotonohaAudioChannelsTests PRIVATE KotonohaEngine ${KOTONOHA_IMGUI_TARGET})
add_test(NAME Kotonoha.AudioChannels COMMAND KotonohaAudioChannelsTests)
set_tests_properties(Kotonoha.AudioChannels PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaAssetProviderTests AssetProviderTests.cpp)
target_link_libraries(KotonohaAssetProviderTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.AssetProvider COMMAND KotonohaAssetProviderTests)
set_tests_properties(Kotonoha.AssetProvider PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
if(WIN32)
    set_tests_properties(Kotonoha.AssetProvider PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
endif()

add_executable(KotonohaComponentHooksTests ComponentHooksTests.cpp)
target_link_libraries(KotonohaComponentHooksTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.ComponentHooks COMMAND KotonohaComponentHooksTests)
set_tests_properties(Kotonoha.ComponentHooks PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaMediaFormatsTests MediaFormatsTests.cpp)
target_link_libraries(KotonohaMediaFormatsTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.MediaFormats COMMAND KotonohaMediaFormatsTests)
set_tests_properties(Kotonoha.MediaFormats PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaMediaAnalysisTests MediaAnalysisTests.cpp)
target_link_libraries(KotonohaMediaAnalysisTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.MediaAnalysis COMMAND KotonohaMediaAnalysisTests)
if(WIN32)
    set_tests_properties(Kotonoha.MediaAnalysis PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
endif()

add_executable(KotonohaMediaPlayerRateTests MediaPlayerRateTests.cpp)
target_link_libraries(KotonohaMediaPlayerRateTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.MediaPlayerRate COMMAND KotonohaMediaPlayerRateTests)
if(WIN32)
    set_tests_properties(Kotonoha.MediaPlayerRate PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
endif()

# Git-driven version is shared by the CLI, launcher and debug applets.
add_executable(KotonohaBuildVersionTests BuildVersionTests.cpp)
target_link_libraries(KotonohaBuildVersionTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.BuildVersion COMMAND KotonohaBuildVersionTests)
set_tests_properties(Kotonoha.BuildVersion PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaLauncherTests LauncherTests.cpp)
target_link_libraries(KotonohaLauncherTests PRIVATE KotonohaEngine)
# The test stages KOTONOHA_TEST_FONT in its own temporary assets directory.
# Never overwrite the shared default font beside the test binaries: other
# suites load that font from SDL_GetBasePath()/assets/fonts.
target_compile_definitions(KotonohaLauncherTests PRIVATE
    KOTONOHA_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/SvgTestFont.ttf")
add_test(NAME Kotonoha.Launcher COMMAND KotonohaLauncherTests)
set_tests_properties(Kotonoha.Launcher PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaPresentationTests PresentationTests.cpp)
target_link_libraries(KotonohaPresentationTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.Presentation COMMAND KotonohaPresentationTests)
set_tests_properties(Kotonoha.Presentation PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

find_program(KOTONOHA_TEST_FFMPEG ffmpeg)
if(KOTONOHA_TEST_FFMPEG)
    add_executable(KotonohaAudioBufferTests AudioBufferTests.cpp)
    target_link_libraries(KotonohaAudioBufferTests PRIVATE KotonohaEngine)
    add_test(NAME Kotonoha.AudioBufferFixture COMMAND "${KOTONOHA_TEST_FFMPEG}"
        -v error -y -f lavfi -i "testsrc2=size=320x180:rate=24"
        -f lavfi -i "sine=frequency=440:sample_rate=48000"
        -t 4 -c:v wmv2 -c:a wmav2 "${CMAKE_CURRENT_BINARY_DIR}/interleaved.wmv"
        -f lavfi -i "sine=frequency=330:sample_rate=44100" -t 6 -map 2:a
        "${CMAKE_CURRENT_BINARY_DIR}/tone.flac")
    set_tests_properties(Kotonoha.AudioBufferFixture PROPERTIES FIXTURES_SETUP interleaved_audio)
    add_test(NAME Kotonoha.AudioBuffer COMMAND KotonohaAudioBufferTests
        "${CMAKE_CURRENT_BINARY_DIR}/interleaved.wmv" "${CMAKE_CURRENT_BINARY_DIR}/tone.flac")
    set_tests_properties(Kotonoha.AudioBuffer PROPERTIES
        FIXTURES_REQUIRED interleaved_audio
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endif()

# Pure C++17 wrapping test; runs without SDL, a window or game assets.
add_executable(KotonohaEncodingUtfTests EncodingUtfTests.cpp)
target_link_libraries(KotonohaEncodingUtfTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.EncodingUtf COMMAND KotonohaEncodingUtfTests)

add_executable(KotonohaTextWrappingStandaloneTests TextWrappingStandalone.cpp)
target_include_directories(KotonohaTextWrappingStandaloneTests PRIVATE
    "${KOTONOHA_PROJECT_ROOT}/lib/include")
add_test(NAME Kotonoha.TextWrapping COMMAND KotonohaTextWrappingStandaloneTests)

add_executable(KotonohaInputActionsTests InputActionsTests.cpp)
target_link_libraries(KotonohaInputActionsTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.InputActions COMMAND KotonohaInputActionsTests)

add_executable(KotonohaInputMapperTests InputMapperTests.cpp)
target_link_libraries(KotonohaInputMapperTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.InputMapper COMMAND KotonohaInputMapperTests)

# A widget throwing or a minimized window must not leave ImGui's frame
# open for the next loop, which otherwise triggers an internal assertion.
add_executable(KotonohaImGuiFrameScopeTests ImGuiFrameScopeTests.cpp)
target_include_directories(KotonohaImGuiFrameScopeTests PRIVATE
    "${KOTONOHA_PROJECT_ROOT}/lib/include")
target_compile_features(KotonohaImGuiFrameScopeTests PRIVATE cxx_std_17)
target_link_libraries(KotonohaImGuiFrameScopeTests PRIVATE
    ${KOTONOHA_IMGUI_TARGET})
add_test(NAME Kotonoha.ImGuiFrameScope COMMAND KotonohaImGuiFrameScopeTests)

if(NOT KOTONOHA_DISTRIBUTION_BUILD)
add_executable(KotonohaImGuiThemeTests ImGuiThemeTests.cpp)
target_link_libraries(KotonohaImGuiThemeTests PRIVATE KotonohaEngine)
target_compile_definitions(KotonohaImGuiThemeTests PRIVATE
    KOTONOHA_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/SvgTestFont.ttf")
add_test(NAME Kotonoha.ImGuiTheme COMMAND KotonohaImGuiThemeTests)
set_tests_properties(Kotonoha.ImGuiTheme PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endif()

add_executable(KotonohaDebugTranslationTests DebugTranslationTests.cpp)
target_link_libraries(KotonohaDebugTranslationTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.DebugTranslation COMMAND KotonohaDebugTranslationTests)
set_tests_properties(Kotonoha.DebugTranslation PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaPs2SoundBankTests Ps2SoundBankTests.cpp)
target_link_libraries(KotonohaPs2SoundBankTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.Ps2SoundBank COMMAND KotonohaPs2SoundBankTests)

add_executable(KotonohaNativeInputTests NativeInputTests.cpp)
target_link_libraries(KotonohaNativeInputTests PRIVATE KotonohaEngine)
add_test(NAME Kotonoha.NativeInput COMMAND KotonohaNativeInputTests)
set_tests_properties(Kotonoha.NativeInput PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

# The Windows loader does not search the engine DLL output directory when CTest
# launches test executables from lib/tests/Release. Apply the same runtime path
# to every executable test; the CMake script fixtures do not load the engine.
if(WIN32)
    get_property(_kotonoha_tests DIRECTORY PROPERTY TESTS)
    foreach(_test IN LISTS _kotonoha_tests)
        if(NOT _test MATCHES "^Kotonoha\\.(PackageAssets|BundleContents|AudioBufferFixture)$")
            set_property(TEST "${_test}" APPEND PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
        endif()
    endforeach()
endif()

# Engine tests use generated media and bundled test fixtures only.
kotonoha_label_directory_tests()
