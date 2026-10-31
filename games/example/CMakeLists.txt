cmake_minimum_required(VERSION 3.16)

project(KotonohaExample VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(kotonoha_demo STATIC
    src/DemoGame.cpp
    src/DemoStory.cpp
    src/DemoStage.cpp
)
target_include_directories(kotonoha_demo PUBLIC src)
target_link_libraries(kotonoha_demo PUBLIC Kotonoha::Engine)
set_target_properties(kotonoha_demo PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(Kotonoha src/main.cpp)
target_link_libraries(Kotonoha PRIVATE kotonoha_demo)
set_target_properties(Kotonoha PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(Kotonoha
    BUNDLE_ID org.kotonoha.example GAME example)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
