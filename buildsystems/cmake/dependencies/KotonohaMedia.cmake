# Media dependency discovery. Shared by the engine, without game target definitions.
# --- Bibliotecas Externas (Ass, FFmpeg, JSON-C) ---
find_package(PkgConfig REQUIRED)

if (ANDROID)
    find_path(LIBASS_INCLUDE_DIRS NAMES ass/ass.h
            PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include" NO_DEFAULT_PATH REQUIRED)
    find_package(harfbuzz CONFIG REQUIRED)
    find_package(Freetype CONFIG REQUIRED)
    find_package(Threads REQUIRED)

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(kotonoha_vcpkg_library_dir
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib")
    else ()
        set(kotonoha_vcpkg_library_dir
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
    endif ()

    find_library(KOTONOHA_LIBASS_LIBRARY NAMES ass PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_FRIBIDI_LIBRARY NAMES fribidi PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_UNIBREAK_LIBRARY NAMES unibreak PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_FONTCONFIG_LIBRARY NAMES fontconfig PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_EXPAT_LIBRARY NAMES expat PATHS "${kotonoha_vcpkg_library_dir}" NO_DEFAULT_PATH REQUIRED)

    add_library(kotonoha_ass INTERFACE)
    target_include_directories(kotonoha_ass INTERFACE ${LIBASS_INCLUDE_DIRS})
    target_link_libraries(kotonoha_ass INTERFACE
            "${KOTONOHA_LIBASS_LIBRARY}"
            "${KOTONOHA_FRIBIDI_LIBRARY}"
            "${KOTONOHA_UNIBREAK_LIBRARY}"
            "${KOTONOHA_FONTCONFIG_LIBRARY}"
            "${KOTONOHA_EXPAT_LIBRARY}"
            harfbuzz::harfbuzz
            Freetype::Freetype
            Threads::Threads
            m
    )
    set(kotonoha_ass_target kotonoha_ass)

    find_package(FFMPEG REQUIRED)
    find_package(json-c CONFIG REQUIRED)

    add_library(kotonoha_ffmpeg INTERFACE)
    target_include_directories(kotonoha_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(kotonoha_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})

    set(kotonoha_ffmpeg_target kotonoha_ffmpeg)
    set(kotonoha_json_target json-c::json-c)
elseif (EMSCRIPTEN)
    pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass>=0.17)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
            libavformat libavcodec libavutil libswscale libswresample)
    pkg_check_modules(JSON_C REQUIRED IMPORTED_TARGET json-c>=0.15)
    set(kotonoha_ass_target PkgConfig::LIBASS)
    set(kotonoha_ffmpeg_target PkgConfig::FFMPEG)
    set(kotonoha_json_target PkgConfig::JSON_C)
    # vcpkg's FFmpeg .pc advertises -pthread even though its WASM archives
    # are built without atomics. Emscripten's single-thread stubs suffice.
    set_property(TARGET PkgConfig::FFMPEG PROPERTY INTERFACE_LINK_OPTIONS "")
else ()
    pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass>=0.17)
    set(kotonoha_ass_target PkgConfig::LIBASS)

    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
            libavformat libavcodec libavutil libswscale libswresample)
    set(kotonoha_ffmpeg_target PkgConfig::FFMPEG)

    pkg_check_modules(JSON_C REQUIRED IMPORTED_TARGET json-c>=0.15)
    set(kotonoha_json_target PkgConfig::JSON_C)
endif ()
