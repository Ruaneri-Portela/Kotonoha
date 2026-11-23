cmake_minimum_required(VERSION 3.16)
project(MissEachOther VERSION 0.1.0 LANGUAGES CXX)

if(NOT TARGET summer_radish_vacation)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../summer-radish-vacation"
        "${CMAKE_CURRENT_BINARY_DIR}/summer-shared")
endif()

kotonoha_add_app(MEO src/main.cpp)
target_link_libraries(MEO PRIVATE summer_radish_vacation)
set_target_properties(MEO PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(MEO
    BUNDLE_ID me.hirameki.kotonoha.meo GAME meo)

set(MEO_DATA_DIR "" CACHE PATH "MISS EACH OTHER retail directory")
if(BUILD_TESTING AND NOT ANDROID)
    add_executable(MeoPresentationTest tests/MeoPresentationTest.cpp)
    target_link_libraries(MeoPresentationTest PRIVATE summer_radish_vacation)
    if(MEO_DATA_DIR)
        add_test(NAME MEO.Presentation COMMAND MeoPresentationTest "${MEO_DATA_DIR}")
        add_test(NAME MEO.SceneMenu COMMAND MeoPresentationTest "${MEO_DATA_DIR}" scene)
        add_test(NAME MEO.MouseMenu COMMAND MeoPresentationTest "${MEO_DATA_DIR}" mouse)
        add_test(NAME MEO.NextScene COMMAND MeoPresentationTest "${MEO_DATA_DIR}" transition)
        add_test(NAME MEO.Choice COMMAND MeoPresentationTest "${MEO_DATA_DIR}" choice)
        add_test(NAME MEO.Audio COMMAND MeoPresentationTest "${MEO_DATA_DIR}" audio)
        set_tests_properties(MEO.Presentation MEO.SceneMenu MEO.MouseMenu
            MEO.NextScene MEO.Choice MEO.Audio PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endif()
endif()
