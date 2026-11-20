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
