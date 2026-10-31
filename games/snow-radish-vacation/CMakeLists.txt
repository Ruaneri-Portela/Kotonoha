cmake_minimum_required(VERSION 3.16)

project(SnowRadishVacation VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

# Overflow ADV3 formats (packs, pictures, ADPCM, script VM). Assets stay in the
# user's DAT directory and are never copied into the repository.
add_library(SnowRadishFormats STATIC
    src/Pack.cpp
    src/Vm.cpp
    src/Game.cpp
)
target_include_directories(SnowRadishFormats PUBLIC include)
target_link_libraries(SnowRadishFormats PUBLIC Kotonoha::Engine)
target_compile_features(SnowRadishFormats PUBLIC cxx_std_17)
set_target_properties(SnowRadishFormats PROPERTIES CXX_EXTENSIONS OFF)

# The game on the Kotonoha engine: DAT AssetProvider, session and the
# polymorphic engine components.
add_library(snow_radish_vacation STATIC
    src/SnowAssets.cpp
    src/SnowSession.cpp
    src/SnowComponents.cpp
    src/SnowRuntime.cpp
    src/SnowLauncher.cpp
)
target_include_directories(snow_radish_vacation PUBLIC src)
target_link_libraries(snow_radish_vacation PUBLIC Kotonoha::Engine SnowRadishFormats)
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
