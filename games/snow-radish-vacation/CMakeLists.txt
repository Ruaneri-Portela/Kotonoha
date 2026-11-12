cmake_minimum_required(VERSION 3.16)

project(SnowRadishVacation VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

if(NOT TARGET Kotonoha::Modules::Providers::ADV)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/adv"
        "${CMAKE_CURRENT_BINARY_DIR}/providers/adv")
endif()

add_library(SnowRadishFormats STATIC src/Game.cpp)
target_include_directories(SnowRadishFormats PUBLIC include)
target_link_libraries(SnowRadishFormats PUBLIC Kotonoha::Engine Kotonoha::Modules::Providers::ADV)
target_compile_features(SnowRadishFormats PUBLIC cxx_std_17)
set_target_properties(SnowRadishFormats PROPERTIES CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/snow-radish/CMakeLists.txt")
target_link_libraries(SnowRadishFormats PUBLIC Kotonoha::Modules::VMs::SnowRadish)
target_include_directories(SnowRadishFormats PUBLIC
    $<TARGET_PROPERTY:Kotonoha::Modules::VMs::SnowRadish,INTERFACE_INCLUDE_DIRECTORIES>)

add_library(snow_radish_vacation STATIC
    src/SnowSession.cpp
    src/SnowComponents.cpp
    src/SnowRuntime.cpp
    src/SnowLauncher.cpp
)
target_include_directories(snow_radish_vacation PUBLIC src)
target_link_libraries(snow_radish_vacation PUBLIC
    Kotonoha::Engine
    SnowRadishFormats
    Kotonoha::Modules::VMs::SnowRadish
)
set_target_properties(snow_radish_vacation PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(SnowRadish src/main.cpp)
target_link_libraries(SnowRadish PRIVATE snow_radish_vacation)
set_target_properties(SnowRadish PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(SnowRadish
    BUNDLE_ID me.hirameki.kotonoha.snowradishvacation GAME snow-radish-vacation)

if(NOT ANDROID)
    add_executable(SnowRun tools/SnowRun.cpp)
    target_link_libraries(SnowRun PRIVATE SnowRadishFormats)

    add_executable(SnowExtract tools/SnowExtract.cpp)
    target_link_libraries(SnowExtract PRIVATE SnowRadishFormats)
endif()

if(BUILD_TESTING)
add_subdirectory(tests)
endif()
