# kotonoha_configure_app(<target>
#     [INCLUDE_ASSETS_SCRIPTS]
#     OUTPUT_NAME <name>          executable / bundle name (Kotonoha, SchoolDays)
#     BUNDLE_ID   <identifier>    macOS bundle identifier)
#
# Shared packaging for desktop applications: Windows icon resource, and on macOS
# the .app bundle with libKotonoha.dylib and recursive dylibs in Frameworks,
# the icon and the engine assets in Resources/assets.
function(kotonoha_configure_app target)
    cmake_parse_arguments(APP "INCLUDE_ASSETS_SCRIPTS" "OUTPUT_NAME;BUNDLE_ID" "" ${ARGN})
    if(NOT APP_OUTPUT_NAME)
        set(APP_OUTPUT_NAME "${target}")
    endif()
    set_target_properties(${target} PROPERTIES
            OUTPUT_NAME "${APP_OUTPUT_NAME}"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND
            EXISTS "${CMAKE_SOURCE_DIR}/assets/icon.ico")
        target_compile_definitions(${target} PRIVATE KOTONOHA_HAS_WINDOWS_ICON=1)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_sources(${target} PRIVATE
                "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/Kotonoha.rc")
    endif()

    if(NOT APPLE)
        return()
    endif()

    set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST
            "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${APP_OUTPUT_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_ID}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_ICON_FILE "icon.icns"
            BUILD_RPATH "@executable_path/../Frameworks")

    set(icon_file "${KOTONOHA_ASSETS_DIR}/icon.icns")
    if(NOT EXISTS "${icon_file}")
        message(FATAL_ERROR "Ícone macOS não encontrado: ${icon_file}")
    endif()
    set_source_files_properties("${icon_file}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
            HEADER_FILE_ONLY TRUE)
    target_sources(${target} PRIVATE "${icon_file}")

    # --- Resolução Recursiva de Bibliotecas (.dylib) em Frameworks ---
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:KotonohaRuntime>"
            "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks"
            VERBATIM)

    # Script pós-build apontando para o executável exato no bundle
    set(FIXUP_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/fixup_${target}.cmake")
    file(GENERATE OUTPUT "${FIXUP_SCRIPT}" CONTENT "
        include(BundleUtilities)

        # Aponta para o binário do executável dentro do bundle macOS
        set(APP_EXEC \"$<TARGET_FILE:${target}>\")

        # Inclui os diretórios de build e do vcpkg na busca por dylibs
        set(LIBS_SEARCH_DIRS
            \"$<TARGET_FILE_DIR:KotonohaRuntime>\"
            \"${CMAKE_BINARY_DIR}\"
            \"${CMAKE_BINARY_DIR}/vcpkg_installed\"
            \"/usr/local/lib\"
            \"/opt/homebrew/lib\"
        )

        # Executa a cópia recursiva e o re-link via install_name_tool
        fixup_bundle(\"\${APP_EXEC}\" \"\" \"\${LIBS_SEARCH_DIRS}\")
    ")

    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -P "${FIXUP_SCRIPT}"
            VERBATIM)

    # --- Empacotamento dos Assets em Resources/assets (Dentro da .app no macOS) ---
    if(EXISTS "${KOTONOHA_ASSETS_DIR}")
        file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS "${KOTONOHA_ASSETS_DIR}/*")
        foreach(asset_file IN LISTS asset_files)
            if(IS_DIRECTORY "${asset_file}" OR asset_file STREQUAL icon_file)
                continue()
            endif()

            file(RELATIVE_PATH asset_relative "${KOTONOHA_ASSETS_DIR}" "${asset_file}")

            # Ignora scripts/ nos assets do bundle .app por padrão, a menos que INCLUDE_ASSETS_SCRIPTS seja passado
            if(NOT APP_INCLUDE_ASSETS_SCRIPTS AND asset_relative MATCHES "^scripts/")
                continue()
            endif()

            get_filename_component(asset_dir "${asset_relative}" DIRECTORY)
            if(asset_dir)
                set(asset_location "Resources/assets/${asset_dir}")
            else()
                set(asset_location "Resources/assets")
            endif()
            set_source_files_properties("${asset_file}" PROPERTIES
                    MACOSX_PACKAGE_LOCATION "${asset_location}"
                    HEADER_FILE_ONLY TRUE)
            target_sources(${target} PRIVATE "${asset_file}")
        endforeach()
    endif()
endfunction()
