cmake_minimum_required(VERSION 3.18)

# ==============================================================================
# VCPKG & Toolchain Configuration
# ==============================================================================
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "" FORCE)
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(VCPKG_TARGET_TRIPLET "x64-android" CACHE STRING "" FORCE)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(VCPKG_TARGET_TRIPLET "arm-android" CACHE STRING "" FORCE)
elseif(ANDROID_ABI STREQUAL "x86")
    set(VCPKG_TARGET_TRIPLET "x86-android" CACHE STRING "" FORCE)
endif()

# ==============================================================================
# Project Declaration & Language Standards
# ==============================================================================
project(Kotonoha VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# Source Files
# ==============================================================================
set(KOTONOHA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

file(GLOB SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*.c
    src/components/*.cpp
    src/mobile/*.cpp
    src/parsers/*.c
    src/renders/*.c
    src/utils/*.c
)

# ==============================================================================
# Dependencies — all via pkg-config
# ==============================================================================
find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
    libswresample
)

pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(SDL3_TTF REQUIRED IMPORTED_TARGET sdl3-ttf)

find_package(imgui CONFIG REQUIRED)

if(ANDROID)
    pkg_check_modules(EXPAT REQUIRED IMPORTED_TARGET expat)
endif()

# ==============================================================================
# Target Construction: Android (Shared Library) vs Desktop (Executable)
# ==============================================================================
if(ANDROID)
    message(STATUS "Configuração Android | ABI: ${ANDROID_ABI} | Platform: ${ANDROID_PLATFORM}")

    # Suporte a alinhamento de páginas de 16 KB para Android 15+ (API 35+)
    add_link_options("-Wl,-z,max-page-size=16384")

    # Wrapper SDL3
    add_library(SDL3 SHARED bundle.cpp)
    target_link_libraries(SDL3 PRIVATE
        "-Wl,--whole-archive"
        PkgConfig::SDL3
        "-Wl,--no-whole-archive"
        android log atomic m
    )

    # Wrapper SDL3_ttf
    add_library(SDL3_ttf SHARED bundle.cpp)
    target_link_libraries(SDL3_ttf PRIVATE
        "-Wl,--whole-archive"
        PkgConfig::SDL3_TTF
        "-Wl,--no-whole-archive"
        SDL3
        android log atomic m
    )

    # Library principal
    add_library(Kotonoha SHARED ${SOURCES})

    target_include_directories(Kotonoha PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )

    target_link_libraries(Kotonoha PRIVATE
        SDL3
        SDL3_ttf
        PkgConfig::FFMPEG
        PkgConfig::LIBASS
        PkgConfig::EXPAT
        imgui::imgui
        android
        mediandk
        log
        atomic
        m
    )

else()
    list(FILTER SOURCES EXCLUDE REGEX "/LibIconvCompat\\.c$")
    message(STATUS "Configuração Desktop | Sistema: ${CMAKE_SYSTEM_NAME}")

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        list(APPEND SOURCES src/Kotonoha.rc)
    endif()

    add_executable(Kotonoha ${SOURCES})

    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        target_compile_options(Kotonoha PRIVATE -fPIC)
    endif()

    target_include_directories(Kotonoha PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )

    target_link_libraries(Kotonoha PRIVATE
        PkgConfig::FFMPEG
        PkgConfig::LIBASS
        PkgConfig::SDL3
        PkgConfig::SDL3_TTF
        imgui::imgui
    )
endif()

# ==============================================================================
# Unit Testing
# ==============================================================================
include(CTest)

if(BUILD_TESTING AND NOT ANDROID)
    add_executable(OrsParserFidelityTest
        tests/OrsParserFidelityTest.cpp
        src/parsers/Ors.c
    )

    target_include_directories(OrsParserFidelityTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )

    target_link_libraries(OrsParserFidelityTest PRIVATE
        PkgConfig::SDL3
    )

    add_test(NAME OrsParserFidelityTest COMMAND OrsParserFidelityTest)
endif()