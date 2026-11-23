# Media dependency discovery. Shared by the engine, without game target definitions.
# --- Bibliotecas Externas (Ass, FFmpeg, JSON-C) ---
# MSVC uses native vcpkg packages, never the host pkg-config executable.
if(NOT MSVC)
    find_package(PkgConfig REQUIRED)
endif()

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
elseif (MSVC)
    # MSVC must never consume pkg-config metadata or binaries built for
    # MSYS2/MinGW. These three dependencies are provided by native vcpkg.
    if(NOT VCPKG_INSTALLED_DIR OR NOT VCPKG_TARGET_TRIPLET OR
       VCPKG_TARGET_TRIPLET MATCHES "[Mm][Ii][Nn][Gg][Ww]")
        message(FATAL_ERROR
            "[Kotonoha] MSVC requires a native vcpkg triplet, for example "
            "x64-windows, and cannot use MSYS2/MinGW libraries. Set "
            "CMAKE_TOOLCHAIN_FILE before the first project() call.")
    endif()
    set(_kotonoha_native_vcpkg
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(NOT IS_DIRECTORY "${_kotonoha_native_vcpkg}/include")
        message(FATAL_ERROR
            "[Kotonoha] Missing native vcpkg headers: "
            "${_kotonoha_native_vcpkg}/include. Install dependencies "
            "for ${VCPKG_TARGET_TRIPLET}.")
    endif()

    # libass has no portable CMake config package. Search only the
    # selected native triplet for its headers and native .lib files.
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

    # Native vcpkg's FFmpeg FindFFMPEG wrapper exports these variables.
    find_package(FFMPEG REQUIRED)
    if(NOT FFMPEG_LIBRARIES OR NOT FFMPEG_INCLUDE_DIRS)
        message(FATAL_ERROR
            "[Kotonoha] Native vcpkg FFmpeg package was not resolved.")
    endif()

    # A stale cache or rogue environment path may still point to MSYS2.
    # Reject unexpected include/lib directories rather than mixing ABIs.
    file(TO_CMAKE_PATH "${_kotonoha_native_vcpkg}" _vcpkg_native_path)
    string(TOLOWER "${_vcpkg_native_path}/" _vcpkg_native_path_lower)
    foreach(_candidate IN LISTS FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARY_DIRS)
        file(TO_CMAKE_PATH "${_candidate}" _candidate_path)
        string(TOLOWER "${_candidate_path}/" _candidate_path_lower)
        string(FIND "${_candidate_path_lower}" "${_vcpkg_native_path_lower}" _position)
        if(NOT _position EQUAL 0)
            message(FATAL_ERROR
                "[Kotonoha] MSVC dependency outside native vcpkg triplet: "
                "${_candidate}. Remove stale CMake cache or configure "
                "a new MSVC build directory.")
        endif()
    endforeach()
    foreach(_candidate IN LISTS FFMPEG_LIBRARIES)
        if(IS_ABSOLUTE "${_candidate}")
            file(TO_CMAKE_PATH "${_candidate}" _candidate_path)
            string(TOLOWER "${_candidate_path}/" _candidate_path_lower)
            string(FIND "${_candidate_path_lower}" "${_vcpkg_native_path_lower}" _position)
            if(NOT _position EQUAL 0)
                message(FATAL_ERROR
                    "[Kotonoha] MSVC FFmpeg library is not from vcpkg: "
                    "${_candidate}")
            endif()
        endif()
    endforeach()
    add_library(kotonoha_ffmpeg INTERFACE)
    target_include_directories(kotonoha_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
    if(FFMPEG_LIBRARY_DIRS)
        target_link_directories(kotonoha_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
    endif()
    target_link_libraries(kotonoha_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
    set(kotonoha_ffmpeg_target kotonoha_ffmpeg)

    find_package(json-c CONFIG REQUIRED
        PATHS "${_kotonoha_native_vcpkg}" NO_DEFAULT_PATH)
    if(NOT TARGET json-c::json-c)
        message(FATAL_ERROR "[Kotonoha] Native vcpkg json-c::json-c target not found")
    endif()
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
