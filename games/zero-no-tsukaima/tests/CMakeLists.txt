add_executable(ZeroFormatsTest ZeroFormatsTest.cpp)
target_link_libraries(ZeroFormatsTest PRIVATE ZeroNoTsukaimaFormats)
add_test(NAME ZeroNoTsukaima.Formats COMMAND ZeroFormatsTest)

add_executable(ZeroConcertoScriptTest ZeroConcertoScriptTest.cpp)
target_link_libraries(ZeroConcertoScriptTest PRIVATE ZeroNoTsukaimaFormats)
add_test(NAME ZeroNoTsukaima.ConcertoScript COMMAND ZeroConcertoScriptTest)

add_executable(ZeroRetailPresentationTest ZeroRetailPresentationTest.cpp)
target_link_libraries(ZeroRetailPresentationTest PRIVATE zero_no_tsukaima)
set(ZERO_SYMPHONY_DISC_IMAGE "" CACHE FILEPATH "Symphony disc image for presentation tests")
foreach(edition CONCERTO SYMPHONY)
    if(ZERO_${edition}_DISC_IMAGE AND EXISTS "${ZERO_${edition}_DISC_IMAGE}")
        add_test(NAME ZeroNoTsukaima.${edition}Presentation
            COMMAND ZeroRetailPresentationTest "${ZERO_${edition}_DISC_IMAGE}")
        set_tests_properties(ZeroNoTsukaima.${edition}Presentation PROPERTIES
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
    endif()
endforeach()

set(ZERO_CONCERTO_DISC_IMAGE "" CACHE FILEPATH "Concerto disc image for archive tests")
if(ZERO_CONCERTO_DISC_IMAGE AND EXISTS "${ZERO_CONCERTO_DISC_IMAGE}")
    add_executable(ZeroConcertoSourceTest ZeroConcertoSourceTest.cpp)
    target_link_libraries(ZeroConcertoSourceTest PRIVATE ZeroNoTsukaimaFormats)
    add_test(NAME ZeroNoTsukaima.ConcertoSource
        COMMAND ZeroConcertoSourceTest "${ZERO_CONCERTO_DISC_IMAGE}")
endif()

# Engine-level checks against the user's disc image (never in the repository).
set(ZERO_DISC_IMAGE "" CACHE FILEPATH "Zero no Tsukaima disc image for the smoke tests")
if(ZERO_DISC_IMAGE AND EXISTS "${ZERO_DISC_IMAGE}")
    add_executable(ZeroNodeDebugTest ZeroNodeDebugTest.cpp)
    target_link_libraries(ZeroNodeDebugTest PRIVATE zero_no_tsukaima)
    add_test(NAME ZeroNoTsukaima.NodeDebug COMMAND ZeroNodeDebugTest "${ZERO_DISC_IMAGE}")
    set_tests_properties(ZeroNoTsukaima.NodeDebug PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;ZERO_START=")
    add_executable(ZeroEngineTest ZeroEngineTest.cpp)
    target_link_libraries(ZeroEngineTest PRIVATE zero_no_tsukaima)
    add_test(NAME ZeroNoTsukaima.Engine COMMAND ZeroEngineTest "${ZERO_DISC_IMAGE}")
    set_tests_properties(ZeroNoTsukaima.Engine PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;ZERO_START=")
    add_test(NAME ZeroNoTsukaima.Boot
        COMMAND ZeroNoTsukaima --zero "${ZERO_DISC_IMAGE}" --no-config
            --no-launcher --frames 120)
    set_tests_properties(ZeroNoTsukaima.Boot PROPERTIES TIMEOUT 120)
    set_tests_properties(ZeroNoTsukaima.Boot PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endif()
