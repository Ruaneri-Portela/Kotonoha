# Kotonoha Git-aware version metadata.
# Evaluated at CMake configure time, before any engine or game subdirectory.
# Git-less source archives use deterministic, explicit "unknown" values.
if(NOT DEFINED KOTONOHA_VERSION_CONFIGURED)
    set(KOTONOHA_VERSION_CONFIGURED TRUE)
    set(_kotonoha_repo "${CMAKE_CURRENT_LIST_DIR}/../..")
    set(KOTONOHA_GIT_BRANCH "unknown")
    set(KOTONOHA_GIT_COMMIT "unknown")
    set(KOTONOHA_GIT_SHORT "unknown")
    set(KOTONOHA_GIT_DIRTY 0)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_kotonoha_repo}"
                        rev-parse --verify HEAD
                        OUTPUT_VARIABLE _kotonoha_commit
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        RESULT_VARIABLE _kotonoha_git_ok
                        ERROR_QUIET)
        if(_kotonoha_git_ok EQUAL 0 AND
                _kotonoha_commit MATCHES "^[0-9a-fA-F]+$")
            set(KOTONOHA_GIT_COMMIT "${_kotonoha_commit}")
            string(SUBSTRING "${_kotonoha_commit}" 0 12 KOTONOHA_GIT_SHORT)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_kotonoha_repo}"
                            symbolic-ref --quiet --short HEAD
                            OUTPUT_VARIABLE _kotonoha_branch
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_kotonoha_branch)
                set(KOTONOHA_GIT_BRANCH "${_kotonoha_branch}")
            elseif(DEFINED ENV{GITHUB_HEAD_REF} AND NOT "$ENV{GITHUB_HEAD_REF}" STREQUAL "")
                set(KOTONOHA_GIT_BRANCH "$ENV{GITHUB_HEAD_REF}")
            elseif(DEFINED ENV{GITHUB_REF_NAME} AND NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "")
                set(KOTONOHA_GIT_BRANCH "$ENV{GITHUB_REF_NAME}")
            else()
                set(KOTONOHA_GIT_BRANCH "detached")
            endif()
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_kotonoha_repo}"
                            status --porcelain --untracked-files=no
                            OUTPUT_VARIABLE _kotonoha_dirty
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_kotonoha_dirty)
                set(KOTONOHA_GIT_DIRTY 1)
            endif()
            # Watch .git/HEAD and the active ref, so Ninja/Make rerun CMake
            # after checkout/commit without embedding git hashes in every
            # individual game build script.
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_kotonoha_repo}"
                            rev-parse --git-path HEAD
                            OUTPUT_VARIABLE _kotonoha_head_path
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_kotonoha_head_path)
                if(NOT IS_ABSOLUTE "${_kotonoha_head_path}")
                    get_filename_component(_kotonoha_head_path
                        "${_kotonoha_repo}/${_kotonoha_head_path}" ABSOLUTE)
                endif()
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                    "${_kotonoha_head_path}")
            endif()
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_kotonoha_repo}"
                            symbolic-ref --quiet HEAD
                            OUTPUT_VARIABLE _kotonoha_ref
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_kotonoha_ref)
                execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_kotonoha_repo}"
                                rev-parse --git-path "${_kotonoha_ref}"
                                OUTPUT_VARIABLE _kotonoha_ref_path
                                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
                if(NOT IS_ABSOLUTE "${_kotonoha_ref_path}")
                    get_filename_component(_kotonoha_ref_path
                        "${_kotonoha_repo}/${_kotonoha_ref_path}" ABSOLUTE)
                endif()
                if(EXISTS "${_kotonoha_ref_path}")
                    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                        "${_kotonoha_ref_path}")
                endif()
            endif()
        endif()
    endif()

    # Git refs can be unsuitable for C++ string literals and SemVer labels.
    string(REGEX REPLACE "[^A-Za-z0-9._/-]" "_" KOTONOHA_GIT_BRANCH
           "${KOTONOHA_GIT_BRANCH}")
    string(REGEX REPLACE "[^A-Za-z0-9.-]" "-" _kotonoha_semver_branch
           "${KOTONOHA_GIT_BRANCH}")
    if(_kotonoha_semver_branch STREQUAL "")
        set(_kotonoha_semver_branch "unknown")
    endif()
    set(KOTONOHA_VERSION_FULL
        "${CMAKE_PROJECT_VERSION}+${_kotonoha_semver_branch}.${KOTONOHA_GIT_SHORT}")
    if(KOTONOHA_GIT_DIRTY)
        string(APPEND KOTONOHA_VERSION_FULL ".dirty")
    endif()
    set(KOTONOHA_BUILD_VERSION_INCLUDE_DIR
        "${CMAKE_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${KOTONOHA_BUILD_VERSION_INCLUDE_DIR}/Kotonoha/core")
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/BuildVersion.hpp.in"
        "${KOTONOHA_BUILD_VERSION_INCLUDE_DIR}/Kotonoha/core/BuildVersion.hpp"
        @ONLY)
    message(STATUS "[Kotonoha] Version: ${KOTONOHA_VERSION_FULL}")
    message(STATUS "[Kotonoha] Git: ${KOTONOHA_GIT_BRANCH} @ ${KOTONOHA_GIT_COMMIT}")
endif()
