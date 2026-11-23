# Python.framework (Homebrew) and Python3.framework (Xcode) can contain a
# Python.app helper under Versions/<version>/Resources. This nested executable
# is not required for CPython embedding and must not remain inside the signed
# Kotonoha .app.
# Only touch the staged framework directory; never modify the Python install.
include_guard(GLOBAL)

function(kotonoha_prune_nested_python_app python_version_dir)
    if(NOT IS_DIRECTORY "${python_version_dir}")
        message(FATAL_ERROR
            "Cannot sanitize Python framework: missing staged version directory ${python_version_dir}")
    endif()

    set(_nested_app "${python_version_dir}/Resources/Python.app")
    if(IS_SYMLINK "${_nested_app}")
        # Remove the link itself, never follow it outside the .app bundle.
        file(REMOVE "${_nested_app}")
    elseif(EXISTS "${_nested_app}")
        if(NOT IS_DIRECTORY "${_nested_app}")
            message(FATAL_ERROR
                "Unexpected Python.app entry in staged framework: ${_nested_app}")
        endif()
        message(STATUS "[Kotonoha/Python] Removing nested Python.app helper from staged bundle")
        file(REMOVE_RECURSE "${_nested_app}")
    endif()

    if(EXISTS "${_nested_app}" OR IS_SYMLINK "${_nested_app}")
        message(FATAL_ERROR
            "Could not remove nested Python.app from staged bundle: ${_nested_app}")
    endif()
endfunction()
