cmake_minimum_required(VERSION 3.18)
project(SchoolDaysApp LANGUAGES C CXX)

if(NOT TARGET KotonohaRuntime)
    set(_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    set(KOTONOHA_INCLUDE_DIR "${_ROOT_DIR}/include")
    set(KOTONOHA_ENGINE_SOURCE_DIR "${_ROOT_DIR}/src/Kotonoha")
    set(SCHOOL_DAYS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
    set(SCHOOL_DAYS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_subdirectory("${_ROOT_DIR}" kotonoha_build EXCLUDE_FROM_ALL)
endif()

# School Days HQ application: game profile, KTRF adapter, persistence, UI,
# GPK mount and the SDL callbacks, on top of the KotonohaRuntime library.
# Produces SchoolDays.app on macOS, SchoolDays(.exe) on other desktops and
# libKotonohaSchoolDays.so (loaded by SDLActivity) on Android.

find_package(ZLIB REQUIRED)

file(GLOB_RECURSE SCHOOL_DAYS_SOURCES CONFIGURE_DEPENDS
    "${SCHOOL_DAYS_SOURCE_DIR}/*.cpp"
    "${SCHOOL_DAYS_SOURCE_DIR}/*.c"
)

if(ANDROID)
    add_library(SchoolDays SHARED ${SCHOOL_DAYS_SOURCES} )
    set_target_properties(SchoolDays PROPERTIES OUTPUT_NAME SchoolDays)
    target_link_libraries(SchoolDays PRIVATE KotonohaRuntime ZLIB::ZLIB)
else()
    add_executable(SchoolDays ${SCHOOL_DAYS_SOURCES})
    target_link_libraries(SchoolDays PUBLIC KotonohaRuntime PRIVATE ZLIB::ZLIB)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # Commercial/player-facing Windows builds are GUI applications.  Keep
        # diagnostics in SDL/debugger logging, but do not allocate a console
        # window beside the game for ordinary users.
        set_target_properties(SchoolDays PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()

target_include_directories(SchoolDays PRIVATE
    "${SCHOOL_DAYS_INCLUDE_DIR}"
    "${KOTONOHA_INCLUDE_DIR}")

if(KOTONOHA_DEV_CHECKPOINTS)
    target_compile_definitions(SchoolDays PRIVATE KOTONOHA_DEV_CHECKPOINTS=1)
    message(STATUS "KOTONOHA_DEV_CHECKPOINTS=ON (development-only routing helpers)")
endif()

if(NOT ANDROID)
    kotonoha_configure_app(SchoolDays
        OUTPUT_NAME SchoolDays
        BUNDLE_ID "me.hirameki.kotonoha.schooldays"
        GAME_ASSET "school-days"
        )

    add_custom_command(TARGET SchoolDays POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:SchoolDays>/assets/school-days"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/styles.skot"
            "$<TARGET_FILE_DIR:SchoolDays>/assets/school-days/styles.skot"
        VERBATIM)

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        get_filename_component(_kotonoha_zlib_prefix "${ZLIB_INCLUDE_DIRS}" DIRECTORY)
        find_file(_kotonoha_zlib_runtime
            NAMES z.dll zlib1.dll
            HINTS "${_kotonoha_zlib_prefix}/bin"
            NO_DEFAULT_PATH)
        if(_kotonoha_zlib_runtime)
            add_custom_command(TARGET SchoolDays POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_kotonoha_zlib_runtime}"
                    "$<TARGET_FILE_DIR:SchoolDays>"
                VERBATIM)
        endif()
    endif()
endif()

if((KOTONOHA_ROUTER_TESTS OR KOTONOHA_ORS_PARSER_TESTS) AND NOT ANDROID)
    add_subdirectory(tests)
endif()
