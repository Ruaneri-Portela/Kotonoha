cmake_minimum_required(VERSION 3.16)

project(Kotonoha VERSION 0.1.0 LANGUAGES CXX)
include(cmake/MSVC.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)

# Android 15+ devices may use 16 KiB memory pages.  Keep every Kotonoha shared
# object aligned for that loader contract instead of relying on the toolchain's
# older 4 KiB default.
if(ANDROID)
    add_link_options("-Wl,-z,max-page-size=16384")
endif()

option(KOTONOHA_BUILD_EXAMPLE "Build the Kotonoha example game" ON)
option(KOTONOHA_BUILD_AMAGAMI "Build the Amagami (PS2) port in development" ON)
option(KOTONOHA_BUILD_ZERO "Build the Zero no Tsukaima (PS2) port" ON)
option(KOTONOHA_BUILD_SNOW "Build the Snow Radish Vacation!! port" ON)
option(KOTONOHA_BUILD_SUMMER "Build the Summer Radish Vacation!! port" ON)
option(KOTONOHA_BUILD_DDLC "Build the Doki Doki Literature Club! port" ON)
option(KOTONOHA_BUILD_SCHOOL_DAYS "Build the School Days port" ON)
option(KOTONOHA_DEV_CONTROL "Enable local School Days automation bridge" OFF)

option(KOTONOHA_BUILD_AIR "Build the AIR Standard Edition port" ON)
option(KOTONOHA_BUILD_MACOS_BUNDLES "Build macOS application bundles" ON)

add_subdirectory(lib)

if(KOTONOHA_BUILD_EXAMPLE)
    add_subdirectory(games/example)
endif()
if(KOTONOHA_BUILD_ZERO)
    add_subdirectory(games/zero-no-tsukaima)
endif()
if(KOTONOHA_BUILD_SNOW)
    add_subdirectory(games/snow-radish-vacation)
endif()
if(KOTONOHA_BUILD_DDLC)
    add_subdirectory(games/doki-doki-literature-club)
endif()
if(KOTONOHA_BUILD_SCHOOL_DAYS)
    add_subdirectory(games/school-days)
endif()
if(KOTONOHA_BUILD_SUMMER)
    add_subdirectory(games/summer-radish-vacation)
endif()

if(KOTONOHA_BUILD_AIR)
    add_subdirectory(games/air)
endif()

if(KOTONOHA_BUILD_AMAGAMI)
    add_subdirectory(games/amagami)
endif()
