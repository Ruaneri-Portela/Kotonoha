if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

if(NOT EXISTS "${APP_EXECUTABLE}")
    message(FATAL_ERROR "Windows app executable is missing: ${APP_EXECUTABLE}")
endif()
if(NOT IS_DIRECTORY "${APP_DESTINATION}")
    message(FATAL_ERROR "Windows app destination is missing: ${APP_DESTINATION}")
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${APP_EXECUTABLE}"
    DIRECTORIES ${DEPENDENCY_DIRS}
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicts
    PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-win-.*"
    POST_EXCLUDE_REGEXES
        ".*[Ww]indows[/\\\\][Ss]ystem32[/\\\\].*"
        ".*[Ww]indows[/\\\\][Ss]ysWOW64[/\\\\].*")

# CMake reports a conflict when the same DLL name is visible through more than
# one dependency directory. This is common on Windows because vcpkg applocal
# may already have copied a dependency beside the application while a linked
# Kotonoha DLL has the same runtime beside itself. Treat byte-identical copies
# as one dependency, but fail closed if the files with the same name differ.
foreach(filename IN LISTS conflicts_FILENAMES)
    set(paths "${conflicts_${filename}}")
    list(REMOVE_DUPLICATES paths)
    list(LENGTH paths path_count)
    if(path_count LESS 1)
        continue()
    endif()

    list(GET paths 0 selected)
    file(SHA256 "${selected}" selected_hash)
    foreach(candidate IN LISTS paths)
        file(SHA256 "${candidate}" candidate_hash)
        if(NOT candidate_hash STREQUAL selected_hash)
            string(JOIN ", " conflict_paths ${paths})
            message(FATAL_ERROR
                "Conflicting non-identical Windows runtime dependency ${filename}: ${conflict_paths}")
        endif()
    endforeach()
    list(APPEND resolved "${selected}")
endforeach()
list(REMOVE_DUPLICATES resolved)
if(unresolved)
    list(FILTER unresolved EXCLUDE REGEX "^(KERNEL32|USER32|GDI32|ADVAPI32|SHELL32|OLE32|OLEAUT32|WS2_32|WINMM|IMM32|VERSION|SETUPAPI|CFGmgr32|COMDLG32|SHLWAPI|CRYPT32|BCRYPT|DWMAPI|UXTHEME|D3D11|DXGI|OPENGL32|MF|MFPLAT|MFREADWRITE|Mfuuid|AVRT|POWRPROF|NTDLL|MSVCRT|VCRUNTIME[0-9_]*|MSVCP[0-9_]*|ucrtbase)\\.dll$")
endif()

if(unresolved)
    list(JOIN unresolved ", " missing)
    message(FATAL_ERROR "Unresolved Windows runtime dependencies for ${APP_EXECUTABLE}: ${missing}")
endif()

foreach(dependency IN LISTS resolved)
    if(EXISTS "${dependency}")
        file(COPY "${dependency}" DESTINATION "${APP_DESTINATION}")
    endif()
endforeach()
