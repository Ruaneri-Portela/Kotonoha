cmake_minimum_required(VERSION 3.16)

project(ZeroNoTsukaima VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(ZeroNoTsukaimaFormats STATIC
    src/Archive.cpp
    src/Lz.cpp
    src/Scene.cpp
    src/ConcertoScene.cpp
    src/Source.cpp
    src/Catalog.cpp
    src/Vag.cpp
    src/Pss.cpp
    src/Game.cpp
)
target_include_directories(ZeroNoTsukaimaFormats PUBLIC include)
target_link_libraries(ZeroNoTsukaimaFormats PUBLIC Kotonoha::Engine)
target_compile_features(ZeroNoTsukaimaFormats PUBLIC cxx_std_17)
set_target_properties(ZeroNoTsukaimaFormats PROPERTIES CXX_EXTENSIONS OFF)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/zero"
    "${CMAKE_CURRENT_BINARY_DIR}/providers/zero")

add_library(zero_no_tsukaima STATIC
    src/Media.cpp
    src/Frontend.cpp
    src/ZeroSession.cpp
    src/ZeroComponents.cpp
    src/ZeroRuntime.cpp
    src/ZeroLauncher.cpp
)
target_include_directories(zero_no_tsukaima PUBLIC src)
target_link_libraries(zero_no_tsukaima PUBLIC
    Kotonoha::Engine
    ZeroNoTsukaimaFormats
    Kotonoha::Modules::Providers::Zero
)
set_target_properties(zero_no_tsukaima PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(ZeroNoTsukaima src/main.cpp)
target_link_libraries(ZeroNoTsukaima PRIVATE zero_no_tsukaima)
set_target_properties(ZeroNoTsukaima PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(ZeroNoTsukaima
    BUNDLE_ID me.hirameki.kotonoha.zeronotsukaima GAME zero-no-tsukaima)

if(NOT ANDROID)
    add_executable(ZeroExtract tools/ZeroExtract.cpp)
    target_link_libraries(ZeroExtract PRIVATE ZeroNoTsukaimaFormats)

    add_executable(ZeroRun tools/ZeroRun.cpp)
    target_link_libraries(ZeroRun PRIVATE ZeroNoTsukaimaFormats)
endif()

if(BUILD_TESTING)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
add_subdirectory(tests)
endif()
