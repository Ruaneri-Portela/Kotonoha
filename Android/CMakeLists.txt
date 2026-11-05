cmake_minimum_required(VERSION 3.22)

# Global 16KiB page support
if(ANDROID)
    add_link_options("-Wl,-z,max-page-size=16384")
endif()

# Enable Link-Time Optimization (LTO / IPO) for Release builds
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "IPO is not supported: ${ipo_error}")
endif()

# Detectar automaticamente o triplet Android do vcpkg.
# Deve executar antes de project().
if(NOT ANDROID_ABI AND CMAKE_ANDROID_ARCH_ABI)
    set(ANDROID_ABI "${CMAKE_ANDROID_ARCH_ABI}")
endif()

if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(_VCPKG_ANDROID_TRIPLET "arm64-android")
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    if(ANDROID_ARM_NEON)
        set(_VCPKG_ANDROID_TRIPLET "arm-neon-android")
    else()
        set(_VCPKG_ANDROID_TRIPLET "arm-android")
    endif()
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(_VCPKG_ANDROID_TRIPLET "x64-android")
elseif(ANDROID_ABI STREQUAL "x86")
    set(_VCPKG_ANDROID_TRIPLET "x86-android")
else()
    message(FATAL_ERROR
        "Unsupported Android ABI: ${ANDROID_ABI}")
endif()

set(VCPKG_TARGET_TRIPLET
    "${_VCPKG_ANDROID_TRIPLET}"
    CACHE STRING "Android vcpkg triplet" FORCE
)

message(STATUS "Android ABI: ${ANDROID_ABI}")
message(STATUS "vcpkg triplet: ${VCPKG_TARGET_TRIPLET}")

# Each manifest install reconciles its entire installed tree. Sharing it
# between ABIs makes the second configure remove the first ABI's libraries.
if(KOTONOHA_VCPKG_BASE_DIR)
    file(TO_CMAKE_PATH "${KOTONOHA_VCPKG_BASE_DIR}" _vcpkg_base)
    set(VCPKG_INSTALLED_DIR "${_vcpkg_base}/${ANDROID_ABI}"
        CACHE PATH "Per-ABI vcpkg installation" FORCE)
endif()

# The vcpkg Android triplets read the NDK from the environment, while Gradle
# supplies it as a CMake variable (including when Studio is opened from GUI).
if(CMAKE_ANDROID_NDK)
    set(ENV{ANDROID_NDK_HOME} "${CMAKE_ANDROID_NDK}")
elseif(ANDROID_NDK)
    set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK}")
endif()

set(KOTONOHA_ANDROID_GAME "example" CACHE STRING "Game built by this APK")
if(KOTONOHA_ANDROID_GAME STREQUAL "doki-doki-literature-club")
    list(APPEND VCPKG_MANIFEST_FEATURES android-python)
endif()

project(KotonohaAndroid LANGUAGES C CXX)

foreach(game EXAMPLE AMAGAMI ZERO SNOW DDLC SCHOOL_DAYS AIR SUMMER)
    set(KOTONOHA_BUILD_${game} OFF CACHE BOOL "" FORCE)
endforeach()

if(KOTONOHA_ANDROID_GAME STREQUAL "example")
    set(KOTONOHA_BUILD_EXAMPLE ON CACHE BOOL "" FORCE)
elseif(KOTONOHA_ANDROID_GAME STREQUAL "amagami")
    set(KOTONOHA_BUILD_AMAGAMI ON CACHE BOOL "" FORCE)
elseif(KOTONOHA_ANDROID_GAME STREQUAL "zero-no-tsukaima")
    set(KOTONOHA_BUILD_ZERO ON CACHE BOOL "" FORCE)
elseif(KOTONOHA_ANDROID_GAME STREQUAL "snow-radish-vacation")
    set(KOTONOHA_BUILD_SNOW ON CACHE BOOL "" FORCE)
elseif(KOTONOHA_ANDROID_GAME STREQUAL "doki-doki-literature-club")
    set(KOTONOHA_BUILD_DDLC ON CACHE BOOL "" FORCE)
elseif(KOTONOHA_ANDROID_GAME STREQUAL "school-days")
    set(KOTONOHA_BUILD_SCHOOL_DAYS ON CACHE BOOL "" FORCE)
elseif(KOTONOHA_ANDROID_GAME STREQUAL "air")
    set(KOTONOHA_BUILD_AIR ON CACHE BOOL "" FORCE)
elseif(KOTONOHA_ANDROID_GAME STREQUAL "summer-radish-vacation")
    set(KOTONOHA_BUILD_SUMMER ON CACHE BOOL "" FORCE)
else()
    message(FATAL_ERROR "Unknown Android game: ${KOTONOHA_ANDROID_GAME}")
endif()

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory(.. kotonoha)

if(TARGET DdlcFormats)
    target_link_libraries(DdlcFormats PRIVATE Kotonoha::Engine)
endif()
