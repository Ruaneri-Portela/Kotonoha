add_executable(ZeroFormatsTest ZeroFormatsTest.cpp)
target_link_libraries(ZeroFormatsTest PRIVATE ZeroNoTsukaimaFormats)
add_test(NAME ZeroNoTsukaima.Formats COMMAND ZeroFormatsTest)

# Engine-level checks against the user's disc image (never in the repository).
set(ZERO_DISC_IMAGE "" CACHE FILEPATH "Zero no Tsukaima disc image for the smoke tests")
if(ZERO_DISC_IMAGE AND EXISTS "${ZERO_DISC_IMAGE}")
    add_executable(ZeroNodeDebugTest ZeroNodeDebugTest.cpp)
    target_link_libraries(ZeroNodeDebugTest PRIVATE zero_no_tsukaima)
    kotonoha_copy_assets(ZeroNodeDebugTest)
    add_test(NAME ZeroNoTsukaima.NodeDebug COMMAND ZeroNodeDebugTest "${ZERO_DISC_IMAGE}")
    set_tests_properties(ZeroNoTsukaima.NodeDebug PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;ZERO_START=")
    add_executable(ZeroEngineTest ZeroEngineTest.cpp)
    kotonoha_copy_assets(ZeroEngineTest)
    target_link_libraries(ZeroEngineTest PRIVATE zero_no_tsukaima)
    add_test(NAME ZeroNoTsukaima.Engine COMMAND ZeroEngineTest "${ZERO_DISC_IMAGE}")
    set_tests_properties(ZeroNoTsukaima.Engine PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;ZERO_START=")
    add_test(NAME ZeroNoTsukaima.Boot
        COMMAND ZeroNoTsukaima --zero "${ZERO_DISC_IMAGE}" --no-config --frames 120)
    set_tests_properties(ZeroNoTsukaima.Boot PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endif()
