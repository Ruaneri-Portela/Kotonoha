option(KOTONOHA_BUILD_MACOS_BUNDLES "Build macOS application bundles" OFF)

# Copia os assets do jogo e do engine para o diretório do executável/bundle
function(kotonoha_copy_assets target)
    cmake_parse_arguments(ASSETS "" "GAME" "" ${ARGN})

    get_target_property(is_bundle ${target} MACOSX_BUNDLE)
    if(APPLE AND is_bundle)
        set(destination "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources")
    else()
        set(destination "$<TARGET_FILE_DIR:${target}>")
    endif()

    set(include_game ON)
    if(ASSETS_GAME AND APPLE AND is_bundle AND NOT ASSETS_GAME STREQUAL "example")
        set(include_game "${KOTONOHA_BUNDLE_LOCAL_GAME_ASSETS}")
    endif()

    if(ASSETS_GAME)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DRESOURCE_SOURCE=${KOTONOHA_ENGINE_ASSETS}"
                "-DGAME_SOURCE=${KOTONOHA_PROJECT_ROOT}/games/${ASSETS_GAME}/game-assets"
                "-DRESOURCE_DESTINATION=${destination}"
                "-DINCLUDE_GAME_ASSETS=${include_game}"
                -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/PackageAssets.cmake"
            VERBATIM)
    else()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DASSET_SOURCE=${KOTONOHA_ENGINE_ASSETS}"
                "-DASSET_DESTINATION=${destination}/assets"
                -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/CopyAssetDirectory.cmake"
            VERBATIM)
    endif()
endfunction()

# Copia DLLs (.dll) via vcpkg / CMake para o diretório do executável.
function(kotonoha_copy_game_dependencies target)
    # Não copiar DLLs/libs quando fora do bundle (macOS) e fora do vcpkg.
    if(NOT DEFINED VCPKG_TARGET_TRIPLET AND NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        return()
    endif()

    if(WIN32 AND NOT ANDROID)
        # Habilita a cópia automática do vcpkg para o executável deste target
        set_target_properties(${target} PROPERTIES VS_GLOBAL_VcpkgEnabled "true")
        set_target_properties(${target} PROPERTIES VS_GLOBAL_VcpkgApplocalDeps "true")

        # Cópia garantida via CMake runtime DLLs (para MinGW, Ninja ou bibliotecas fora do vcpkg)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                "$<TARGET_FILE_DIR:${target}>"
            COMMAND_EXPAND_LISTS
            VERBATIM)
    endif()
endfunction()

# Copia as bibliotecas compartilhadas para <target>/libs no Linux e no macOS
# sem bundle. No macOS os install names também são reescritos para @loader_path.
function(kotonoha_copy_runtime_libraries target dependency_dirs)
    message(STATUS "KOTONOHA_DEBUG: kotonoha_copy_runtime_libraries called for ${target}")
    if(ANDROID OR WIN32)
        return()
    endif()
    if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        return()
    endif()
    if(APPLE AND KOTONOHA_BUILD_MACOS_BUNDLES)
        return()
    endif()
    # Não copiar DLLs/libs quando fora do bundle (macOS) e fora do vcpkg.
    if(NOT DEFINED VCPKG_TARGET_TRIPLET AND NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DEXECUTABLE=$<TARGET_FILE:${target}>"
            "-DLIBRARY_DIR=$<TARGET_FILE_DIR:${target}>/libs"
            "-DSEARCH_DIRS=${dependency_dirs}"
            -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/CopyRuntimeLibraries.cmake"
        VERBATIM)
    message(STATUS "KOTONOHA_DEBUG: add_custom_command called for ${target}")
endfunction()

# Cria o executável ou biblioteca compartilhada (Android)
function(kotonoha_add_app target)
    if(ANDROID)
        add_library(${target} SHARED ${ARGN})
    elseif(EMSCRIPTEN)
        add_executable(${target} ${ARGN})
    else()
        add_executable(${target} ${ARGN})
    endif()

    # Habilita implantação local do vcpkg globalmente para o target
    set_target_properties(${target} PROPERTIES VCPKG_APPLOCAL_DEPS ON)
endfunction()

# Helper to get the default dependency directories for tools/apps
function(kotonoha_get_default_dependency_dirs _out_dirs)
    if(ANDROID OR EMSCRIPTEN)
        set(${_out_dirs} "" PARENT_SCOPE)
        return()
    endif()

    set(_dirs)
    list(APPEND _dirs
        "$<TARGET_FILE_DIR:KotonohaEngine>"
        "$<TARGET_FILE_DIR:imgui>"
        "$<TARGET_FILE_DIR:SDL3::SDL3>"
        "$<TARGET_FILE_DIR:SDL3_ttf::SDL3_ttf>"
        ${LIBASS_LIBRARY_DIRS}
        ${FFMPEG_LIBRARY_DIRS}
        ${FFMPEG_STATIC_LIBRARY_DIRS}
        ${JSON_C_LIBRARY_DIRS}
    )

    if(FREETYPE_LIBRARY_RELEASE)
        get_filename_component(freetype_directory "${FREETYPE_LIBRARY_RELEASE}" DIRECTORY)
        list(APPEND _dirs "${freetype_directory}")
    endif()

    list(REMOVE_DUPLICATES _dirs)
    set(${_out_dirs} "${_dirs}" PARENT_SCOPE)
endfunction()

# Configurações gerais do app por plataforma
function(kotonoha_configure_app target)
    cmake_parse_arguments(APP "" "BUNDLE_ID;GAME;PYTHON_STDLIB;PYTHON_VERSION" "" ${ARGN})

    # Diretórios usados para localizar dependências de runtime.
    kotonoha_get_default_dependency_dirs(_kotonoha_dependency_dirs)

    # 1. Configuração específica para macOS (.app bundle)
    if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND KOTONOHA_BUILD_MACOS_BUNDLES)
        set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${target}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_ID}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_ICON_FILE "icon.icns"
            BUILD_RPATH "@executable_path/../Frameworks"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        )

        kotonoha_configure_app_output(${target})

        set(icon "${KOTONOHA_ENGINE_ASSETS}/icon.icns")
        set_source_files_properties("${icon}" PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources
            HEADER_FILE_ONLY TRUE
        )
        target_sources(${target} PRIVATE "${icon}")

        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/BundleApp.cmake"
            "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/CheckBundleContents.cmake"
            "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/PackageAssets.cmake"
            "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/CheckBundledAssets.cmake"
        )

        # Assets must be in place before BundleApp validates the bundle.
        kotonoha_copy_assets(${target} GAME "${APP_GAME}")

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DAPP_EXECUTABLE=$<TARGET_FILE:${target}>"
                "-DBUNDLE_CONTENTS=$<TARGET_BUNDLE_CONTENT_DIR:${target}>"
                "-DAPP_GAME=${APP_GAME}"
                "-DDEPENDENCY_DIRS=${_kotonoha_dependency_dirs}"
                "-DPYTHON_STDLIB=${APP_PYTHON_STDLIB}"
                "-DPYTHON_VERSION=${APP_PYTHON_VERSION}"
                -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/BundleApp.cmake"
            VERBATIM)

        if(BUILD_TESTING)
            add_test(NAME "Kotonoha.BundleLayout.${target}"
                COMMAND "${CMAKE_COMMAND}"
                    "-DBUNDLE_CONTENTS=$<TARGET_BUNDLE_CONTENT_DIR:${target}>"
                    "-DAPP_GAME=${APP_GAME}"
                    "-DPYTHON_VERSION=${APP_PYTHON_VERSION}"
                    -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/CheckBundleContents.cmake"
            )
        endif()
    else()
        # 2. Configuração para plataformas fora do macOS Bundle (Windows/Linux/Executáveis padrão)
        kotonoha_configure_app_output(${target})
        if(NOT EMSCRIPTEN)
            kotonoha_copy_game_dependencies(${target})
            kotonoha_copy_runtime_libraries(${target} "${_kotonoha_dependency_dirs}")
        endif()

        if(EMSCRIPTEN)
            target_compile_options(${target} PRIVATE
                -fexceptions
            )
            target_link_options(${target} PRIVATE
                -fexceptions
                "SHELL:-sDISABLE_EXCEPTION_CATCHING=0"
                "SHELL:-sEXPORTED_RUNTIME_METHODS=['FS','ccall','FS_readFile']"
                "SHELL:-sFORCE_FILESYSTEM=1"
                "SHELL:-sALLOW_MEMORY_GROWTH=1"
                "SHELL:-sINITIAL_MEMORY=268435456"
                "SHELL:-sFETCH=1"
                "SHELL:-sFETCH_STREAMING=1"
                "SHELL:-lidbfs.js"
                "SHELL:--preload-file ${KOTONOHA_ENGINE_ASSETS}@/assets"
            )

            set(APP_TITLE "${target}")
            set(TARGET_NAME "${target}")

            configure_file(
                "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/index.html.in"
                "${KOTONOHA_OUTPUT_CONFIG_DIR}/${target}/index.html"
                @ONLY
            )

            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND python3 "${KOTONOHA_PROJECT_ROOT}/tools/generate_manifest.py"
                        "${KOTONOHA_PROJECT_ROOT}/games/${APP_GAME}/game-assets"
                        "${KOTONOHA_OUTPUT_CONFIG_DIR}/${target}/game-assets/manifest.json"
                COMMENT "Generating manifest.json for lazy loading"
            )
        endif()

        kotonoha_copy_assets(${target} GAME "${APP_GAME}")
    endif()
endfunction()

