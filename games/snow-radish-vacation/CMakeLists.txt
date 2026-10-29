cmake_minimum_required(VERSION 3.18)
project(SnowRadishApp LANGUAGES C CXX)

if(NOT TARGET KotonohaRuntime)
    set(_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    set(KOTONOHA_INCLUDE_DIR "${_ROOT_DIR}/include")
    set(KOTONOHA_ENGINE_SOURCE_DIR "${_ROOT_DIR}/src/Kotonoha")
    add_subdirectory("${_ROOT_DIR}" kotonoha_build EXCLUDE_FROM_ALL)
endif()

# Snow Radish Vacation!! (Overflow ADV3, Win32) pack-format library.
add_library(SnowRadishFormats STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Pack.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Vm.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Game.cpp")
target_include_directories(SnowRadishFormats PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(SnowRadishFormats PUBLIC cxx_std_17)
target_link_libraries(SnowRadishFormats PUBLIC ${KOTONOHA_SDL3_TARGET})

if(ANDROID)
    add_library(SnowRadish SHARED "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
    set_target_properties(SnowRadish PROPERTIES OUTPUT_NAME SnowRadish)
    target_link_libraries(SnowRadish PRIVATE KotonohaRuntime SnowRadishFormats)
elseif(NOT IOS)
    add_executable(SnowRun "${CMAKE_CURRENT_SOURCE_DIR}/tools/SnowRun.cpp")
    target_link_libraries(SnowRun PRIVATE SnowRadishFormats)
    add_executable(SnowExtract "${CMAKE_CURRENT_SOURCE_DIR}/tools/SnowExtract.cpp")
    target_link_libraries(SnowExtract PRIVATE SnowRadishFormats)

    add_executable(SnowRadishPackTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/PackTest.cpp")
    target_link_libraries(SnowRadishPackTest PRIVATE SnowRadishFormats)

    add_executable(SnowRadish "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
    target_link_libraries(SnowRadish PRIVATE KotonohaRuntime SnowRadishFormats)
    kotonoha_configure_app(SnowRadish
        OUTPUT_NAME SnowRadish
        BUNDLE_ID "me.hirameki.kotonoha.snowradish"
        GAME_ASSET "snow-radish-vacation"
        )
endif()