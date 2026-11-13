add_executable(AmagamiFormatsTest FormatsTest.cpp)
target_link_libraries(AmagamiFormatsTest PRIVATE AmagamiFormats)
add_test(NAME Amagami.Formats COMMAND AmagamiFormatsTest)
set(AMAGAMI_DISC_IMAGE "" CACHE FILEPATH "Amagami Japan PS2 disc image for integration checks")
if(AMAGAMI_DISC_IMAGE AND EXISTS "${AMAGAMI_DISC_IMAGE}" AND TARGET AmagamiInspect)
    add_test(NAME Amagami.Disc COMMAND AmagamiInspect "${AMAGAMI_DISC_IMAGE}" --verify)
endif()
if(AMAGAMI_DISC_IMAGE AND EXISTS "${AMAGAMI_DISC_IMAGE}")
    add_executable(AmagamiSessionTest SessionTest.cpp)
    target_link_libraries(AmagamiSessionTest PRIVATE AmagamiFormats Kotonoha::Vmamagami)
    add_test(NAME Amagami.Session COMMAND AmagamiSessionTest "${AMAGAMI_DISC_IMAGE}")
    add_executable(AmagamiEngineTest EngineTest.cpp)
    target_link_libraries(AmagamiEngineTest PRIVATE amagami_port)
    target_include_directories(AmagamiEngineTest PRIVATE ../src)

    add_test(NAME Amagami.Engine COMMAND AmagamiEngineTest "${AMAGAMI_DISC_IMAGE}")
    set_tests_properties(Amagami.Engine PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME Amagami.Boot COMMAND Amagami --amagami "${AMAGAMI_DISC_IMAGE}" --no-config --no-launcher --frames 5)
    set_tests_properties(Amagami.Boot PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endif()
add_executable(AmagamiVmTest VmTest.cpp)
target_link_libraries(AmagamiVmTest PRIVATE AmagamiFormats Kotonoha::Vmamagami)
add_test(NAME Amagami.Vm COMMAND AmagamiVmTest)

if(TEST Amagami.Engine)
    set_property(TEST Amagami.Engine APPEND PROPERTY FAIL_REGULAR_EXPRESSION
        "Estimating duration from bitrate" "Could not update timestamps for discarded samples")
endif()

if(TARGET AmagamiEngineTest)
    add_test(NAME Amagami.Translation COMMAND AmagamiEngineTest "${AMAGAMI_DISC_IMAGE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../translations/amagami/pt-BR.json")
    set_tests_properties(Amagami.Translation PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endif()

add_executable(AmagamiBacklogTest BacklogTest.cpp)
target_link_libraries(AmagamiBacklogTest PRIVATE amagami_port)
target_include_directories(AmagamiBacklogTest PRIVATE ../src)
add_test(NAME Amagami.Backlog COMMAND AmagamiBacklogTest)
set_tests_properties(Amagami.Backlog PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

if(AMAGAMI_DISC_IMAGE AND EXISTS "${AMAGAMI_DISC_IMAGE}")
    add_executable(AmagamiCampaignTest CampaignTest.cpp)
    target_link_libraries(AmagamiCampaignTest PRIVATE AmagamiFormats Kotonoha::Vmamagami)
    add_test(NAME Amagami.Campaign COMMAND AmagamiCampaignTest "${AMAGAMI_DISC_IMAGE}")
endif()
