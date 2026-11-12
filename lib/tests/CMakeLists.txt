add_executable(KotonohaInfrastructureTests InfrastructureTests.cpp)
target_link_libraries(KotonohaInfrastructureTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.Infrastructure COMMAND KotonohaInfrastructureTests)
set_tests_properties(Kotonoha.Infrastructure PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
if(WIN32)
    set_tests_properties(Kotonoha.Infrastructure PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
endif()

add_executable(KotonohaVmTests VmTests.cpp)
target_link_libraries(KotonohaVmTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.Vm COMMAND KotonohaVmTests)

add_executable(KotonohaDisplayModeCatalogTests DisplayModeCatalogTests.cpp)
target_link_libraries(KotonohaDisplayModeCatalogTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.DisplayModeCatalog COMMAND KotonohaDisplayModeCatalogTests)

add_executable(KotonohaDirectoryTests DirectoryTests.cpp)
target_link_libraries(KotonohaDirectoryTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.Directories COMMAND KotonohaDirectoryTests)

add_executable(KotonohaKtrfScriptTests KtrfScriptTests.cpp)
target_link_libraries(KotonohaKtrfScriptTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.KtrfScript COMMAND KotonohaKtrfScriptTests)

add_test(NAME Kotonoha.PackageAssets COMMAND "${CMAKE_COMMAND}"
    "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
    "-DPACKAGE_SCRIPT=${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/PackageAssets.cmake"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/PackageAssetsTest.cmake")
add_test(NAME Kotonoha.BundleContents COMMAND "${CMAKE_COMMAND}"
    "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
    "-DCHECK_SCRIPT=${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/CheckBundleContents.cmake"
    "-DBUNDLE_SCRIPT=${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/BundleApp.cmake"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/BundleContentsTest.cmake")

if(SDLTTF_PLUTOSVG)
    add_executable(KotonohaPlatformSmokeTests PlatformSmokeTests.cpp)
    target_link_libraries(KotonohaPlatformSmokeTests PRIVATE Kotonoha::Engine)
    target_compile_definitions(KotonohaPlatformSmokeTests PRIVATE
        KOTONOHA_SVG_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/SvgTestFont.ttf")
    add_test(NAME Kotonoha.PlatformSmoke COMMAND KotonohaPlatformSmokeTests)
endif()

add_executable(KotonohaAudioChannelsTests AudioChannelsTests.cpp)
target_link_libraries(KotonohaAudioChannelsTests PRIVATE Kotonoha::Engine imgui)
add_test(NAME Kotonoha.AudioChannels COMMAND KotonohaAudioChannelsTests)
set_tests_properties(Kotonoha.AudioChannels PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaAssetProviderTests AssetProviderTests.cpp)
target_link_libraries(KotonohaAssetProviderTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.AssetProvider COMMAND KotonohaAssetProviderTests)
set_tests_properties(Kotonoha.AssetProvider PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
if(WIN32)
    set_tests_properties(Kotonoha.AssetProvider PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
endif()

add_executable(KotonohaComponentHooksTests ComponentHooksTests.cpp)
target_link_libraries(KotonohaComponentHooksTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.ComponentHooks COMMAND KotonohaComponentHooksTests)
set_tests_properties(Kotonoha.ComponentHooks PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaMediaFormatsTests MediaFormatsTests.cpp)
target_link_libraries(KotonohaMediaFormatsTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.MediaFormats COMMAND KotonohaMediaFormatsTests)
set_tests_properties(Kotonoha.MediaFormats PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaMediaAnalysisTests MediaAnalysisTests.cpp)
target_link_libraries(KotonohaMediaAnalysisTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.MediaAnalysis COMMAND KotonohaMediaAnalysisTests)
if(WIN32)
    set_tests_properties(Kotonoha.MediaAnalysis PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
endif()

add_executable(KotonohaMediaPlayerRateTests MediaPlayerRateTests.cpp)
target_link_libraries(KotonohaMediaPlayerRateTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.MediaPlayerRate COMMAND KotonohaMediaPlayerRateTests)
if(WIN32)
    set_tests_properties(Kotonoha.MediaPlayerRate PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:KotonohaEngine>")
endif()

add_executable(KotonohaLauncherTests LauncherTests.cpp)
target_link_libraries(KotonohaLauncherTests PRIVATE Kotonoha::Engine)
target_compile_definitions(KotonohaLauncherTests PRIVATE
    KOTONOHA_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/SvgTestFont.ttf")
add_test(NAME Kotonoha.Launcher COMMAND KotonohaLauncherTests)
set_tests_properties(Kotonoha.Launcher PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaPresentationTests PresentationTests.cpp)
target_link_libraries(KotonohaPresentationTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.Presentation COMMAND KotonohaPresentationTests)
set_tests_properties(Kotonoha.Presentation PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

find_program(KOTONOHA_TEST_FFMPEG ffmpeg)
if(KOTONOHA_TEST_FFMPEG)
    add_executable(KotonohaAudioBufferTests AudioBufferTests.cpp)
    target_link_libraries(KotonohaAudioBufferTests PRIVATE Kotonoha::Engine)
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

add_executable(KotonohaInputActionsTests InputActionsTests.cpp)
target_link_libraries(KotonohaInputActionsTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.InputActions COMMAND KotonohaInputActionsTests)

add_executable(KotonohaDebugTranslationTests DebugTranslationTests.cpp)
target_link_libraries(KotonohaDebugTranslationTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.DebugTranslation COMMAND KotonohaDebugTranslationTests)
set_tests_properties(Kotonoha.DebugTranslation PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

add_executable(KotonohaPs2SoundBankTests Ps2SoundBankTests.cpp)
target_link_libraries(KotonohaPs2SoundBankTests PRIVATE Kotonoha::Engine)
add_test(NAME Kotonoha.Ps2SoundBank COMMAND KotonohaPs2SoundBankTests)

add_executable(KotonohaNativeInputTests NativeInputTests.cpp)
target_link_libraries(KotonohaNativeInputTests PRIVATE Kotonoha::Engine)
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
