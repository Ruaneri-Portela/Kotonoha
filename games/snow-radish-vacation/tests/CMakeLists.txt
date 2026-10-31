add_executable(SnowPackTest PackTest.cpp)
target_link_libraries(SnowPackTest PRIVATE SnowRadishFormats)
add_test(NAME SnowRadish.Pack COMMAND SnowPackTest)

# Checks against the user's DAT directory (never in the repository).
set(SNOW_DATA_DIR "" CACHE PATH "Snow Radish Vacation DAT directory for the smoke tests")
if(SNOW_DATA_DIR AND EXISTS "${SNOW_DATA_DIR}")
    add_executable(SnowNodeDebugTest SnowNodeDebugTest.cpp)
    target_link_libraries(SnowNodeDebugTest PRIVATE snow_radish_vacation)
    kotonoha_copy_assets(SnowNodeDebugTest)
    add_test(NAME SnowRadish.NodeDebug COMMAND SnowNodeDebugTest "${SNOW_DATA_DIR}")
    set_tests_properties(SnowRadish.NodeDebug PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
    add_executable(SnowEngineTest SnowEngineTest.cpp)
    kotonoha_copy_assets(SnowEngineTest)
    target_link_libraries(SnowEngineTest PRIVATE snow_radish_vacation)
    add_test(NAME SnowRadish.Engine COMMAND SnowEngineTest "${SNOW_DATA_DIR}")
    set_tests_properties(SnowRadish.Engine PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME SnowRadish.Retail COMMAND SnowPackTest "${SNOW_DATA_DIR}")
    add_test(NAME SnowRadish.Boot
        COMMAND SnowRadish --snow "${SNOW_DATA_DIR}" --no-config --frames 120)
    set_tests_properties(SnowRadish.Boot PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endif()
