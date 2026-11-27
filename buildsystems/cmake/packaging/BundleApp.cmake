if(NOT EXISTS "${APP_EXECUTABLE}" OR NOT IS_DIRECTORY "${BUNDLE_CONTENTS}")
    message(FATAL_ERROR "App bundle is missing: ${APP_EXECUTABLE}")
endif()

if(NOT APP_GAME)
    message(FATAL_ERROR "App bundle game identifier is missing")
endif()

# Ship the universal detached host next to the app executable. The launcher
# core discovers it there; for Ren'Py titles this also gives the host access
# to the app bundle's own Python standard library.
if(KOTONOHA_GAME_HOST AND EXISTS "${KOTONOHA_GAME_HOST}")
    get_filename_component(app_executable_dir "${APP_EXECUTABLE}" DIRECTORY)
    file(MAKE_DIRECTORY "${app_executable_dir}")
    file(COPY "${KOTONOHA_GAME_HOST}" DESTINATION "${app_executable_dir}"
         FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                          GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()
# Remove Python's nested helper application only from the staged bundle.
include("${CMAKE_CURRENT_LIST_DIR}/PruneNestedPythonApp.cmake")
# The unified hub links DDLC's runtime when built with BUILD_ALL.
# Its macOS bundle needs the same Python deployment as standalone DDLC.
if(APP_GAME STREQUAL "doki-doki-literature-club" OR
   APP_GAME STREQUAL "katawa-shoujo" OR
   (APP_GAME STREQUAL "launcher" AND PYTHON_STDLIB AND PYTHON_VERSION))
    if(NOT IS_DIRECTORY "${PYTHON_STDLIB}" OR NOT PYTHON_VERSION)
        message(FATAL_ERROR "Python standard library is missing: ${PYTHON_STDLIB}")
    endif()
    set(include_python ON)
elseif(PYTHON_STDLIB OR PYTHON_VERSION)
    message(FATAL_ERROR "Only RenPy games (DDLC/Katawa) and the linked launcher may bundle Python")
else()
    set(include_python OFF)
endif()

set(plugins)
if(include_python)
    get_filename_component(python_stdlib_real "${PYTHON_STDLIB}" REALPATH)
    get_filename_component(python_lib_dir "${python_stdlib_real}" DIRECTORY)
    get_filename_component(python_framework_version "${python_lib_dir}" DIRECTORY)
    # Homebrew ships Python.framework (binary "Python"); Xcode ships the
    # universal Python3.framework (binary "Python3"). The amd64 build uses the
    # latter because Homebrew is arm64-only. Detect both so the runtime is
    # staged inside a real .framework bundle instead of a plain "python"
    # directory, which codesign --deep rejects as an invalid bundle.
    if(python_framework_version MATCHES "/(Python3?)\\.framework/Versions/[^/]+$")
        # CMAKE_MATCH_1 must be captured before another if() is evaluated.
        set(_python_framework_name "${CMAKE_MATCH_1}")
    else()
        set(_python_framework_name "")
    endif()
    if(_python_framework_name AND EXISTS "${python_framework_version}/${_python_framework_name}")
        set(python_framework_name "${_python_framework_name}")
        set(python_binary_name "${python_framework_name}")
        get_filename_component(python_version "${python_framework_version}" NAME)
        set(python_framework "${BUNDLE_CONTENTS}/Frameworks/${python_framework_name}.framework")
        set(python_version_dir "${python_framework}/Versions/${python_version}")
        set(python_home "${python_version_dir}")
    else()
        set(python_framework_name "")
        set(python_binary_name "Python")
        set(python_home "${BUNDLE_CONTENTS}/Frameworks/python")
    endif()
    set(python_lib "${python_home}/lib/python${PYTHON_VERSION}")
    file(COPY "${PYTHON_STDLIB}/" DESTINATION "${python_lib}"
        PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE
        PATTERN "site-packages" EXCLUDE PATTERN "test" EXCLUDE
        PATTERN "tests" EXCLUDE PATTERN "config-*" EXCLUDE)
    file(GLOB stale_python_configs LIST_DIRECTORIES true "${python_lib}/config-*")
    if(stale_python_configs)
        message(FATAL_ERROR
            "The bundle contains a stale Python build configuration directory. Clean the build before packaging.")
    endif()
    file(GLOB_RECURSE plugins "${python_lib}/lib-dynload/*.so")
    # Remove old CMake-owned layouts after the new copy is ready. A stale
    # "Frameworks/python" directory is especially harmful: codesign --deep
    # treats it as a bundle and fails with "bundle format unrecognized".
    file(REMOVE_RECURSE "${BUNDLE_CONTENTS}/Resources/python")
    if(python_framework)
        file(REMOVE_RECURSE "${BUNDLE_CONTENTS}/Frameworks/python")
    endif()
endif()

include(BundleUtilities)
set(BU_CHMOD_BUNDLE_ITEMS ON)

if(include_python)
    if(python_framework)
        # A prior build/fixup may have staged a Python.app helper (Homebrew's
        # Python.framework or Xcode's Python3.framework both carry one).
        # Prune it without forcing users to delete the whole build directory.
        kotonoha_prune_nested_python_app("${python_version_dir}")
        file(MAKE_DIRECTORY "${python_version_dir}/Resources")
        file(COPY "${python_framework_version}/${python_binary_name}"
            DESTINATION "${python_version_dir}")
        if(EXISTS "${python_framework_version}/Resources/Info.plist")
            file(COPY "${python_framework_version}/Resources/Info.plist"
                DESTINATION "${python_version_dir}/Resources")
        endif()
        foreach(link IN ITEMS "Versions/Current|${python_version}"
                              "${python_binary_name}|Versions/Current/${python_binary_name}"
                              "Resources|Versions/Current/Resources")
            string(REPLACE "|" ";" parts "${link}")
            list(GET parts 0 name)
            list(GET parts 1 target)
            set(link_path "${python_framework}/${name}")
            if(IS_SYMLINK "${link_path}")
                file(READ_SYMLINK "${link_path}" existing_target)
                if(NOT existing_target STREQUAL target)
                    message(FATAL_ERROR "Stale Python framework link: ${link_path}")
                endif()
            elseif(EXISTS "${link_path}")
                message(FATAL_ERROR "Python framework link is not a symlink: ${link_path}")
            else()
                file(CREATE_LINK "${target}" "${link_path}" SYMBOLIC RESULT link_result)
                if(NOT link_result STREQUAL "0")
                    message(FATAL_ERROR "Cannot create ${link_path}: ${link_result}")
                endif()
            endif()
        endforeach()

        set(bundled_python
            "@executable_path/../Frameworks/${python_framework_name}.framework/Versions/${python_version}/${python_binary_name}")
        foreach(item IN ITEMS "${APP_EXECUTABLE}" ${plugins})
            get_prerequisites("${item}" prerequisites 1 0 "" "${DEPENDENCY_DIRS}")
            foreach(prerequisite IN LISTS prerequisites)
                if(prerequisite MATCHES
                   "(^|/)Python3?\\.framework/Versions/[^/]+/Python3?$")
                    execute_process(COMMAND install_name_tool -change "${prerequisite}"
                        "${bundled_python}" "${item}" RESULT_VARIABLE change_result)
                    if(NOT change_result EQUAL 0)
                        message(FATAL_ERROR "Cannot relocate Python dependency in ${item}")
                    endif()
                endif()
            endforeach()
        endforeach()
    endif()
endif()

# Homebrew's Python.framework embeds a nested Python.app helper whose own
# dependency on the homebrew Python binary is not relocatable. It is pruned
# from the staged bundle after fixup, so ignore it during prerequisite checks.
if(KOTONOHA_SYSTEM_DEPS)
    # System provider: dependencies remain in their native prefix and the
    # executable keeps absolute paths. No relocation/copy is performed.
    message(STATUS
        "[Kotonoha] SYSTEM provider: using native libraries from the system prefix")
else()
    fixup_bundle("${APP_EXECUTABLE}" "${plugins}" "${DEPENDENCY_DIRS}"
        IGNORE_ITEM "Python")
endif()

# fixup_bundle relocates dependencies with install_name_tool, which invalidates
# the ad-hoc signature of every copied dylib. KotonohaLauncherCore is a shared
# library linked by the hub, so re-sign all bundled dylibs before the app runs.
if(APPLE)
    file(GLOB _kotonoha_bundled_dylibs "${BUNDLE_CONTENTS}/Frameworks/*.dylib")
    foreach(bundled_dylib IN LISTS _kotonoha_bundled_dylibs)
        execute_process(COMMAND codesign --force --sign - "${bundled_dylib}"
            RESULT_VARIABLE dylib_sign_result OUTPUT_QUIET ERROR_QUIET)
        if(NOT dylib_sign_result EQUAL 0)
            message(FATAL_ERROR "Could not ad-hoc sign bundled library ${bundled_dylib}")
        endif()
    endforeach()
endif()

# CMake's dependency fix-up can copy the original Python.framework again;
# normalize the final staged version before validating/signing the .app.
if(include_python AND python_framework)
    kotonoha_prune_nested_python_app("${python_version_dir}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckBundleContents.cmake")
foreach(plugin IN LISTS plugins)
    execute_process(COMMAND codesign --force --sign - "${plugin}"
        RESULT_VARIABLE plugin_sign_result)
    if(NOT plugin_sign_result EQUAL 0)
        message(FATAL_ERROR "Could not ad-hoc sign Python extension ${plugin}")
    endif()
endforeach()

get_filename_component(app_dir "${BUNDLE_CONTENTS}" DIRECTORY)
execute_process(COMMAND codesign --force --deep --sign - "${app_dir}"
    RESULT_VARIABLE sign_result)
if(NOT sign_result EQUAL 0)
    message(FATAL_ERROR "Could not ad-hoc sign ${app_dir}")
endif()
execute_process(COMMAND codesign --verify --deep --strict "${app_dir}"
    RESULT_VARIABLE verify_result)
if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "Could not verify the ad-hoc signature of ${app_dir}")
endif()
foreach(plugin IN LISTS plugins)
    execute_process(COMMAND codesign --verify --strict "${plugin}"
        RESULT_VARIABLE plugin_verify_result)
    if(NOT plugin_verify_result EQUAL 0)
        message(FATAL_ERROR "Could not verify the signature of Python extension ${plugin}")
    endif()
endforeach()
