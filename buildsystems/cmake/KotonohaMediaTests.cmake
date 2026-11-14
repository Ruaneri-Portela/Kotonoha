# FFmpeg-backed test fixtures are owned by the Kotonoha build layer,
# not by individual game ports.
include_guard(GLOBAL)

function(kotonoha_register_media_fixture_tests runtime_test_target audio_test_target provider_test_target output_directory)
    find_program(_kotonoha_media_ffmpeg NAMES ffmpeg)
    if(NOT _kotonoha_media_ffmpeg)
        message(WARNING "[Kotonoha] ffmpeg CLI unavailable: generated-media tests disabled")
        return()
    endif()
    set(_media_file "${output_directory}/test-media.mp4")
    add_test(NAME Kotonoha.GenerateTestMedia
        COMMAND "${_kotonoha_media_ffmpeg}"
            -hide_banner -loglevel error
            -f lavfi -i testsrc2=size=320x180:rate=30
            -f lavfi -i sine=frequency=440:sample_rate=44100
            -t 1 -c:v mpeg4 -bf 2 -c:a aac -y "${_media_file}")
    set_tests_properties(Kotonoha.GenerateTestMedia PROPERTIES FIXTURES_SETUP Media)
    foreach(_pair IN ITEMS
            "Kotonoha.Media|${runtime_test_target}"
            "Kotonoha.AudioChannelsMedia|${audio_test_target}"
            "Kotonoha.AssetProviderMedia|${provider_test_target}")
        string(REPLACE "|" ";" _fields "${_pair}")
        list(GET _fields 0 _test_name)
        list(GET _fields 1 _test_target)
        add_test(NAME "${_test_name}" COMMAND "${_test_target}" "${_media_file}")
        set_tests_properties("${_test_name}" PROPERTIES
            FIXTURES_REQUIRED Media
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
    endforeach()
endfunction()
