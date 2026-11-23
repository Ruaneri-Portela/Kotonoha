cmake_minimum_required(VERSION 3.16)

project(DokiDokiLiteratureClub VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

find_package(ZLIB REQUIRED)
if(ANDROID)
    if(KOTONOHA_HAS_PYTHON_DEVELOPMENT)
        find_package(Python3 REQUIRED COMPONENTS Development)
    endif()
elseif(NOT KOTONOHA_HAS_PYTHON_DEVELOPMENT)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
endif()

if(KOTONOHA_USE_PYTHON_STUBS)
    set(DDLC_SOURCES
        src/Archive.cpp
        src/Display.cpp
        src/Script.cpp
        src/Value.cpp
    )
else()
    set(DDLC_SOURCES
        src/Archive.cpp
        src/Display.cpp
        src/Script.cpp
        src/Value.cpp
    )
endif()

add_library(DdlcFormats STATIC ${DDLC_SOURCES})
target_include_directories(DdlcFormats PUBLIC include)
target_include_directories(DdlcFormats PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/renpy/src")
target_compile_features(DdlcFormats PUBLIC cxx_std_20)
# Archive.cpp uses Kotonoha::Filesystem (including SDLFileSource/readAll).
# This must be a direct dependency of DdlcFormats, rather than inherited
# only from its consumer: GNU ld (MinGW and ELF) scans link archives in
# order, and otherwise places the Kotonoha DLL import library too early.
target_link_libraries(DdlcFormats PUBLIC KotonohaEngine)

target_link_libraries(DdlcFormats PUBLIC SDL3::SDL3 ZLIB::ZLIB)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/renpy/CMakeLists.txt")
# Replay reads live VM state, so compile it once in the VM archive rather
# than making the format/parser library depend on the VM backwards.
target_sources(kotonoha_vm_renpy PRIVATE src/Replay.cpp)
target_include_directories(DdlcFormats PUBLIC
    $<TARGET_PROPERTY:kotonoha_vm_renpy,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(DdlcFormats PROPERTIES CXX_EXTENSIONS OFF)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/rpa"
    "${CMAKE_CURRENT_BINARY_DIR}/providers/rpa")

add_library(doki_doki_literature_club STATIC
    src/DdlcLauncher.cpp
    src/DdlcRuntime.cpp
    src/DdlcSession.cpp
)
target_include_directories(doki_doki_literature_club PUBLIC src)
target_link_libraries(doki_doki_literature_club PUBLIC
    KotonohaEngine
    DdlcFormats
    kotonoha_provider_rpa
    kotonoha_vm_renpy
)
set_target_properties(doki_doki_literature_club PROPERTIES CXX_EXTENSIONS OFF)

if(KOTONOHA_BUILD_DDLC OR CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    kotonoha_add_app(DokiDoki src/main.cpp)
    target_link_libraries(DokiDoki PRIVATE doki_doki_literature_club)
    set_target_properties(DokiDoki PROPERTIES CXX_EXTENSIONS OFF)
    kotonoha_configure_app(DokiDoki
        BUNDLE_ID me.hirameki.kotonoha.dokidokiliteratureclub GAME doki-doki-literature-club
        PYTHON_STDLIB "${KOTONOHA_RENPY_PYTHON_STDLIB}"
        PYTHON_VERSION "${KOTONOHA_RENPY_PYTHON_VERSION}")
endif()

if(BUILD_TESTING)
add_subdirectory(tests)
endif()

if(ANDROID)
    target_compile_definitions(DdlcFormats PRIVATE KOTONOHA_ANDROID_ABI="${ANDROID_ABI}")
endif()
