# Android/vcpkg selection must run before project().
include_guard(GLOBAL)

if (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    if (NOT ANDROID_ABI AND CMAKE_ANDROID_ARCH_ABI)
        set(ANDROID_ABI "${CMAKE_ANDROID_ARCH_ABI}")
    endif ()

    if (ANDROID_ABI STREQUAL "arm64-v8a")
        set(_VCPKG_ANDROID_TRIPLET "arm64-android")
    elseif (ANDROID_ABI STREQUAL "armeabi-v7a")
        if (ANDROID_ARM_NEON)
            set(_VCPKG_ANDROID_TRIPLET "arm-neon-android")
        else ()
            set(_VCPKG_ANDROID_TRIPLET "arm-android")
        endif ()
    elseif (ANDROID_ABI STREQUAL "x86_64")
        set(_VCPKG_ANDROID_TRIPLET "x64-android")
    elseif (ANDROID_ABI STREQUAL "x86")
        set(_VCPKG_ANDROID_TRIPLET "x86-android")
    else ()
        message(FATAL_ERROR "[Kotonoha] Unsupported Android ABI: ${ANDROID_ABI}")
    endif ()

    set(VCPKG_TARGET_TRIPLET "${_VCPKG_ANDROID_TRIPLET}"
            CACHE STRING "Android vcpkg triplet" FORCE)

    if (KOTONOHA_VCPKG_BASE_DIR)
        file(TO_CMAKE_PATH "${KOTONOHA_VCPKG_BASE_DIR}" _vcpkg_base)
        set(VCPKG_INSTALLED_DIR "${_vcpkg_base}/${ANDROID_ABI}"
                CACHE PATH "Per-ABI vcpkg installation" FORCE)
    endif ()

    if (CMAKE_ANDROID_NDK)
        set(ENV{ANDROID_NDK_HOME} "${CMAKE_ANDROID_NDK}")
    elseif (ANDROID_NDK)
        set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK}")
    endif ()

    if (NOT DEFINED KOTONOHA_BUILD_GAME)
        set(KOTONOHA_BUILD_GAME "example" CACHE STRING "Game built by this Android APK")
    endif ()

    # vcpkg manifest features must be declared before project().
    if (KOTONOHA_BUILD_GAME STREQUAL "doki-doki-literature-club")
        list(APPEND VCPKG_MANIFEST_FEATURES android-python)
        list(REMOVE_DUPLICATES VCPKG_MANIFEST_FEATURES)
    endif ()
endif ()
