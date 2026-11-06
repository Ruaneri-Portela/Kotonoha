option(KOTONOHA_BUILD_MACOS_BUNDLES "Build macOS application bundles"
    OFF)

function(kotonoha_add_app target)
    if(WIN32)
        # kotonoha_configure_app deploys native targets before scanning the
        # dependency closure. vcpkg's earlier scan can see stale local DLLs.
        set(VCPKG_APPLOCAL_DEPS OFF)
    endif()
    if(ANDROID)
        add_library(${target} SHARED ${ARGN})
    else()
        add_executable(${target} ${ARGN})
    endif()
endfunction()

function(kotonoha_configure_app target)
    cmake_parse_arguments(APP "" "BUNDLE_ID;GAME;PYTHON_STDLIB;PYTHON_VERSION" "" ${ARGN})

    if((APP_PYTHON_STDLIB OR APP_PYTHON_VERSION) AND
       NOT APP_GAME STREQUAL "doki-doki-literature-club")
        message(FATAL_ERROR "Only Doki Doki Literature Club may bundle Python")
    endif()

    if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND KOTONOHA_BUILD_MACOS_BUNDLES)
        set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${KOTONOHA_PROJECT_ROOT}/cmake/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${target}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_ID}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_ICON_FILE "icon.icns"
            BUILD_RPATH "@executable_path/../Frameworks"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
        set(icon "${KOTONOHA_ENGINE_ASSETS}/icon.icns")
        set_source_files_properties("${icon}" PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources HEADER_FILE_ONLY TRUE)
        target_sources(${target} PRIVATE "${icon}")
    endif()

    kotonoha_copy_assets(${target} GAME "${APP_GAME}")

    if(WIN32 AND NOT ANDROID)
        set(windows_dependency_dirs
            "$<TARGET_FILE_DIR:KotonohaEngine>"
            "$<TARGET_FILE_DIR:kotonoha_imgui>"
            "$<TARGET_FILE_DIR:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:SDL3_ttf::SDL3_ttf>"
            ${FFMPEG_LIBRARY_DIRS} ${JSON_C_LIBRARY_DIRS} ${LIBASS_LIBRARY_DIRS})
        if(APP_GAME STREQUAL "doki-doki-literature-club" AND Python3_EXECUTABLE)
            get_filename_component(python_runtime_dir "${Python3_EXECUTABLE}" DIRECTORY)
            list(APPEND windows_dependency_dirs "${python_runtime_dir}")
        endif()
        foreach(library_dir IN LISTS FFMPEG_LIBRARY_DIRS JSON_C_LIBRARY_DIRS LIBASS_LIBRARY_DIRS)
            if(library_dir)
                get_filename_component(dependency_prefix "${library_dir}" DIRECTORY)
                list(APPEND windows_dependency_dirs "${dependency_prefix}/bin")
            endif()
        endforeach()
        if(FREETYPE_LIBRARY_RELEASE)
            get_filename_component(freetype_lib_dir "${FREETYPE_LIBRARY_RELEASE}" DIRECTORY)
            get_filename_component(freetype_prefix "${freetype_lib_dir}" DIRECTORY)
            list(APPEND windows_dependency_dirs "${freetype_prefix}/bin")
        endif()
        list(REMOVE_DUPLICATES windows_dependency_dirs)
        add_custom_command(TARGET ${target} POST_BUILD
            # Always refresh the four direct runtime DLLs before dependency
            # discovery; otherwise an older DLL already beside the executable
            # wins Windows' normal lookup order and gets repackaged again.
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:KotonohaEngine>" "$<TARGET_FILE_DIR:${target}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:kotonoha_imgui>" "$<TARGET_FILE_DIR:${target}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:${target}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:SDL3_ttf::SDL3_ttf>" "$<TARGET_FILE_DIR:${target}>"
            COMMAND "${CMAKE_COMMAND}"
                "-DAPP_EXECUTABLE=$<TARGET_FILE:${target}>"
                "-DAPP_DESTINATION=$<TARGET_FILE_DIR:${target}>"
                "-DDEPENDENCY_DIRS=${windows_dependency_dirs}"
                -P "${KOTONOHA_PROJECT_ROOT}/cmake/PackageWindowsRuntime.cmake"
            VERBATIM)
    endif()

    if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND KOTONOHA_BUILD_MACOS_BUNDLES)
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            "${KOTONOHA_PROJECT_ROOT}/cmake/BundleApp.cmake"
            "${KOTONOHA_PROJECT_ROOT}/cmake/CheckBundleContents.cmake"
            "${KOTONOHA_PROJECT_ROOT}/cmake/PackageAssets.cmake"
            "${KOTONOHA_PROJECT_ROOT}/cmake/CheckBundledAssets.cmake")
        set(search_dirs
            "$<TARGET_FILE_DIR:KotonohaEngine>"
            "$<TARGET_FILE_DIR:kotonoha_imgui>"
            "$<TARGET_FILE_DIR:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:SDL3_ttf::SDL3_ttf>"
            ${FFMPEG_LIBRARY_DIRS} ${FFMPEG_STATIC_LIBRARY_DIRS}
            ${JSON_C_LIBRARY_DIRS})
        if(FREETYPE_LIBRARY_RELEASE)
            get_filename_component(freetype_directory "${FREETYPE_LIBRARY_RELEASE}" DIRECTORY)
            list(APPEND search_dirs "${freetype_directory}")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DAPP_EXECUTABLE=$<TARGET_FILE:${target}>"
                "-DBUNDLE_CONTENTS=$<TARGET_BUNDLE_CONTENT_DIR:${target}>"
                "-DAPP_GAME=${APP_GAME}"
                "-DDEPENDENCY_DIRS=${search_dirs}"
                "-DPYTHON_STDLIB=${APP_PYTHON_STDLIB}"
                "-DPYTHON_VERSION=${APP_PYTHON_VERSION}"
                -P "${KOTONOHA_PROJECT_ROOT}/cmake/BundleApp.cmake"
            VERBATIM)
        if(BUILD_TESTING)
            add_test(NAME "Kotonoha.BundleLayout.${target}"
                COMMAND "${CMAKE_COMMAND}"
                    "-DBUNDLE_CONTENTS=$<TARGET_BUNDLE_CONTENT_DIR:${target}>"
                    "-DAPP_GAME=${APP_GAME}"
                    "-DPYTHON_VERSION=${APP_PYTHON_VERSION}"
                    -P "${KOTONOHA_PROJECT_ROOT}/cmake/CheckBundleContents.cmake")
        endif()
    elseif(WIN32)
        # A DLL can change without changing its import library. Re-run the
        # deployment step in that case so incremental builds launch new code.
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            "$<TARGET_FILE:KotonohaEngine>"
            "$<TARGET_FILE:kotonoha_imgui>"
            "$<TARGET_FILE:SDL3::SDL3>"
            "$<TARGET_FILE:SDL3_ttf::SDL3_ttf>")
        # 1. Copia DLLs de alvos dinamicos nativos do CMake
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
        )

        # Scan the executable after copying native targets. The transitive
        # import graph, not every installed vcpkg package, determines its DLLs.
        if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            get_filename_component(compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
            if(MSVC)
                find_program(KOTONOHA_DUMPBIN dumpbin HINTS "${compiler_directory}" REQUIRED)
                set(runtime_tool dumpbin)
                set(runtime_command "${KOTONOHA_DUMPBIN}")
            else()
                set(runtime_tool objdump)
                set(runtime_command "${CMAKE_OBJDUMP}")
            endif()
            set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
                "${KOTONOHA_PROJECT_ROOT}/cmake/CopyRuntimeDependencies.cmake")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}"
                    "-DAPP_EXECUTABLE=$<TARGET_FILE:${target}>"
                    "-DDEPENDENCY_DIRS=$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>;${compiler_directory}"
                    "-DCMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM=windows+pe"
                    "-DCMAKE_GET_RUNTIME_DEPENDENCIES_TOOL=${runtime_tool}"
                    "-DCMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND=${runtime_command}"
                    -P "${KOTONOHA_PROJECT_ROOT}/cmake/CopyRuntimeDependencies.cmake"
                VERBATIM
            )
        endif()
    endif()
endfunction()
