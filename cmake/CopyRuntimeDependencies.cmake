cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

get_filename_component(destination "${APP_EXECUTABLE}" DIRECTORY)

# Get runtime dependencies - list DEPENDENCY_DIRS in priority order
# vcpkg_installed should take priority over msys64 system folder
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${APP_EXECUTABLE}"
    DIRECTORIES ${DEPENDENCY_DIRS}
    PRE_EXCLUDE_REGEXES "^[aA][pP][iI]-[mM][sS]-" "^[eE][xX][tT]-[mM][sS]-"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*" ".*libgcc.*" ".*libstdc\\+\\+.*" ".*libwinpthread.*"
    RESOLVED_DEPENDENCIES_VAR dependencies
    UNRESOLVED_DEPENDENCIES_VAR missing)

# Handle unresolved dependencies by filtering system libraries we know about
set(known_unresolved "libkotonoha.dll;libkotonoha_imgui.dll;SDL3_ttf.dll;SDL3.dll")
foreach(m IN LISTS missing)
    list(REMOVE_ITEM known_unresolved "${m}")
endforeach()
if(missing)
    message(FATAL_ERROR "Unresolved runtime dependencies of ${APP_EXECUTABLE}: ${missing}")
endif()

file(TO_CMAKE_PATH "$ENV{SystemRoot}" system_root)
string(TOLOWER "${system_root}/" system_root)
set(runtime_names)
foreach(dependency IN LISTS dependencies)
    file(TO_CMAKE_PATH "${dependency}" dependency)
    string(TOLOWER "${dependency}" lower_path)
    string(FIND "${lower_path}" "${system_root}" is_system)
    if(is_system EQUAL 0)
        continue()
    endif()
    get_filename_component(name "${dependency}" NAME)
    string(TOLOWER "${name}" lower_name)
    list(APPEND runtime_names "${lower_name}")
    if(NOT dependency STREQUAL "${destination}/${name}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${dependency}" "${destination}/${name}"
            COMMAND_ERROR_IS_FATAL ANY)
    endif()
endforeach()

# Migrate old build outputs produced by copying the entire vcpkg bin folder.
# Only remove unused Python DLLs which are byte-for-byte copies of that input.
foreach(directory IN LISTS DEPENDENCY_DIRS)
    file(GLOB python_dlls "${directory}/python*.dll" "${directory}/python*.pdb")
    foreach(source IN LISTS python_dlls)
        get_filename_component(name "${source}" NAME)
        string(TOLOWER "${name}" lower_name)
        if(lower_name MATCHES "\\.pdb$")
            string(REGEX REPLACE "\\.pdb$" ".dll" lower_name "${lower_name}")
        endif()
        set(old_copy "${destination}/${name}")
        if(NOT lower_name IN_LIST runtime_names AND EXISTS "${old_copy}" AND
           NOT source STREQUAL old_copy)
            file(SHA256 "${source}" source_hash)
            file(SHA256 "${old_copy}" copy_hash)
            if(source_hash STREQUAL copy_hash)
                file(REMOVE "${old_copy}")
            endif()
        endif()
    endforeach()
endforeach()
