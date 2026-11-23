# CMake 4.2+ normalizes Windows paths before matching runtime DLL filters.
# Opt in explicitly; older CMake versions do not have CMP0207.
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

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

# On Linux/Windows, do not scoop up unrelated host system installations:
# only libraries from this CMake build tree or the configured vcpkg root are
# deployable. macOS is allowed to use Homebrew as well.
function(kotonoha_library_is_deployable candidate out)
    if(APPLE)
        set(${out} TRUE PARENT_SCOPE)
        return()
    endif()
    file(TO_CMAKE_PATH "${candidate}" _candidate)
    string(TOLOWER "${_candidate}" _candidate)
    set(_accepted FALSE)
    foreach(_root IN ITEMS "${KOTONOHA_BUILD_LIBRARY_ROOT}"
                           "${KOTONOHA_VCPKG_ROOT}")
        if(NOT _root)
            continue()
        endif()
        file(TO_CMAKE_PATH "${_root}" _root_path)
        string(TOLOWER "${_root_path}" _root_path)
        string(FIND "${_candidate}/" "${_root_path}/" _position)
        if(_position EQUAL 0)
            set(_accepted TRUE)
            break()
        endif()
    endforeach()
    set(${out} ${_accepted} PARENT_SCOPE)
endfunction()

# Packaged shared targets must not depend on a special vcpkg configuration:
# SDL3, SDL_ttf, ImGui and KotonohaEngine are built from CMake source too.
# Copy direct built-target binaries independently of dependency discovery;
# a linked library can otherwise be skipped due to host search/RPATH rules.
foreach(_built IN LISTS CMAKE_SHARED_LIBRARIES)
    if(NOT EXISTS "${_built}")
        message(FATAL_ERROR
            "[Kotonoha] CMake shared target is missing: ${_built}")
    endif()
    get_filename_component(_filename "${_built}" NAME)
    if(WIN32 AND _filename MATCHES "\\.dll$")
        file(COPY "${_built}" DESTINATION "${LIBRARY_DIR}")
    elseif(APPLE AND (_filename MATCHES "\\.dylib$" OR
                       _filename MATCHES "\\.so$"))
        file(COPY "${_built}" DESTINATION "${LIBRARY_DIR}"
            FOLLOW_SYMLINK_CHAIN)
    elseif(UNIX AND (_filename MATCHES "\\.so($|\\.)" OR
                      _filename MATCHES "\\.dylib$"))
        file(COPY "${_built}" DESTINATION "${LIBRARY_DIR}"
            FOLLOW_SYMLINK_CHAIN)
    endif()
endforeach()

# On Windows, DLLs belong next to the executable rather than in libs/.
# This works with ordinary CMake builds and with vcpkg/MinGW/Ninja; it
# also avoids TARGET_RUNTIME_DLLS (introduced only in CMake 3.21).
if(WIN32)
    file(GET_RUNTIME_DEPENDENCIES
        EXECUTABLES "${EXECUTABLE}"
        DIRECTORIES ${SEARCH_DIRS}
        PRE_EXCLUDE_REGEXES "^[Aa][Pp][Ii]-[Mm][Ss]-[Ww][Ii][Nn]-.*"
                            "^[Ee][Xx][Tt]-[Mm][Ss]-[Ww][Ii][Nn]-.*"
        POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*"
        RESOLVED_DEPENDENCIES_VAR _windows_dlls
        UNRESOLVED_DEPENDENCIES_VAR _windows_missing
        CONFLICTING_DEPENDENCIES_PREFIX _windows_conflicts)
    # vcpkg's app-local deployment can place copies of the same DLL in
    # both the shared build bin directory and individual game directories.
    # Accept duplicates only when their bytes match; never silently select
    # one of two different DLL builds with the same basename.
    foreach(_dll_name IN LISTS _windows_conflicts_FILENAMES)
        set(_candidates "${_windows_conflicts_${_dll_name}}")
        # Host installations (e.g. MSYS2's libbrotlicommon.dll) also sit on
        # SEARCH_DIRS but are never copied by kotonoha_library_is_deployable().
        # Drop them before comparing hashes so a harmless host/build-tree name
        # collision does not abort packaging of an otherwise valid runtime.
        set(_deployable_candidates "")
        foreach(_candidate IN LISTS _candidates)
            kotonoha_library_is_deployable("${_candidate}" _candidate_deployable)
            if(_candidate_deployable)
                list(APPEND _deployable_candidates "${_candidate}")
            endif()
        endforeach()
        if(NOT _deployable_candidates)
            continue()
        endif()
        set(_candidates "${_deployable_candidates}")
        set(_expected_hash "")
        set(_identical TRUE)
        foreach(_candidate IN LISTS _candidates)
            file(SHA256 "${_candidate}" _candidate_hash)
            if(_expected_hash AND NOT _candidate_hash STREQUAL _expected_hash)
                set(_identical FALSE)
                break()
            endif()
            set(_expected_hash "${_candidate_hash}")
        endforeach()
        if(NOT _identical)
            message(FATAL_ERROR
                "[Kotonoha] Different runtime DLLs share the name "
                "${_dll_name}: ${_candidates}")
        endif()
        list(GET _candidates 0 _identical_dll)
        list(APPEND _windows_dlls "${_identical_dll}")
    endforeach()
    if(_windows_missing)
        message(WARNING
            "[Kotonoha] Unresolved Windows runtime DLLs: ${_windows_missing}")
    endif()
    foreach(_dll IN LISTS _windows_dlls)
        kotonoha_library_is_deployable("${_dll}" _deployable)
        if(_deployable AND EXISTS "${_dll}" AND
           _dll MATCHES "\\.[Dd][Ll][Ll]$")
            file(COPY "${_dll}" DESTINATION "${LIBRARY_DIR}")
        endif()
    endforeach()
    return()
endif()

set(pre_exclude
        "^/lib/"
        "^/lib64/"
        "^/usr/lib/"
        "^/usr/lib64/"
        "^/System/")
set(post_exclude
        ".*/outputs/.*"
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

# An explicitly built shared library may not be part of a given game's
# direct import table. Include it in the relocation pass regardless.
list(APPEND resolved ${CMAKE_SHARED_LIBRARIES})
list(REMOVE_DUPLICATES resolved)

foreach(library IN LISTS resolved)
    kotonoha_library_is_deployable("${library}" _deployable)
    if(NOT _deployable)
        message(VERBOSE "[Kotonoha] Host library left external: ${library}")
        continue()
    endif()
    if(EXISTS "${library}")
        message(VERBOSE "[Kotonoha] Copying ${library} to ${LIBRARY_DIR}")
        file(COPY "${library}" DESTINATION "${LIBRARY_DIR}" FOLLOW_SYMLINK_CHAIN)
    else()
        message(WARNING "[Kotonoha] Resolved library ${library} does not exist on disk")
    endif()
endforeach()

if(NOT APPLE)
    # Set local RPATH on copied ELF libraries when patchelf exists. Without
    # it, only the executable may have $ORIGIN/libs, which does not cover
    # indirect dependencies when the ELF loader uses RUNPATH semantics.
    find_program(_kotonoha_patchelf patchelf)
    if(_kotonoha_patchelf)
        file(GLOB _elf_libraries LIST_DIRECTORIES FALSE
            "${LIBRARY_DIR}/*.so" "${LIBRARY_DIR}/*.so.*")
        foreach(_lib IN LISTS _elf_libraries)
            if(IS_SYMLINK "${_lib}")
                continue()
            endif()
            execute_process(COMMAND "${_kotonoha_patchelf}"
                --set-rpath "$ORIGIN" "${_lib}"
                RESULT_VARIABLE _patchelf_result
                ERROR_VARIABLE _patchelf_error)
            if(NOT _patchelf_result EQUAL 0)
                message(FATAL_ERROR
                    "Cannot set origin-relative RPATH on ${_lib}: "
                    "${_patchelf_error}")
            endif()
        endforeach()
    else()
        message(STATUS
            "[Kotonoha] patchelf unavailable; copied ELF library RPATHs "
            "are not rewritten (transitive dependencies may require it)")
    endif()
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
