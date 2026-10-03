cmake_minimum_required(VERSION 3.18)

project(Kotonoha VERSION 0.1.0 LANGUAGES C CXX)

option(KOTONOHA_DEV_CHECKPOINTS
    "Enable non-release School Days routing checkpoints and state dumps"
    OFF
)

set(KOTONOHA_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

file(GLOB SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*.c
    src/components/*.cpp
    src/parsers/*.c
    src/renders/*.c
    src/utils/*.c
)

find_package(PkgConfig REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale libswresample)
pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass)

if(ANDROID)

    message(STATUS "Configuração Android")
    message(STATUS "ANDROID_ABI=${ANDROID_ABI}")
    message(STATUS "ANDROID_PLATFORM=${ANDROID_PLATFORM}")

    find_package(expat CONFIG REQUIRED)

    add_library(SDL3 SHARED
        bundle.cpp
    )

    target_link_libraries(SDL3 PRIVATE
        "-Wl,--whole-archive"
        SDL3::SDL3-static
        "-Wl,--no-whole-archive"

        android
        log
        atomic
        m
    )
    add_library(SDL3_ttf SHARED
        bundle.cpp
    )

    target_link_libraries(SDL3_ttf PRIVATE
        "-Wl,--whole-archive"
        SDL3_ttf::SDL3_ttf-static
        "-Wl,--no-whole-archive"

        SDL3

        android
        log
        atomic
        m
    )

    add_library(Kotonoha SHARED
        ${SOURCES}
    )

    target_include_directories(Kotonoha PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
        ${LIBASS_INCLUDE_DIRS}
    )

    target_link_directories(Kotonoha PRIVATE
        ${FFMPEG_LIBRARY_DIRS} 
        ${LIBASS_LIBRARY_DIRS}
    )

    target_link_libraries(Kotonoha PRIVATE
        SDL3
        SDL3_ttf

        ${FFMPEG_LIBRARIES}
        ${LIBASS_LIBRARIES}
        imgui::imgui
        expat::expat

        android
        mediandk
        log
        atomic
        m
    )

else()

    message(STATUS "Configuração desktop")

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        message("Configuração para Windows")
        list(APPEND SOURCES src/Kotonoha.rc)
    endif()

    add_executable(Kotonoha ${SOURCES})

    target_link_directories(Kotonoha PRIVATE
        ${FFMPEG_LIBRARY_DIRS} 
        ${LIBASS_LIBRARY_DIRS}
        ${SDL3_LIBRARY_DIRS}
        ${SDL3_TTF_LIBRARY_DIRS}
        ${IMGUI_LIBRARY_DIRS}
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        message("Configuração para Haiku")
        target_compile_options(Kotonoha PRIVATE -fPIC)
    endif()

    target_include_directories(Kotonoha PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
        ${LIBASS_INCLUDE_DIRS}
        ${SDL3_INCLUDE_DIRS}
        ${SDL3_TTF_INCLUDE_DIRS}
    )

    target_link_libraries(Kotonoha PRIVATE
        PkgConfig::FFMPEG
        PkgConfig::LIBASS
        SDL3::SDL3 
        SDL3_ttf::SDL3_ttf 
        imgui::imgui
    )
endif()

if(KOTONOHA_DEV_CHECKPOINTS)
    target_compile_definitions(Kotonoha PRIVATE KOTONOHA_DEV_CHECKPOINTS=1)
    message(STATUS "KOTONOHA_DEV_CHECKPOINTS=ON (development-only routing helpers)")
endif()
