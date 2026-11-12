cmake_minimum_required(VERSION 3.16)

project(RealLive VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(RealLiveFormats STATIC
    src/Config.cpp
    src/Images.cpp
    src/Voice.cpp
    src/Script.cpp
    src/System.cpp
    vendor/libreallive/archive.cc
    vendor/libreallive/bytecode.cc
    vendor/libreallive/compression.cc
    vendor/libreallive/expression.cc
    vendor/libreallive/filemap.cc
    vendor/libreallive/scenario.cc
)
target_include_directories(RealLiveFormats PUBLIC include vendor)
target_compile_features(RealLiveFormats PUBLIC cxx_std_17)
set_target_properties(RealLiveFormats PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(RealLiveFormats PUBLIC /J)
else()
    target_compile_options(RealLiveFormats PUBLIC -funsigned-char)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/reallive/CMakeLists.txt")
target_link_libraries(RealLiveFormats PUBLIC Kotonoha::Vmreallive)

if(NOT ANDROID)
    add_executable(RealLiveInspect tools/RealLiveInspect.cpp)
    target_link_libraries(RealLiveInspect PRIVATE RealLiveFormats)
endif()

find_package(Iconv REQUIRED)
target_link_libraries(RealLiveFormats PUBLIC Kotonoha::Engine Iconv::Iconv)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/reallive"
    "${CMAKE_CURRENT_BINARY_DIR}/providers/reallive")

add_library(reallive_port STATIC src/RealLiveRuntime.cpp)
target_include_directories(reallive_port PUBLIC src)
set_target_properties(reallive_port PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(reallive_port PUBLIC
    RealLiveFormats
    Kotonoha::Engine
    Iconv::Iconv
    Kotonoha::ProviderRealLive
    Kotonoha::Vmreallive
)

kotonoha_add_app(RealLive src/main.cpp)
target_link_libraries(RealLive PRIVATE reallive_port)
set_target_properties(RealLive PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(RealLive
    BUNDLE_ID me.hirameki.kotonoha.reallive GAME reallive)

if(BUILD_TESTING)
    foreach(format Images Voice Script Session System)
        add_executable(RealLive${format}Tests tests/${format}Test.cpp)
        target_link_libraries(RealLive${format}Tests PRIVATE RealLiveFormats Kotonoha::Vmreallive)
        if(format STREQUAL "Session" AND REALLIVE_TEST_DATA)
            add_test(NAME RealLive${format} COMMAND RealLive${format}Tests "${REALLIVE_TEST_DATA}/Seen.txt")
        elseif(format STREQUAL "Session")
            add_test(NAME RealLive${format} COMMAND RealLive${format}Tests)
            set_tests_properties(RealLive${format} PROPERTIES SKIP_RETURN_CODE 77)
        else()
            add_test(NAME RealLive${format} COMMAND RealLive${format}Tests)
        endif()
    endforeach()

    add_executable(RealLiveEngineTests tests/EngineTest.cpp)
    target_link_libraries(RealLiveEngineTests PRIVATE reallive_port)
    add_executable(RealLiveConfigTests tests/ConfigTest.cpp)
    target_link_libraries(RealLiveConfigTests PRIVATE reallive_port)
    add_test(NAME RealLiveConfig COMMAND RealLiveConfigTests)
endif()
