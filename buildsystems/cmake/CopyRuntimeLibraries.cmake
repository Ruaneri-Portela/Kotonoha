# Copies the shared libraries required by a desktop executable into its
# adjacent "libs" directory. Linux keeps the ELF SONAME, so an $ORIGIN/libs
# runtime path resolves both direct and transitive dependencies. macOS install
# names are rewritten to @loader_path so the copied libraries are used instead
# of the build-tree originals.

if(NOT DEFINED EXECUTABLE OR NOT DEFINED LIBRARY_DIR)
    message(FATAL_ERROR "Missing runtime library packaging argument: EXECUTABLE or LIBRARY_DIR")
endif()

if(NOT DEFINED SEARCH_DIRS)
    set(SEARCH_DIRS "")
endif()

message(VERBOSE "[Kotonoha] Runtime executable: ${EXECUTABLE}")
message(VERBOSE "[Kotonoha] Runtime library directory: ${LIBRARY_DIR}")
message(VERBOSE "[Kotonoha] Runtime library search paths: ${SEARCH_DIRS}")

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable is missing: ${EXECUTABLE}")
endif()

file(MAKE_DIRECTORY "${LIBRARY_DIR}")

set(pre_exclude
        "^/lib/"
        "^/lib64/"
        "^/usr/lib/"
        "^/usr/lib64/"
        "^/System/")
set(post_exclude
        ".*/libc\\.so.*"
        ".*/libm\\.so.*"
        ".*/libdl\\.so.*"
        ".*/libpthread\\.so.*"
        ".*/librt\\.so.*"
        ".*/ld-linux.*"
        ".*/libSystem\\..*"
        ".*/libc\\+\\..*"
        ".*/libobjc\\..*"
        ".*/libresolv\\..*"
        ".*/libutil\\..*"
        ".*/libxpc\\..*"
        ".*/libdispatch\\..*")

file(GET_RUNTIME_DEPENDENCIES
        EXECUTABLES "${EXECUTABLE}"
        DIRECTORIES ${SEARCH_DIRS}
        PRE_EXCLUDE_REGEXES ${pre_exclude}
        POST_EXCLUDE_REGEXES ${post_exclude}
        RESOLVED_DEPENDENCIES_VAR resolved
        UNRESOLVED_DEPENDENCIES_VAR unresolved)

if(unresolved)
    message(STATUS "[Kotonoha] Unresolved runtime libraries for ${EXECUTABLE}: ${unresolved}")
endif()

# Manually resolve @rpath dependencies that file(GET_RUNTIME_DEPENDENCIES) missed
set(newly_resolved "")
foreach(unresolved_lib IN LISTS unresolved)
    if(unresolved_lib MATCHES "^@rpath/(.+)")
        set(lib_name "${CMAKE_MATCH_1}")
        set(found FALSE)
        foreach(search_dir IN LISTS SEARCH_DIRS)
            set(potential_path "${search_dir}/${lib_name}")
            if(EXISTS "${potential_path}")
                list(APPEND newly_resolved "${potential_path}")
                set(found TRUE)
                break()
            endif()
        endforeach()
        if(found)
            message(STATUS "[Kotonoha] Manually resolved @rpath dependency: ${unresolved_lib} -> ${potential_path}")
        endif()
    endif()
endforeach()
list(APPEND resolved ${newly_resolved})

message(VERBOSE "[Kotonoha] Resolved runtime libraries: ${resolved}")

foreach(library IN LISTS resolved)
    if(EXISTS "${library}")
        message(VERBOSE "[Kotonoha] Copying ${library} to ${LIBRARY_DIR}")
        file(COPY "${library}" DESTINATION "${LIBRARY_DIR}" FOLLOW_SYMLINK_CHAIN)
    else()
        message(WARNING "[Kotonoha] Resolved library ${library} does not exist on disk")
    endif()
endforeach()

if(NOT APPLE)
    return()
endif()

file(GLOB copied_libraries LIST_DIRECTORIES FALSE
        "${LIBRARY_DIR}/*.dylib" "${LIBRARY_DIR}/*.so")

# Give every copied dylib a local identity before rewriting the references.
foreach(library IN LISTS copied_libraries)
    get_filename_component(name "${library}" NAME)
    execute_process(COMMAND install_name_tool -id "@loader_path/${name}" "${library}"
            RESULT_VARIABLE result ERROR_QUIET)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot set the install name of ${library}")
    endif()
endforeach()

set(files "${EXECUTABLE}" ${copied_libraries})
foreach(item IN LISTS files)
    execute_process(COMMAND otool -L "${item}"
            OUTPUT_VARIABLE output RESULT_VARIABLE result ERROR_QUIET)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot inspect the runtime dependencies of ${item}")
    endif()

    string(REPLACE "\n" ";" lines "${output}")
    foreach(line IN LISTS lines)
        if(line MATCHES "^[ \t]+(.+) \\(compatibility version")
            set(dependency "${CMAKE_MATCH_1}")
            get_filename_component(dependency_name "${dependency}" NAME)
            if(NOT EXISTS "${LIBRARY_DIR}/${dependency_name}")
                continue()
            endif()

            if(item STREQUAL EXECUTABLE)
                set(replacement "@loader_path/libs/${dependency_name}")
            else()
                set(replacement "@loader_path/${dependency_name}")
            endif()
            if(NOT dependency STREQUAL replacement)
                execute_process(COMMAND install_name_tool -change "${dependency}"
                        "${replacement}" "${item}" RESULT_VARIABLE change_result
                        ERROR_QUIET)
                if(NOT change_result EQUAL 0)
                    message(FATAL_ERROR
                        "Cannot relocate ${dependency} in ${item}")
                endif()
            endif()
        endif()
    endforeach()
endforeach()

# install_name_tool invalidates the existing signature. Re-sign the rewritten
# files ad hoc so Apple silicon does not kill the process at launch.
foreach(item IN LISTS files)
    execute_process(COMMAND codesign --force --sign - "${item}"
            RESULT_VARIABLE sign_result ERROR_QUIET)
    if(NOT sign_result EQUAL 0)
        message(FATAL_ERROR "Cannot ad-hoc sign ${item}")
    endif()
endforeach()
