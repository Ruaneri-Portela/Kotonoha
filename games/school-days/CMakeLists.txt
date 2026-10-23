# School Days HQ application: game profile, KTRF adapter, persistence, UI,
# GPK mount and the SDL callbacks, on top of the KotonohaRuntime library.
# Produces SchoolDays.app on macOS, SchoolDays(.exe) on other desktops and
# libKotonohaSchoolDays.so (loaded by SDLActivity) on Android.

file(GLOB_RECURSE SCHOOL_DAYS_SOURCES CONFIGURE_DEPENDS
    "${SCHOOL_DAYS_SOURCE_DIR}/*.cpp"
    "${SCHOOL_DAYS_SOURCE_DIR}/*.c"
)
set(SCHOOL_DAYS_MAIN "${SCHOOL_DAYS_SOURCE_DIR}/Main.cpp")
list(REMOVE_ITEM SCHOOL_DAYS_SOURCES "${SCHOOL_DAYS_MAIN}"
    ${KOTONOHA_RUNTIME_GAME_POLICY_SOURCES})

if(ANDROID)
    add_library(SchoolDays SHARED ${SCHOOL_DAYS_SOURCES} "${SCHOOL_DAYS_MAIN}")
    set_target_properties(SchoolDays PROPERTIES OUTPUT_NAME KotonohaSchoolDays)
else()
    add_executable(SchoolDays ${SCHOOL_DAYS_SOURCES} "${SCHOOL_DAYS_MAIN}")
endif()

target_link_libraries(SchoolDays PUBLIC KotonohaRuntime PRIVATE ZLIB::ZLIB)

if(KOTONOHA_DEV_CHECKPOINTS)
    target_compile_definitions(SchoolDays PRIVATE KOTONOHA_DEV_CHECKPOINTS=1)
    message(STATUS "KOTONOHA_DEV_CHECKPOINTS=ON (development-only routing helpers)")
endif()

if(NOT ANDROID)
    kotonoha_configure_app(SchoolDays
        OUTPUT_NAME SchoolDays
        BUNDLE_ID "me.hirameki.kotonoha.schooldays")

    # Config.DAT and retail GPKs consume zlib from the game layer.  Copy the
    # runtime DLL beside the executable on Windows rather than coupling zlib to
    # the reusable engine target.
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

# School Days regression tests (games/school-days/tests).
if((KOTONOHA_ROUTER_TESTS OR KOTONOHA_ORS_PARSER_TESTS) AND NOT ANDROID)
    add_subdirectory(tests)
endif()
