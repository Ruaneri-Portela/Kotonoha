cmake_minimum_required(VERSION 3.18)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
        "Minimum macOS version (Sequoia)" FORCE)
endif()

# ==============================================================================
# Android / vcpkg target triplet
# ==============================================================================
if (ANDROID_ABI STREQUAL "arm64-v8a")
    set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "" FORCE)
elseif (ANDROID_ABI STREQUAL "x86_64")
    set(VCPKG_TARGET_TRIPLET "x64-android" CACHE STRING "" FORCE)
elseif (ANDROID_ABI STREQUAL "armeabi-v7a")
    set(VCPKG_TARGET_TRIPLET "arm-android" CACHE STRING "" FORCE)
elseif (ANDROID_ABI STREQUAL "x86")
    set(VCPKG_TARGET_TRIPLET "x86-android" CACHE STRING "" FORCE)
endif ()

# ==============================================================================
# Project
# ==============================================================================
project(Kotonoha VERSION 0.1.0 LANGUAGES C CXX)

option(KOTONOHA_DEV_CHECKPOINTS
    "Enable non-release School Days routing checkpoints and state dumps"
    OFF
)

option(KOTONOHA_ROUTER_TESTS
    "Build standalone School Days routing regression tests"
    OFF
)

option(KOTONOHA_ORS_PARSER_TESTS
    "Build standalone School Days ORS parser fidelity tests"
    OFF
)

option(KOTONOHA_MPV_UPSCALER_RUNTIME_TESTS
    "Build the standalone MPVCompatibleUpscaler OpenGL runtime regression test"
    OFF
)

# ==============================================================================
# Languages and language standards
# ==============================================================================
if (APPLE)
    enable_language(OBJC OBJCXX)
endif ()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/experimental:c11atomics)
endif()

# ==============================================================================
# Paths
# ==============================================================================
set(KOTONOHA_INCLUDE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

set(KOTONOHA_ASSETS_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

set(SCHOOL_DAYS_INCLUDE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/games/school-days/include"
)

# ==============================================================================
# Source roots
# ==============================================================================
set(KOTONOHA_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/Kotonoha")
set(KOTONOHA_RUNTIME_SOURCE_DIR "${KOTONOHA_ENGINE_SOURCE_DIR}/runtime")
set(SCHOOL_DAYS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/games/school-days/src")

# ==============================================================================
# Common dependencies
# ==============================================================================
find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec
        libavformat
        libavutil
        libswscale
        libswresample
)

pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET
        libass
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3/CMakeLists.txt")
    message(STATUS "Using SDL3 submodule")
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/sdl3 EXCLUDE_FROM_ALL)
else()
    find_package(SDL3 CONFIG QUIET)
    if(NOT TARGET SDL3::SDL3 AND NOT TARGET SDL3::SDL3-shared)
        pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3_ttf/CMakeLists.txt")
    message(STATUS "Using SDL3_ttf submodule")
    set(SDL3TTF_VENDORED ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/sdl3_ttf EXCLUDE_FROM_ALL)
else()
    find_package(SDL3_ttf CONFIG QUIET)
    if(NOT TARGET SDL3_ttf::SDL3_ttf AND NOT TARGET SDL3_ttf::SDL3_ttf-shared)
        pkg_check_modules(SDL3_TTF REQUIRED IMPORTED_TARGET sdl3-ttf)
    endif()
endif()

if(TARGET SDL3::SDL3-shared)
    set(KOTONOHA_SDL3_TARGET SDL3::SDL3-shared)
elseif(TARGET SDL3::SDL3)
    set(KOTONOHA_SDL3_TARGET SDL3::SDL3)
else()
    set(KOTONOHA_SDL3_TARGET ${KOTONOHA_SDL3_TARGET})
endif()

if(TARGET SDL3_ttf::SDL3_ttf-shared)
    set(KOTONOHA_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf-shared)
elseif(TARGET SDL3_ttf::SDL3_ttf)
    set(KOTONOHA_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
else()
    set(KOTONOHA_SDL3_TTF_TARGET ${KOTONOHA_SDL3_TTF_TARGET})
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp")
    message(STATUS "Using ImGui submodule")
    add_library(imgui STATIC
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/misc/cpp/imgui_stdlib.cpp
        third_party/imgui/backends/imgui_impl_sdl3.cpp
        third_party/imgui/backends/imgui_impl_sdlrenderer3.cpp
        third_party/imgui/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/misc/cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends"
    )
    target_link_libraries(imgui PUBLIC ${KOTONOHA_SDL3_TARGET})
    add_library(imgui::imgui ALIAS imgui)
else()
    find_package(imgui CONFIG REQUIRED)
endif()

# ==============================================================================
# Engine, runtime library, and SDL entry executable targets
# ==============================================================================
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KotonohaTargets.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KotonohaApp.cmake")

# ==============================================================================
# Target libKotonoha.so para Android
# ==============================================================================
if(ANDROID)
    add_library(Kotonoha SHARED "${CMAKE_CURRENT_SOURCE_DIR}/games/generic/src/Main.cpp")
    set_target_properties(Kotonoha PROPERTIES OUTPUT_NAME Kotonoha)
    target_link_libraries(Kotonoha PRIVATE KotonohaRuntime)
endif()

# ==============================================================================
# Applications (each game owns its CMakeLists.txt and its tests/ folder)
# ==============================================================================
if((KOTONOHA_ROUTER_TESTS OR KOTONOHA_ORS_PARSER_TESTS) AND NOT ANDROID)
    include(CTest)
    set(KOTONOHA_SCHOOL_DAYS_FIXTURE_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        CACHE PATH
        "Optional extracted School Days HQ retail fixture root used by corpus-dependent tests")
endif()

add_subdirectory(games/generic)
add_subdirectory(games/school-days)
add_subdirectory(games/zero-no-tsukaima)
add_subdirectory(games/snow-radish-vacation)
add_subdirectory(games/doki-doki-literature-club)

if((KOTONOHA_ROUTER_TESTS OR KOTONOHA_ORS_PARSER_TESTS) AND NOT ANDROID)
    include_directories("${SCHOOL_DAYS_INCLUDE_DIR}")

    add_executable(KotonohaEngineLinkSmokeTest
        tests/KotonohaEngineLinkSmokeTest.cpp
    )
    target_link_libraries(KotonohaEngineLinkSmokeTest PRIVATE Kotonoha::Engine)
    add_test(NAME KotonohaEngineLinkSmokeTest COMMAND KotonohaEngineLinkSmokeTest)
    if(TARGET ZeroFormatsTest)
        add_test(NAME ZeroFormatsTest COMMAND ZeroFormatsTest)
    endif()
    if(TARGET SnowRadishPackTest)
        add_test(NAME SnowRadishPackTest COMMAND SnowRadishPackTest)
    endif()
    if(TARGET DdlcScriptTest)
        add_test(NAME DdlcScriptTest COMMAND DdlcScriptTest)
    endif()

    add_executable(UpscalingTextureLifecycleTest
        tests/UpscalingTextureLifecycleTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/UpscalingGeneric.cpp
    )
    target_include_directories(UpscalingTextureLifecycleTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR})
    target_link_libraries(UpscalingTextureLifecycleTest PRIVATE
        ${KOTONOHA_SDL3_TARGET})
    set_target_properties(UpscalingTextureLifecycleTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME UpscalingTextureLifecycleTest
        COMMAND UpscalingTextureLifecycleTest)

    add_executable(OrsParserFidelityTest
        tests/OrsParserFidelityTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/Ors.c
    )
    target_include_directories(OrsParserFidelityTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )
    target_link_libraries(OrsParserFidelityTest PRIVATE ${KOTONOHA_SDL3_TARGET})
    set_target_properties(OrsParserFidelityTest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME OrsParserFidelityTest COMMAND OrsParserFidelityTest)

    add_executable(FFmpegInputSourceTest
        tests/FFmpegInputSourceTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Demuxer.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/FFmpeg.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/VoicePcm.c
    )
    target_include_directories(FFmpegInputSourceTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(FFmpegInputSourceTest PRIVATE
        ${KOTONOHA_SDL3_TARGET} PkgConfig::FFMPEG)
    set_target_properties(FFmpegInputSourceTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME FFmpegInputSourceTest COMMAND FFmpegInputSourceTest)
endif()

if(KOTONOHA_MPV_UPSCALER_RUNTIME_TESTS AND NOT ANDROID)
    include(CTest)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        message(FATAL_ERROR
            "KOTONOHA_MPV_UPSCALER_RUNTIME_TESTS currently requires Windows")
    endif()
    if(NOT OpenGL_FOUND)
        message(FATAL_ERROR
            "KOTONOHA_MPV_UPSCALER_RUNTIME_TESTS requires OpenGL")
    endif()

    add_executable(MPVCompatibleUpscalerRuntimeTest
        tests/MPVCompatibleUpscalerRuntimeTest.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/MPVCompatibleUpscaler.cpp
        ${KOTONOHA_ENGINE_SOURCE_DIR}/specials/OpenGL.c
        ${KOTONOHA_ENGINE_SOURCE_DIR}/utils/Log.c
    )
    target_include_directories(MPVCompatibleUpscalerRuntimeTest PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
    )
    target_compile_definitions(MPVCompatibleUpscalerRuntimeTest PRIVATE
        KOTONOHA_HAS_MPV_UPSCALER=1
    )
    target_link_libraries(MPVCompatibleUpscalerRuntimeTest PRIVATE
        ${KOTONOHA_SDL3_TARGET}
        OpenGL::GL
    )
    set_target_properties(MPVCompatibleUpscalerRuntimeTest PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    add_test(NAME MPVCompatibleUpscalerRuntimeTest COMMAND MPVCompatibleUpscalerRuntimeTest)
endif()
