cmake_minimum_required(VERSION 3.18)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
            "Minimum macOS version (Sequoia)" FORCE)
endif ()

# ==============================================================================
# Android / vcpkg target triplet
#
# O Android Gradle Plugin/NDK define ANDROID_ABI. O triplet é definido antes
# de resolver dependências vcpkg para corresponder à ABI que está sendo gerada.
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

# ==============================================================================
# Languages and language standards
# ==============================================================================
if (APPLE)
    # Necessário para arquivos Objective-C / Objective-C++ usados em macOS.
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
#
# CONFIGURE_DEPENDS faz o CMake reconfigurar quando arquivos compatíveis são
# criados/removidos dessas pastas.
# ==============================================================================
file(GLOB KOTONOHA_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/components/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mobile/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/parsers/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/renders/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/specials/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/specials/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/*.c"
)


# ==============================================================================
# Common dependencies
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

find_package(imgui CONFIG REQUIRED)


# ==============================================================================
# Android
# ==============================================================================
if (ANDROID)
    message(STATUS
            "Configuração Android | ABI: ${ANDROID_ABI} | Platform: ${ANDROID_PLATFORM}"
    )

    unset(harfbuzz_DIR CACHE)
    unset(harfbuzz_ROOT CACHE)
    unset(Freetype_DIR CACHE)
    unset(plutosvg_DIR CACHE)
    unset(plutovg_DIR CACHE)

    find_package(ZLIB REQUIRED)
    find_package(BZip2 REQUIRED)
    find_package(PNG REQUIRED)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_ttf CONFIG REQUIRED)
    find_package(expat CONFIG REQUIRED)

    # Exigência de compatibilidade com dispositivos Android usando páginas de 16 KB.
    add_link_options(
            "-Wl,-z,max-page-size=16384"
    )

    # --------------------------------------------------------------------------
    # SDL3 shared wrapper
    #
    # Empacota a SDL3 estática como libSDL3.so para consumo pelo Android.
    # --------------------------------------------------------------------------
    add_library(SDL3 SHARED
            src/specials/bundle.cpp
    )

    target_link_libraries(SDL3 PRIVATE
            "-Wl,--whole-archive"
            SDL3::SDL3
            "-Wl,--no-whole-archive"
            android
            log
            atomic
            m
    )

    # --------------------------------------------------------------------------
    # SDL3_ttf shared wrapper
    #
    # Empacota SDL3_ttf estática como libSDL3_ttf.so.
    # --------------------------------------------------------------------------
    add_library(SDL3_ttf SHARED
            src/specials/bundle.cpp
    )

    target_link_libraries(SDL3_ttf PRIVATE
            "-Wl,--whole-archive"
            SDL3_ttf::SDL3_ttf
            "-Wl,--no-whole-archive"
            SDL3
            android
            log
            atomic
            m
    )

    # --------------------------------------------------------------------------
    # Main Android shared library
    # --------------------------------------------------------------------------
    add_library(Kotonoha SHARED
            ${KOTONOHA_SOURCES}
    )

    target_include_directories(Kotonoha PRIVATE
            "${KOTONOHA_INCLUDE_DIR}"
    )

    target_link_libraries(Kotonoha PRIVATE
            SDL3
            SDL3_ttf
            PkgConfig::FFMPEG
            PkgConfig::LIBASS
            imgui::imgui
            expat::expat
            android
            mediandk
            log
            atomic
            m
            GLESv2
    )
    target_compile_definitions(Kotonoha PRIVATE
            KOTONOHA_HAS_ANIME4K=1
    )

    # ==============================================================================
    # Desktop
    # ==============================================================================
else ()
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    pkg_check_modules(SDL3_TTF REQUIRED IMPORTED_TARGET sdl3-ttf)
    message(STATUS "Configuração Desktop | Sistema: ${CMAKE_SYSTEM_NAME}")

    if (APPLE)
        list(APPEND KOTONOHA_SOURCES
                src/specials/MetalFx.mm
                src/specials/UpscalingGeneric.cpp
                src/mobile/IOS.mm
        )
    endif ()

    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        list(APPEND KOTONOHA_SOURCES
                src/specials/Kotonoha.rc
        )
    endif ()

    # --------------------------------------------------------------------------
    # Main desktop executable
    # --------------------------------------------------------------------------
    if (APPLE)
        # Gera Kotonoha.app no macOS.
        add_executable(Kotonoha MACOSX_BUNDLE
                ${KOTONOHA_SOURCES}
        )
    else ()
        add_executable(Kotonoha
                ${KOTONOHA_SOURCES}
        )
    endif ()

    target_include_directories(Kotonoha PRIVATE
            "${KOTONOHA_INCLUDE_DIR}"
    )

    target_link_libraries(Kotonoha PRIVATE
            PkgConfig::FFMPEG
            PkgConfig::LIBASS
            PkgConfig::SDL3
            PkgConfig::SDL3_TTF
            imgui::imgui
    )

    # --------------------------------------------------------------------------
    # Anime4K / OpenGL Configuration (Desktop: Linux, Windows, macOS, etc.)
    # --------------------------------------------------------------------------
    find_package(OpenGL QUIET)
    if (OpenGL_FOUND)
        target_compile_definitions(Kotonoha PRIVATE
                KOTONOHA_HAS_ANIME4K=1
        )
        target_link_libraries(Kotonoha PRIVATE
                OpenGL::GL
        )
        add_custom_command(TARGET Kotonoha POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${KOTONOHA_ASSETS_DIR}/Anime4K"
                "$<TARGET_FILE_DIR:Kotonoha>/assets/Anime4K"
                VERBATIM
        )
    else ()
        message(STATUS
                "OpenGL não encontrado; Anime4K upscaling desativado"
        )
    endif ()

    # --------------------------------------------------------------------------
    # Suporte específico para Haiku
    # --------------------------------------------------------------------------
    if (CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        target_compile_options(Kotonoha PRIVATE
                -fPIC
        )
    endif ()

    # ==========================================================================
    # macOS application bundle
    # ==========================================================================
    if (APPLE)
        target_compile_definitions(Kotonoha PRIVATE
                KOTONOHA_HAS_METALFX=1
        )

        target_link_libraries(Kotonoha PRIVATE
                "-framework Foundation"
                "-framework CoreFoundation"
                "-framework Metal"
                "-framework MetalFX"
        )

        # ----------------------------------------------------------------------
        # Bundle metadata
        # ----------------------------------------------------------------------
        set_target_properties(Kotonoha PROPERTIES
                MACOSX_BUNDLE TRUE

                MACOSX_BUNDLE_INFO_PLIST
                "${CMAKE_CURRENT_SOURCE_DIR}/src/specials/Info.plist.in"

                MACOSX_BUNDLE_BUNDLE_NAME
                "Kotonoha"

                MACOSX_BUNDLE_GUI_IDENTIFIER
                "me.hirameki.kotonoha"

                MACOSX_BUNDLE_BUNDLE_VERSION
                "${PROJECT_VERSION}"

                MACOSX_BUNDLE_SHORT_VERSION_STRING

                "${PROJECT_VERSION}"

                # Nome do arquivo que o Info.plist referencia.
                MACOSX_BUNDLE_ICON_FILE
                "icon.icns"
        )

        # ----------------------------------------------------------------------
        # Application icon
        #
        # assets/icon.icns -> Kotonoha.app/Contents/Resources/icon.icns
        # ----------------------------------------------------------------------
        set(KOTONOHA_ICON_FILE
                "${KOTONOHA_ASSETS_DIR}/icon.icns"
        )

        if (NOT EXISTS "${KOTONOHA_ICON_FILE}")
            message(FATAL_ERROR
                    "Ícone macOS não encontrado: ${KOTONOHA_ICON_FILE}"
            )
        endif ()

        set_source_files_properties(
                "${KOTONOHA_ICON_FILE}"
                PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
                HEADER_FILE_ONLY TRUE
        )

        target_sources(Kotonoha PRIVATE
                "${KOTONOHA_ICON_FILE}"
        )

        # ----------------------------------------------------------------------
        # Application assets
        #
        # Preserva a estrutura:
        #
        # assets/file.ext
        #   -> Kotonoha.app/Contents/Resources/assets/file.ext
        #
        # assets/subdir/file.ext
        #   -> Kotonoha.app/Contents/Resources/assets/subdir/file.ext
        #
        # icon.icns fica exclusivamente em Contents/Resources, pois é o ícone
        # do app e não precisa ser duplicado em Contents/Resources/assets.
        # ----------------------------------------------------------------------
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
                    set(ASSET_BUNDLE_LOCATION
                            "Resources/assets/${ASSET_RELATIVE_DIRECTORY}"
                    )
                else ()
                    set(ASSET_BUNDLE_LOCATION
                            "Resources/assets"
                    )
                endif ()

                set_source_files_properties(
                        "${ASSET_FILE}"
                        PROPERTIES
                        MACOSX_PACKAGE_LOCATION

                        "${ASSET_BUNDLE_LOCATION}"

                        # Impede que CMake tente compilar assets como fonte.
                        HEADER_FILE_ONLY TRUE
                )

                target_sources(Kotonoha PRIVATE
                        "${ASSET_FILE}"
                )
            endforeach ()
        else ()
            message(WARNING
                    "Diretório de assets não encontrado: ${KOTONOHA_ASSETS_DIR}"
            )
        endif ()
    endif ()
endif ()