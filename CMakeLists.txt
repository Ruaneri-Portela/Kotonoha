cmake_minimum_required(VERSION 3.18)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
            "Minimum macOS version (Sequoia)" FORCE)
endif ()

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

if (APPLE)
    enable_language(OBJC)
endif ()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# Paths
# ==============================================================================
set(KOTONOHA_INCLUDE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

set(KOTONOHA_ASSETS_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

# ==============================================================================
# Main source files
# ==============================================================================
file(GLOB KOTONOHA_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/components/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/windows/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mobile/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/parsers/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/renders/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/specials/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/specials/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/*.c"
)

# ==============================================================================
# System / PkgConfig Dependencies
# ==============================================================================
find_package(PkgConfig REQUIRED)

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

# ==============================================================================
# Dependencies Resolution (Submodules First -> System/vcpkg Fallback)
# ==============================================================================

# --- SDL3 Resolution ---
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3/CMakeLists.txt")
    message(STATUS "Usando submódulo: SDL3")
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/sdl3 EXCLUDE_FROM_ALL)
else ()
    find_package(SDL3 CONFIG QUIET)
    if (NOT SDL3_FOUND AND NOT TARGET SDL3::SDL3)
        message(FATAL_ERROR "SDL3 não encontrado no sistema e o submódulo em third_party/sdl3 está ausente.")
    endif ()
endif ()

# --- SDL3_ttf Resolution ---
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3_ttf/CMakeLists.txt")
    message(STATUS "Usando submódulo: SDL3_ttf")
    set(SDL3TTF_VENDORED ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/sdl3_ttf EXCLUDE_FROM_ALL)
else ()
    find_package(SDL3_ttf CONFIG QUIET)
    if (NOT SDL3_ttf_FOUND AND NOT TARGET SDL3_ttf::SDL3_ttf)
        message(FATAL_ERROR "SDL3_ttf não encontrado no sistema e o submódulo em third_party/sdl3_ttf está ausente.")
    endif ()
endif ()

# --- ImGui Resolution ---
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp")
    message(STATUS "Usando submódulo: ImGui")
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

    if (TARGET SDL3::SDL3-shared)
        target_link_libraries(imgui PUBLIC SDL3::SDL3-shared)
    elseif (TARGET SDL3::SDL3)
        target_link_libraries(imgui PUBLIC SDL3::SDL3)
    else ()
        target_link_libraries(imgui PUBLIC PkgConfig::SDL3)
    endif ()

    add_library(imgui::imgui ALIAS imgui)
else ()
    find_package(imgui CONFIG QUIET)
    if (NOT imgui_FOUND AND NOT TARGET imgui::imgui)
        message(FATAL_ERROR "ImGui não encontrado no sistema e o submódulo em third_party/imgui está ausente.")
    endif ()
endif ()

# Target de link padronizado para SDL3
if (TARGET SDL3::SDL3-shared)
    set(KOTONOHA_SDL3_TARGET SDL3::SDL3-shared)
elseif (TARGET SDL3::SDL3)
    set(KOTONOHA_SDL3_TARGET SDL3::SDL3)
else ()
    set(KOTONOHA_SDL3_TARGET PkgConfig::SDL3)
endif ()

# Target de link padronizado para SDL3_ttf
if (TARGET SDL3_ttf::SDL3_ttf-shared)
    set(KOTONOHA_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf-shared)
elseif (TARGET SDL3_ttf::SDL3_ttf)
    set(KOTONOHA_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
else ()
    set(KOTONOHA_SDL3_TTF_TARGET PkgConfig::SDL3_TTF)
endif ()

# ==============================================================================
# Android
# ==============================================================================
if (ANDROID)
    message(STATUS "Configuração Android | ABI: ${ANDROID_ABI} | Platform: ${ANDROID_PLATFORM}")

    find_package(ZLIB REQUIRED)
    find_package(BZip2 REQUIRED)
    find_package(PNG REQUIRED)
    find_package(expat CONFIG REQUIRED)

    add_link_options("-Wl,-z,max-page-size=16384")

    add_library(SDL3_shared_bundle SHARED src/specials/bundle.cpp)
    target_link_libraries(SDL3_shared_bundle PRIVATE
            "-Wl,--whole-archive" ${KOTONOHA_SDL3_TARGET} "-Wl,--no-whole-archive"
            android log atomic m
    )

    add_library(SDL3_ttf_shared_bundle SHARED src/specials/bundle.cpp)
    target_link_libraries(SDL3_ttf_shared_bundle PRIVATE
            "-Wl,--whole-archive" ${KOTONOHA_SDL3_TTF_TARGET} "-Wl,--no-whole-archive"
            SDL3_shared_bundle android log atomic m
    )

    add_library(Kotonoha SHARED ${KOTONOHA_SOURCES})
    target_include_directories(Kotonoha PRIVATE "${KOTONOHA_INCLUDE_DIR}")
    target_link_libraries(Kotonoha PRIVATE
            SDL3_shared_bundle
            SDL3_ttf_shared_bundle
            PkgConfig::FFMPEG
            PkgConfig::LIBASS
            imgui::imgui
            expat::expat
            android mediandk log atomic m GLESv2
    )
    target_compile_definitions(Kotonoha PRIVATE KOTONOHA_HAS_ANIME4K=1)

# ==============================================================================
# Desktop
# ==============================================================================
else ()
    message(STATUS "Configuração Desktop | Sistema: ${CMAKE_SYSTEM_NAME}")

    if (APPLE)
        list(APPEND KOTONOHA_SOURCES
                src/specials/MetalFX.mm
                src/specials/UpscalingGeneric.cpp
                src/mobile/IOS.mm
        )
    endif ()

    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        list(APPEND KOTONOHA_SOURCES src/specials/Kotonoha.rc)
    endif ()

    if (APPLE)
        add_executable(Kotonoha MACOSX_BUNDLE ${KOTONOHA_SOURCES})
    else ()
        add_executable(Kotonoha ${KOTONOHA_SOURCES})
    endif ()

    target_include_directories(Kotonoha PRIVATE "${KOTONOHA_INCLUDE_DIR}")

    target_link_libraries(Kotonoha PRIVATE
            PkgConfig::FFMPEG
            PkgConfig::LIBASS
            ${KOTONOHA_SDL3_TARGET}
            ${KOTONOHA_SDL3_TTF_TARGET}
            imgui::imgui
    )

    # --------------------------------------------------------------------------
    # Anime4K / OpenGL Configuration
    # --------------------------------------------------------------------------
    find_package(OpenGL QUIET)
    if (OpenGL_FOUND)
        target_compile_definitions(Kotonoha PRIVATE KOTONOHA_HAS_ANIME4K=1)
        target_link_libraries(Kotonoha PRIVATE OpenGL::GL)
    else ()
        message(STATUS "OpenGL não encontrado; Anime4K upscaling desativado")
    endif ()

    if (CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        target_compile_options(Kotonoha PRIVATE -fPIC)
    endif ()

    # ==========================================================================
    # macOS application bundle
    # ==========================================================================
    if (APPLE)
        target_compile_definitions(Kotonoha PRIVATE KOTONOHA_HAS_METALFX=1)
        target_link_libraries(Kotonoha PRIVATE
                "-framework Foundation"
                "-framework CoreFoundation"
                "-framework Metal"
                "-framework MetalFX"
        )

        set_target_properties(Kotonoha PROPERTIES
                MACOSX_BUNDLE TRUE
                MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/specials/Info.plist.in"
                MACOSX_BUNDLE_BUNDLE_NAME "Kotonoha"
                MACOSX_BUNDLE_GUI_IDENTIFIER "me.hirameki.kotonoha"
                MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
                MACOSX_BUNDLE_ICON_FILE "icon.icns"
        )

        set(KOTONOHA_ICON_FILE "${KOTONOHA_ASSETS_DIR}/icon.icns")

        if (NOT EXISTS "${KOTONOHA_ICON_FILE}")
            message(FATAL_ERROR "Ícone macOS não encontrado: ${KOTONOHA_ICON_FILE}")
        endif ()

        set_source_files_properties(
                "${KOTONOHA_ICON_FILE}"
                PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
                HEADER_FILE_ONLY TRUE
        )

        target_sources(Kotonoha PRIVATE "${KOTONOHA_ICON_FILE}")

        if (EXISTS "${KOTONOHA_ASSETS_DIR}")
            file(GLOB_RECURSE KOTONOHA_ASSET_FILES CONFIGURE_DEPENDS
                    "${KOTONOHA_ASSETS_DIR}/*"
            )

            foreach (ASSET_FILE IN LISTS KOTONOHA_ASSET_FILES)
                if (IS_DIRECTORY "${ASSET_FILE}")
                    continue ()
                endif ()

                if (ASSET_FILE STREQUAL KOTONOHA_ICON_FILE)
                    continue ()
                endif ()

                file(RELATIVE_PATH ASSET_RELATIVE_PATH
                        "${KOTONOHA_ASSETS_DIR}"
                        "${ASSET_FILE}"
                )

                get_filename_component(ASSET_RELATIVE_DIRECTORY
                        "${ASSET_RELATIVE_PATH}"
                        DIRECTORY
                )

                if (ASSET_RELATIVE_DIRECTORY)
                    set(ASSET_BUNDLE_LOCATION "Resources/assets/${ASSET_RELATIVE_DIRECTORY}")
                else ()
                    set(ASSET_BUNDLE_LOCATION "Resources/assets")
                endif ()

                set_source_files_properties(
                        "${ASSET_FILE}"
                        PROPERTIES
                        MACOSX_PACKAGE_LOCATION "${ASSET_BUNDLE_LOCATION}"
                        HEADER_FILE_ONLY TRUE
                )

                target_sources(Kotonoha PRIVATE "${ASSET_FILE}")
            endforeach ()
        else ()
            message(WARNING "Diretório de assets não encontrado: ${KOTONOHA_ASSETS_DIR}")
        endif ()
    endif ()
endif ()