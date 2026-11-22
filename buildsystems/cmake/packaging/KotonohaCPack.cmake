# Build one relocatable ZIP per configured game from its output directory.
# The ordinary build remains unchanged; invoke "cmake --build build --target
# package" after building the desired game targets. CPack uses the same output
# layout as the actual apps (assets, DLLs, dylibs and ELF shared libraries).
#
# Commercial user-supplied game-assets are deliberately omitted by default.
# This avoids accidentally redistributing copyrighted retail data in CI.
if(ANDROID)
    return() # Android packaging is handled by Gradle APK/AAB.
endif()

option(KOTONOHA_CPACK_INCLUDE_GAME_ASSETS
    "Explicitly include local retail game-assets in ZIP archives" OFF)
if(KOTONOHA_CPACK_INCLUDE_GAME_ASSETS)
    message(WARNING "[Kotonoha] CPack will include local game-assets. Do not distribute archives containing copyrighted game data without the rights to do so.")
endif()

get_property(_kotonoha_zip_targets GLOBAL PROPERTY KOTONOHA_PACKAGE_TARGETS)
if(NOT _kotonoha_zip_targets)
    message(STATUS "[Kotonoha] No game targets enabled; CPack ZIP skipped")
    return()
endif()
list(REMOVE_DUPLICATES _kotonoha_zip_targets)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "Kotonoha")
set(CPACK_PACKAGE_VENDOR "Kotonoha Engine")
set(CPACK_PACKAGE_CONTACT "Kotonoha Contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Kotonoha SDL3 game ports and their runtime dependencies")
set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(_kotonoha_package_identity "${KOTONOHA_VERSION_FULL}")
string(REGEX REPLACE "[^A-Za-z0-9._-]" "-" _kotonoha_package_identity
    "${_kotonoha_package_identity}")
set(CPACK_PACKAGE_FILE_NAME
    "Kotonoha-${_kotonoha_package_identity}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING IGNORE)
set(CPACK_COMPONENTS_ALL ${_kotonoha_zip_targets})

foreach(_target IN LISTS _kotonoha_zip_targets)
    if(APPLE AND KOTONOHA_BUILD_MACOS_BUNDLES)
        # Preserve the bundle layout, but never package user-supplied retail
        # game-assets accidentally. This must match the non-bundle path below.
        set(_app_source "${KOTONOHA_OUTPUT_CONFIG_DIR}/${_target}.app")
        if(KOTONOHA_CPACK_INCLUDE_GAME_ASSETS OR _target STREQUAL "Kotonoha")
            install(DIRECTORY "${_app_source}"
                DESTINATION "."
                COMPONENT "${_target}"
                PATTERN "*.lock" EXCLUDE)
        else()
            install(DIRECTORY "${_app_source}"
                DESTINATION "."
                COMPONENT "${_target}"
                PATTERN "game-assets" EXCLUDE
                PATTERN "*.lock" EXCLUDE)
        endif()
    else()
        set(_app_source "${KOTONOHA_OUTPUT_CONFIG_DIR}/${_target}")
        if(KOTONOHA_CPACK_INCLUDE_GAME_ASSETS OR _target STREQUAL "Kotonoha")
            install(DIRECTORY "${_app_source}/"
                DESTINATION "${_target}"
                COMPONENT "${_target}"
                PATTERN "*.lock" EXCLUDE
                )
        else()
            install(DIRECTORY "${_app_source}/"
                DESTINATION "${_target}"
                COMPONENT "${_target}"
                PATTERN "game-assets" EXCLUDE
                PATTERN "*.lock" EXCLUDE
                )
        endif()
    endif()
    # Include the engine license and third-party notices in every ZIP
    # component. For macOS bundles keep them in the application resources.
    if(APPLE AND KOTONOHA_BUILD_MACOS_BUNDLES)
        set(_kotonoha_legal_dir "${_target}.app/Contents/Resources/Legal")
    else()
        set(_kotonoha_legal_dir "${_target}/licenses")
    endif()
    install(FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "${_kotonoha_legal_dir}"
        COMPONENT "${_target}")
    install(FILES
        "${PROJECT_SOURCE_DIR}/assets/Anime4K/LICENSE-Anime4K.txt"
        "${PROJECT_SOURCE_DIR}/assets/ArtCNN/LICENSE-ArtCNN.txt"
        "${PROJECT_SOURCE_DIR}/modules/providers/amagami/third_party/open-amagami-LICENSE.txt"
        "${PROJECT_SOURCE_DIR}/games/reallive/vendor/libreallive/LICENSE"
        DESTINATION "${_kotonoha_legal_dir}/third-party"
        COMPONENT "${_target}")
    # Fail loudly instead of silently producing an empty archive when
    # consumers run "cpack" before compiling an enabled application.
    install(CODE "
        if(NOT EXISTS \"${_app_source}\")
            message(FATAL_ERROR
                \"Kotonoha game output is missing: ${_app_source}. Build ${_target} before packaging.\")
        endif()
    " COMPONENT "${_target}")
    set("CPACK_COMPONENT_${_target}_DISPLAY_NAME" "${_target}")
endforeach()

# Compatibility: enable use of generator expressions in install paths on
# multi-config generators. CPack uses CMAKE_INSTALL_CONFIG_NAME at install
# time and resolves the generated component destinations correctly.
include(CPack)
message(STATUS "[Kotonoha] ZIP components: ${_kotonoha_zip_targets}")
message(STATUS "[Kotonoha] ZIP packages: ${CPACK_PACKAGE_DIRECTORY}")
