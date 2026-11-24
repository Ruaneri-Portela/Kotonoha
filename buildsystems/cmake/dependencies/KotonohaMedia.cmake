# Media dependency discovery (FFmpeg, libass, json-c). Shared by the engine
# and game targets. Resolution follows KOTONOHA_DEPS_PROVIDER:
#   VCPKG  - native vcpkg triplet (MSVC CMake configs) or vcpkg pkg-config;
#   SYSTEM - Homebrew, MSYS2 UCRT or apt through pkg-config;
#   SUBMODULES/AUTO - media deps always come from the system when vcpkg is
#                     inactive (there are no media submodules in this repo).
include("${CMAKE_CURRENT_LIST_DIR}/KotonohaDepsProvider.cmake")

if (ANDROID)
    # Android keeps its per-ABI vcpkg installation and explicit import libs.
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
elseif (KOTONOHA_MEDIA_PROVIDER_RESOLVED STREQUAL "VCPKG" AND MSVC)
    # Native vcpkg triplet: libass has no portable config package, FFmpeg
    # uses the vcpkg FindFFMPEG wrapper and json-c exports its CMake target.
    if (NOT VCPKG_INSTALLED_DIR OR NOT VCPKG_TARGET_TRIPLET)
        message(FATAL_ERROR
            "[Kotonoha] VCPKG media provider requires a vcpkg toolchain and "
            "target triplet (for example x64-windows). Set "
            "CMAKE_TOOLCHAIN_FILE before the first project() call.")
    endif ()
    set(_kotonoha_native_vcpkg
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if (NOT IS_DIRECTORY "${_kotonoha_native_vcpkg}/include")
        message(FATAL_ERROR
            "[Kotonoha] Missing vcpkg headers: "
            "${_kotonoha_native_vcpkg}/include. Install dependencies "
            "for ${VCPKG_TARGET_TRIPLET}.")
    endif ()

    find_path(KOTONOHA_MSVC_LIBASS_INCLUDE_DIR NAMES ass/ass.h
        PATHS "${_kotonoha_native_vcpkg}/include"
        NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_MSVC_LIBASS_RELEASE NAMES ass libass
        PATHS "${_kotonoha_native_vcpkg}/lib"
        NO_DEFAULT_PATH REQUIRED)
    find_library(KOTONOHA_MSVC_LIBASS_DEBUG NAMES ass libass
        PATHS "${_kotonoha_native_vcpkg}/debug/lib"
        NO_DEFAULT_PATH REQUIRED)
    add_library(kotonoha_ass INTERFACE)
    target_include_directories(kotonoha_ass INTERFACE
        "${KOTONOHA_MSVC_LIBASS_INCLUDE_DIR}")
    target_link_libraries(kotonoha_ass INTERFACE
        "$<IF:$<CONFIG:Debug>,${KOTONOHA_MSVC_LIBASS_DEBUG},${KOTONOHA_MSVC_LIBASS_RELEASE}>")
    set(kotonoha_ass_target kotonoha_ass)

    find_package(FFMPEG REQUIRED)
    if (NOT FFMPEG_LIBRARIES OR NOT FFMPEG_INCLUDE_DIRS)
        message(FATAL_ERROR
            "[Kotonoha] vcpkg FFmpeg package was not resolved.")
    endif ()

    # Reject stale paths that would mix another ABI into a vcpkg build.
    file(TO_CMAKE_PATH "${_kotonoha_native_vcpkg}" _vcpkg_native_path)
    string(TOLOWER "${_vcpkg_native_path}/" _vcpkg_native_path_lower)
    foreach (_candidate IN LISTS FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARY_DIRS)
        file(TO_CMAKE_PATH "${_candidate}" _candidate_path)
        string(TOLOWER "${_candidate_path}/" _candidate_path_lower)
        string(FIND "${_candidate_path_lower}" "${_vcpkg_native_path_lower}" _position)
        if (NOT _position EQUAL 0)
            message(FATAL_ERROR
                "[Kotonoha] Dependency outside the vcpkg triplet: "
                "${_candidate}. Remove stale CMake cache or configure "
                "a new build directory.")
        endif ()
    endforeach ()
    foreach (_candidate IN LISTS FFMPEG_LIBRARIES)
        if (IS_ABSOLUTE "${_candidate}")
            file(TO_CMAKE_PATH "${_candidate}" _candidate_path)
            string(TOLOWER "${_candidate_path}/" _candidate_path_lower)
            string(FIND "${_candidate_path_lower}" "${_vcpkg_native_path_lower}" _position)
            if (NOT _position EQUAL 0)
                message(FATAL_ERROR
                    "[Kotonoha] FFmpeg library is not from vcpkg: ${_candidate}")
            endif ()
        endif ()
    endforeach ()
    add_library(kotonoha_ffmpeg INTERFACE)
    target_include_directories(kotonoha_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
    if (FFMPEG_LIBRARY_DIRS)
        target_link_directories(kotonoha_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
    endif ()
    target_link_libraries(kotonoha_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
    set(kotonoha_ffmpeg_target kotonoha_ffmpeg)

    find_package(json-c CONFIG REQUIRED
        PATHS "${_kotonoha_native_vcpkg}" NO_DEFAULT_PATH)
    if (NOT TARGET json-c::json-c)
        message(FATAL_ERROR "[Kotonoha] vcpkg json-c::json-c target not found")
    endif ()
    set(kotonoha_json_target json-c::json-c)
else ()
    # pkg-config path: system packages on Apple/Linux/MSYS2, and vcpkg's
    # own .pc files when the vcpkg provider is selected there.
    find_package(PkgConfig REQUIRED)
    if (KOTONOHA_MEDIA_PROVIDER_RESOLVED STREQUAL "VCPKG")
        set(_kotonoha_media_pc_prefix
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        if (NOT IS_DIRECTORY "${_kotonoha_media_pc_prefix}")
            message(FATAL_ERROR
                "[Kotonoha] Missing vcpkg installed directory: "
                "${_kotonoha_media_pc_prefix}. Install the manifest "
                "dependencies before configuring.")
        endif ()
        set(ENV{PKG_CONFIG_PATH}
            "${_kotonoha_media_pc_prefix}/lib/pkgconfig:${_kotonoha_media_pc_prefix}/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")
        # Cached pkg-config results can preempt the vcpkg .pc files when a
        # build tree is switched from SYSTEM to VCPKG.
        get_cmake_property(_kotonoha_media_cache_vars CACHE_VARIABLES)
        foreach (_kotonoha_media_cache_var IN LISTS _kotonoha_media_cache_vars)
            if (_kotonoha_media_cache_var MATCHES "^(LIBASS_|FFMPEG_|JSON_C_|PC_LIBASS_|PC_JSON_C_|pkgcfg_lib_PC_LIBASS_|pkgcfg_lib_PC_JSON_C_)")
                unset(${_kotonoha_media_cache_var} CACHE)
            endif ()
        endforeach ()
    endif ()

    if (EMSCRIPTEN)
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
endif ()
