# Ninja parses /showIncludes as UTF-8. Use the same encoding for detection
# and compilation, including VS installations without the English language
# pack. Otherwise localized output can leave objects with zero dependencies.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    if(NOT KOTONOHA_MSVC_CONFIGURED)
        set(_msvc_launcher "${CMAKE_CURRENT_LIST_DIR}/MSVC-UTF8.cmd")
        set(_msvc_probe "${CMAKE_BINARY_DIR}/CMakeFiles/KotonohaShowIncludes")
        file(WRITE "${_msvc_probe}/probe.h" "\n")
        file(WRITE "${_msvc_probe}/probe.cpp" "#include \"probe.h\"\n")
        execute_process(
            COMMAND "${_msvc_launcher}" "${CMAKE_CXX_COMPILER}"
                /nologo /showIncludes /c probe.cpp
            WORKING_DIRECTORY "${_msvc_probe}"
            OUTPUT_VARIABLE _msvc_output
            ERROR_VARIABLE _msvc_error
            RESULT_VARIABLE _msvc_result
            ENCODING UTF-8)
        if(NOT _msvc_result EQUAL 0 OR NOT _msvc_output MATCHES
            "(^|\n)([^:\n][^:\n]+:[^:\n]*[^: \n][^: \n]:?[ \t]+)([A-Za-z]:\\\\|\\./|\\.\\\\|/)")
            message(FATAL_ERROR "Cannot detect MSVC /showIncludes prefix: ${_msvc_output}${_msvc_error}")
        endif()
        set(KOTONOHA_MSVC_INCLUDE_PREFIX "${CMAKE_MATCH_2}")
        foreach(lang C CXX)
            list(PREPEND CMAKE_${lang}_COMPILER_LAUNCHER
                "${_msvc_launcher}")
        endforeach()
        add_compile_options(/utf-8)
        # Executables in the same directory share DLLs. vcpkg's app-local
        # scanner must not read one while another target is copying it.
        set_property(GLOBAL APPEND PROPERTY JOB_POOLS kotonoha_windows_link=1)
        set(CMAKE_JOB_POOL_LINK kotonoha_windows_link)
        set(KOTONOHA_MSVC_CONFIGURED TRUE)
    endif()
    # enable_language(C) in lib may have restored CMake's cached prefix.
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${KOTONOHA_MSVC_INCLUDE_PREFIX}")
    set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${KOTONOHA_MSVC_INCLUDE_PREFIX}")
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${KOTONOHA_MSVC_INCLUDE_PREFIX}")
endif()
