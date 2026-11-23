cmake_minimum_required(VERSION 3.16)

project(ArtemisGame VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/ArtemisLibrary.cmake")

kotonoha_add_app(ArtemisGame src/main.cpp)
target_link_libraries(ArtemisGame PRIVATE artemis_port)
set_target_properties(ArtemisGame PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(ArtemisGame
    BUNDLE_ID me.hirameki.kotonoha.artemis GAME artemis)

if(BUILD_TESTING)
    add_executable(ArtemisRuntimeTests tests/ArtemisRuntimeTest.cpp)
    target_link_libraries(ArtemisRuntimeTests PRIVATE artemis_port)
    add_test(NAME Artemis.Runtime COMMAND ArtemisRuntimeTests)
    set_tests_properties(Artemis.Runtime PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        LABELS "synthetic;fast")

    add_executable(ArtemisBootTests tests/ArtemisBootTest.cpp)
    target_link_libraries(ArtemisBootTests PRIVATE artemis_port)
    add_test(NAME Artemis.Boot COMMAND ArtemisBootTests)
    set_tests_properties(Artemis.Boot PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        LABELS "synthetic;fast")

    # Retail assets are supplied at run time through the environment; no
    # absolute installation paths are stored in the repository.
    add_test(NAME Artemis.Retail COMMAND ArtemisBootTests)
    set_tests_properties(Artemis.Retail PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        TIMEOUT 180)

    if(COMMAND kotonoha_label_directory_tests)
        kotonoha_label_directory_tests(REAL Artemis.Retail)
    endif()
endif()
