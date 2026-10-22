# Build topology
# --------------
# KotonohaEngine is the reusable engine library.  SchoolDaysHQ is the game
# application and owns every School Days-specific translation unit plus the SDL
# callback entry point.  Keeping the game sources out of the engine target makes
# future regressions attributable to one side of the boundary without changing
# any runtime semantics.

file(GLOB KOTONOHA_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${KOTONOHA_ENGINE_SOURCE_DIR}/*.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/*.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/components/*.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/mobile/*.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/*.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/routing/*.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/renders/*.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/*.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/*.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/utils/*.c"
)

file(GLOB_RECURSE KOTONOHA_ENGINE_HEADERS CONFIGURE_DEPENDS
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/*.h"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/*.hpp"
)

file(GLOB_RECURSE SCHOOL_DAYS_SOURCES CONFIGURE_DEPENDS
    "${SCHOOL_DAYS_SOURCE_DIR}/*.cpp"
    "${SCHOOL_DAYS_SOURCE_DIR}/*.c"
)

set(SCHOOL_DAYS_MAIN "${SCHOOL_DAYS_SOURCE_DIR}/Main.cpp")
list(REMOVE_ITEM SCHOOL_DAYS_SOURCES "${SCHOOL_DAYS_MAIN}")

# Hard build-boundary check: a School Days implementation file must live under
# games/school-days and may never silently re-enter the engine source glob.
foreach(_source IN LISTS KOTONOHA_ENGINE_SOURCES)
    get_filename_component(_name "${_source}" NAME)
    if(_name MATCHES "^SchoolDays")
        message(FATAL_ERROR
            "School Days source leaked into KotonohaEngine: ${_source}. "
            "Move it under games/school-days/src instead.")
    endif()
    file(READ "${_source}" _engine_source_text)
    if(_engine_source_text MATCHES "SchoolDays|schoolDays")
        message(FATAL_ERROR
            "School Days implementation dependency leaked into KotonohaEngine: "
            "${_source}. Keep profile policy on the SchoolDaysHQ side.")
    endif()
endforeach()

foreach(_header IN LISTS KOTONOHA_ENGINE_HEADERS)
    get_filename_component(_name "${_header}" NAME)
    if(_name MATCHES "^SchoolDays")
        message(FATAL_ERROR
            "School Days header leaked into the engine include tree: ${_header}. "
            "Move it under games/school-days/include instead.")
    endif()
    file(READ "${_header}" _engine_header_text)
    if(_engine_header_text MATCHES "SchoolDays|schoolDays|School Days")
        message(FATAL_ERROR
            "School Days API dependency leaked into the engine include tree: "
            "${_header}. Keep game/profile APIs under games/school-days/include.")
    endif()
endforeach()

if(APPLE)
    list(APPEND KOTONOHA_ENGINE_SOURCES
        "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/MetalFX.mm"
        "${KOTONOHA_ENGINE_SOURCE_DIR}/mobile/IOS.mm"
    )
endif()

set(KOTONOHA_GAME_PLATFORM_SOURCES)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND KOTONOHA_GAME_PLATFORM_SOURCES
        "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/Kotonoha.rc"
    )
endif()

# -----------------------------------------------------------------------------
# Android
# -----------------------------------------------------------------------------
if(ANDROID)
    message(STATUS
        "Configuração Android | ABI: ${ANDROID_ABI} | Platform: ${ANDROID_PLATFORM}")

    unset(harfbuzz_DIR CACHE)
    unset(harfbuzz_ROOT CACHE)
    unset(Freetype_DIR CACHE)
    unset(plutosvg_DIR CACHE)
    unset(plutovg_DIR CACHE)

    find_package(BZip2 REQUIRED)
    find_package(PNG REQUIRED)
    find_package(expat CONFIG REQUIRED)

    add_link_options("-Wl,-z,max-page-size=16384")

    add_library(SDL3_shared_bundle SHARED "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/bundle.cpp")
    target_link_libraries(SDL3_shared_bundle PRIVATE
        "-Wl,--whole-archive" ${KOTONOHA_SDL3_TARGET} "-Wl,--no-whole-archive"
        android log atomic m)

    add_library(SDL3_ttf_shared_bundle SHARED "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/bundle.cpp")
    target_link_libraries(SDL3_ttf_shared_bundle PRIVATE
        "-Wl,--whole-archive" ${KOTONOHA_SDL3_TTF_TARGET} "-Wl,--no-whole-archive"
        SDL3_shared_bundle android log atomic m)

    add_library(KotonohaEngine STATIC ${KOTONOHA_ENGINE_SOURCES})
    add_library(Kotonoha::Engine ALIAS KotonohaEngine)
    set_target_properties(KotonohaEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)

    target_include_directories(KotonohaEngine PUBLIC
        "${KOTONOHA_INCLUDE_DIR}"
        ${LIBASS_INCLUDE_DIRS})

    target_link_directories(KotonohaEngine PUBLIC ${LIBASS_LIBRARY_DIRS})
    target_compile_options(KotonohaEngine PRIVATE ${LIBASS_CFLAGS_OTHER})
    target_link_options(KotonohaEngine PUBLIC ${LIBASS_LDFLAGS_OTHER})
    target_compile_definitions(KotonohaEngine PUBLIC KOTONOHA_HAS_ANIME4K=1)

    target_link_libraries(KotonohaEngine PUBLIC
        SDL3_shared_bundle
        SDL3_ttf_shared_bundle
        PkgConfig::FFMPEG
        ${LIBASS_LIBRARIES}
        imgui::imgui
        expat::expat
        android
        mediandk
        log
        atomic
        m
        GLESv2)

    # SDLActivity loads libKotonoha.so today.  Keep the Android SONAME stable
    # while the CMake target itself clearly identifies the game application.
    add_library(SchoolDaysHQ SHARED
        "${SCHOOL_DAYS_MAIN}"
        ${SCHOOL_DAYS_SOURCES}
        ${KOTONOHA_GAME_PLATFORM_SOURCES})
    set_target_properties(SchoolDaysHQ PROPERTIES OUTPUT_NAME Kotonoha)
    target_include_directories(SchoolDaysHQ PRIVATE
        "${SCHOOL_DAYS_INCLUDE_DIR}"
        "${KOTONOHA_INCLUDE_DIR}"
        "${KOTONOHA_ENGINE_SOURCE_DIR}/routing")
    target_link_libraries(SchoolDaysHQ PRIVATE Kotonoha::Engine ZLIB::ZLIB)

# -----------------------------------------------------------------------------
# Desktop
# -----------------------------------------------------------------------------
else()
    message(STATUS "Configuração Desktop | Sistema: ${CMAKE_SYSTEM_NAME}")

    add_library(KotonohaEngine STATIC ${KOTONOHA_ENGINE_SOURCES})
    add_library(Kotonoha::Engine ALIAS KotonohaEngine)
    set_target_properties(KotonohaEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)

    target_include_directories(KotonohaEngine PUBLIC "${KOTONOHA_INCLUDE_DIR}")
    target_link_libraries(KotonohaEngine PUBLIC
        PkgConfig::FFMPEG
        PkgConfig::LIBASS
        ${KOTONOHA_SDL3_TARGET}
        ${KOTONOHA_SDL3_TTF_TARGET}
        imgui::imgui)

    if(APPLE)
        add_executable(SchoolDaysHQ MACOSX_BUNDLE
            "${SCHOOL_DAYS_MAIN}"
            ${SCHOOL_DAYS_SOURCES}
            ${KOTONOHA_GAME_PLATFORM_SOURCES})
    else()
        add_executable(SchoolDaysHQ
            "${SCHOOL_DAYS_MAIN}"
            ${SCHOOL_DAYS_SOURCES}
            ${KOTONOHA_GAME_PLATFORM_SOURCES})
    endif()

    target_include_directories(SchoolDaysHQ PRIVATE
        "${SCHOOL_DAYS_INCLUDE_DIR}"
        "${KOTONOHA_INCLUDE_DIR}"
        "${KOTONOHA_ENGINE_SOURCE_DIR}/routing")
    target_link_libraries(SchoolDaysHQ PRIVATE Kotonoha::Engine ZLIB::ZLIB)

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
        target_compile_definitions(SchoolDaysHQ PRIVATE KOTONOHA_HAS_WINDOWS_ICON=1)
    endif()

    # Config.DAT and retail GPKs consume zlib from the game layer.  Copy the
    # runtime DLL beside SchoolDaysHQ on Windows rather than coupling zlib to the
    # reusable engine target.
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        get_filename_component(_kotonoha_zlib_prefix "${ZLIB_INCLUDE_DIRS}" DIRECTORY)
        find_file(_kotonoha_zlib_runtime
            NAMES z.dll zlib1.dll
            HINTS "${_kotonoha_zlib_prefix}/bin"
            NO_DEFAULT_PATH)
        if(_kotonoha_zlib_runtime)
            add_custom_command(TARGET SchoolDaysHQ POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_kotonoha_zlib_runtime}"
                    "$<TARGET_FILE_DIR:SchoolDaysHQ>"
                VERBATIM)
        endif()
    endif()

    find_package(OpenGL QUIET)
    if(OpenGL_FOUND)
        target_compile_definitions(KotonohaEngine PUBLIC KOTONOHA_HAS_ANIME4K=1)
        target_link_libraries(KotonohaEngine PUBLIC OpenGL::GL)
        add_custom_command(TARGET SchoolDaysHQ POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${KOTONOHA_ASSETS_DIR}/Anime4K"
                "$<TARGET_FILE_DIR:SchoolDaysHQ>/assets/Anime4K"
            VERBATIM)
    else()
        message(STATUS "OpenGL not found; Anime4K upscaling is disabled")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        target_compile_options(KotonohaEngine PRIVATE -fPIC)
    endif()

    if(APPLE)
        target_compile_definitions(KotonohaEngine PUBLIC KOTONOHA_HAS_METALFX=1)
        target_link_libraries(KotonohaEngine PUBLIC
            "-framework Foundation"
            "-framework CoreFoundation"
            "-framework Metal"
            "-framework MetalFX")

        set_target_properties(SchoolDaysHQ PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST
                "${CMAKE_CURRENT_SOURCE_DIR}/src/specials/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "SchoolDaysHQ"
            MACOSX_BUNDLE_GUI_IDENTIFIER "me.hirameki.kotonoha.schooldayshq"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_ICON_FILE "icon.icns")

        set(KOTONOHA_ICON_FILE "${KOTONOHA_ASSETS_DIR}/icon.icns")
        if(NOT EXISTS "${KOTONOHA_ICON_FILE}")
            message(FATAL_ERROR "Ícone macOS não encontrado: ${KOTONOHA_ICON_FILE}")
        endif()
        set_source_files_properties("${KOTONOHA_ICON_FILE}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
            HEADER_FILE_ONLY TRUE)
        target_sources(SchoolDaysHQ PRIVATE "${KOTONOHA_ICON_FILE}")

        if(EXISTS "${KOTONOHA_ASSETS_DIR}")
            file(GLOB_RECURSE KOTONOHA_ASSET_FILES CONFIGURE_DEPENDS
                "${KOTONOHA_ASSETS_DIR}/*")
            foreach(ASSET_FILE IN LISTS KOTONOHA_ASSET_FILES)
                if(IS_DIRECTORY "${ASSET_FILE}" OR ASSET_FILE STREQUAL KOTONOHA_ICON_FILE)
                    continue()
                endif()
                file(RELATIVE_PATH ASSET_RELATIVE_PATH
                    "${KOTONOHA_ASSETS_DIR}" "${ASSET_FILE}")
                get_filename_component(ASSET_RELATIVE_DIRECTORY
                    "${ASSET_RELATIVE_PATH}" DIRECTORY)
                if(ASSET_RELATIVE_DIRECTORY)
                    set(ASSET_BUNDLE_LOCATION
                        "Resources/assets/${ASSET_RELATIVE_DIRECTORY}")
                else()
                    set(ASSET_BUNDLE_LOCATION "Resources/assets")
                endif()
                set_source_files_properties("${ASSET_FILE}" PROPERTIES
                    MACOSX_PACKAGE_LOCATION "${ASSET_BUNDLE_LOCATION}"
                    HEADER_FILE_ONLY TRUE)
                target_sources(SchoolDaysHQ PRIVATE "${ASSET_FILE}")
            endforeach()
        endif()
    endif()
endif()
