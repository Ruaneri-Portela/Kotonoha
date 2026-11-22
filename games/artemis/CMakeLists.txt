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

set(ARTEMIS_TEST_DATA_1 "" CACHE PATH
    "Artemis retail game #1 directory (e.g. NUKITASHI)")
set(ARTEMIS_TEST_DATA_2 "" CACHE PATH
    "Artemis retail game #2 directory (e.g. NUKITASHI 2)")

if(BUILD_TESTING)
    add_executable(ArtemisRuntimeTests tests/ArtemisRuntimeTest.cpp)
    target_link_libraries(ArtemisRuntimeTests PRIVATE artemis_port)
    add_test(NAME Artemis.Runtime COMMAND ArtemisRuntimeTests)
    set_tests_properties(Artemis.Runtime PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        LABELS "synthetic;fast")

    set(_artemis_real_tests "")
    set(_nukitashi1_dir "${ARTEMIS_TEST_DATA_1}")
    if(NOT _nukitashi1_dir AND EXISTS "/Volumes/Nero 2TB/SteamLibrary/steamapps/common/NUKITASHI/NUKITASHI.pfs")
        set(_nukitashi1_dir "/Volumes/Nero 2TB/SteamLibrary/steamapps/common/NUKITASHI")
    endif()
    if(_nukitashi1_dir AND EXISTS "${_nukitashi1_dir}/NUKITASHI.pfs")
        add_test(NAME Artemis.Retail.Nukitashi1
            COMMAND ArtemisRuntimeTests "${_nukitashi1_dir}")
        set_tests_properties(Artemis.Retail.Nukitashi1 PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
            TIMEOUT 120)
        list(APPEND _artemis_real_tests Artemis.Retail.Nukitashi1)
    endif()

    set(_nukitashi2_dir "${ARTEMIS_TEST_DATA_2}")
    if(NOT _nukitashi2_dir AND EXISTS "/Volumes/Nero 2TB/SteamLibrary/steamapps/common/NUKITASHI 2/root.pfs")
        set(_nukitashi2_dir "/Volumes/Nero 2TB/SteamLibrary/steamapps/common/NUKITASHI 2")
    endif()
    if(_nukitashi2_dir AND EXISTS "${_nukitashi2_dir}/root.pfs")
        add_test(NAME Artemis.Retail.Nukitashi2
            COMMAND ArtemisRuntimeTests "${_nukitashi2_dir}")
        set_tests_properties(Artemis.Retail.Nukitashi2 PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
            TIMEOUT 120)
        list(APPEND _artemis_real_tests Artemis.Retail.Nukitashi2)
    endif()

    if(COMMAND kotonoha_label_directory_tests)
        kotonoha_label_directory_tests(REAL ${_artemis_real_tests})
    endif()
endif()
