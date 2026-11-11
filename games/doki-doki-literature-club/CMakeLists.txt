cmake_minimum_required(VERSION 3.16)

project(DokiDokiLiteratureClub VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

find_package(ZLIB REQUIRED)
if(ANDROID)
    find_package(Python3 REQUIRED COMPONENTS Development)
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
endif()

add_library(DdlcFormats STATIC
    src/Archive.cpp
    src/Display.cpp
    src/Python.cpp
    src/Replay.cpp
    src/Script.cpp
    src/Value.cpp
)
target_include_directories(DdlcFormats PUBLIC include)
target_compile_features(DdlcFormats PUBLIC cxx_std_20)
target_link_libraries(DdlcFormats PUBLIC SDL3::SDL3 ZLIB::ZLIB
    PRIVATE Python3::Python)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/renpy/CMakeLists.txt")
# DdlcFormats' session/runtime drive the Ren'Py VM, so consumers get it
# transitively.
target_link_libraries(DdlcFormats PUBLIC Kotonoha::Vmrenpy)
# DdlcFormats sources include Ddlc/Runtime.hpp, which now lives in the VM
# module. Pull the VM's include directory without linking it (no link cycle:
# the VM links DdlcFormats privately for Ddlc/Python.hpp and friends).
target_include_directories(DdlcFormats PUBLIC
    $<TARGET_PROPERTY:Kotonoha::Vmrenpy,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(DdlcFormats PROPERTIES CXX_EXTENSIONS OFF)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/rpa"
    "${CMAKE_CURRENT_BINARY_DIR}/providers/rpa")
add_library(doki_doki_literature_club STATIC
    src/DdlcLauncher.cpp
    src/DdlcRuntime.cpp
    src/DdlcSession.cpp
)
target_include_directories(doki_doki_literature_club PUBLIC src)
target_link_libraries(doki_doki_literature_club PUBLIC Kotonoha::Engine DdlcFormats Kotonoha::ProviderRPA Kotonoha::Vmrenpy)
set_target_properties(doki_doki_literature_club PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(DokiDoki src/main.cpp)
target_link_libraries(DokiDoki PRIVATE doki_doki_literature_club)
set_target_properties(DokiDoki PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(DokiDoki
    BUNDLE_ID me.hirameki.kotonoha.dokidokiliteratureclub GAME doki-doki-literature-club
    PYTHON_STDLIB "${Python3_STDLIB}"
    PYTHON_VERSION "${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(ANDROID)
    target_compile_definitions(DdlcFormats PRIVATE KOTONOHA_ANDROID_ABI="${ANDROID_ABI}")
endif()
