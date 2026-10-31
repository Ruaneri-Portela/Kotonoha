cmake_minimum_required(VERSION 3.16)

project(ZeroNoTsukaima VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

# Disc / archive / image / script formats of the PS2 game. Assets stay in the
# user's disc image and are never copied into the repository.
add_library(ZeroNoTsukaimaFormats STATIC
    src/Archive.cpp
    src/Lz.cpp
    src/Tim2.cpp
    src/Scene.cpp
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

# The game on the Kotonoha engine: disc AssetProvider, session and the
# polymorphic engine components.
add_library(zero_no_tsukaima STATIC
    src/Media.cpp
    src/Frontend.cpp
    src/DiscAssetProvider.cpp
    src/ZeroSession.cpp
    src/ZeroComponents.cpp
    src/ZeroRuntime.cpp
    src/ZeroLauncher.cpp
)
target_include_directories(zero_no_tsukaima PUBLIC src)
target_link_libraries(zero_no_tsukaima PUBLIC Kotonoha::Engine ZeroNoTsukaimaFormats)
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
    add_subdirectory(tests)
endif()
