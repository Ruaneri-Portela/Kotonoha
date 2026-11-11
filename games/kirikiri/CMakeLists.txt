cmake_minimum_required(VERSION 3.16)

project(Kirikiri VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

if(NOT TARGET Kotonoha::ProviderXP3)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/xp3"
        "${CMAKE_CURRENT_BINARY_DIR}/providers/xp3")
endif()

add_library(kirikiri_formats STATIC src/KirikiriRuntime.cpp)
target_include_directories(kirikiri_formats PUBLIC src include)
target_compile_features(kirikiri_formats PUBLIC cxx_std_17)
set_target_properties(kirikiri_formats PROPERTIES CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/kag/CMakeLists.txt")
find_package(ZLIB REQUIRED)
target_link_libraries(kirikiri_formats PUBLIC
    Kotonoha::Engine
    Kotonoha::ProviderXP3
    ZLIB::ZLIB
    Kotonoha::Vmkag
)

kotonoha_add_app(Kirikiri src/main.cpp)
target_link_libraries(Kirikiri PRIVATE kirikiri_formats)
set_target_properties(Kirikiri PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(Kirikiri
    BUNDLE_ID me.hirameki.kotonoha.kirikiri GAME kirikiri)

set(KIRIKIRI_TEST_DATA "" CACHE PATH "Kirikiri2/KirikiriZ original data for integration tests")

if(BUILD_TESTING)
    add_executable(KirikiriXp3Tests tests/Xp3Test.cpp)
    target_link_libraries(KirikiriXp3Tests PRIVATE kirikiri_formats)
    add_test(NAME KirikiriXp3 COMMAND KirikiriXp3Tests)
endif()
