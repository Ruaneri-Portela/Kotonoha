# Keep pinned Git submodules reproducible across CMake builds.
include_guard(GLOBAL)
option(KOTONOHA_AUTO_FETCH_SUBMODULES
  "Initialize missing Git submodules during configuration" ON)
get_filename_component(_KOTONOHA_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

function(kotonoha_require_submodule path file)
  if(EXISTS "${_KOTONOHA_REPO_ROOT}/${path}/${file}")
    return()
  endif()
  if(NOT KOTONOHA_AUTO_FETCH_SUBMODULES)
    message(FATAL_ERROR "[Kotonoha] Missing ${path}/${file}. Run: git submodule update --init -- ${path}")
  endif()
  find_package(Git QUIET)
  if(NOT Git_FOUND)
    message(FATAL_ERROR "[Kotonoha] Git is required to initialize ${path}")
  endif()
  message(STATUS "[Kotonoha] Initializing pinned submodule: ${path}")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_KOTONOHA_REPO_ROOT}" submodule update --init -- "${path}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0 OR
     NOT EXISTS "${_KOTONOHA_REPO_ROOT}/${path}/${file}")
    message(FATAL_ERROR "[Kotonoha] Could not initialize ${path}: ${error}\n${output}")
  endif()
endfunction()
