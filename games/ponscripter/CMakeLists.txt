cmake_minimum_required(VERSION 3.16)

project(PonscripterGame VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(CTest)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/PonscripterLibrary.cmake")

kotonoha_add_app(PonscripterGame src/main.cpp)
target_link_libraries(PonscripterGame PRIVATE ponscripter_port)
set_target_properties(PonscripterGame PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(PonscripterGame
    BUNDLE_ID me.hirameki.kotonoha.ponscripter GAME ponscripter)

set(PONSCRIPTER_TEST_DATA "" CACHE PATH "PONscripter / SayaNoUta_PT-BR data directory for retail integration tests")

if(BUILD_TESTING)
  add_executable(PonscripterRuntimeTests tests/PonscripterRuntimeTest.cpp)
  target_link_libraries(PonscripterRuntimeTests PRIVATE ponscripter_port)
    add_test(NAME Ponscripter.Runtime
        COMMAND PonscripterRuntimeTests
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(Ponscripter.Runtime PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

    if(PONSCRIPTER_TEST_DATA AND EXISTS "${PONSCRIPTER_TEST_DATA}/00.utf")
        add_test(NAME Ponscripter.RetailSayaFlow
            COMMAND PonscripterRuntimeTests "${PONSCRIPTER_TEST_DATA}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        set_tests_properties(Ponscripter.RetailSayaFlow PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
            TIMEOUT 120)
    endif()
endif()

if(BUILD_TESTING)
  kotonoha_label_directory_tests(REAL Ponscripter.RetailSayaFlow)
endif()
