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

# Copia DLLs (.dll) e Bibliotecas Compartilhadas (.so / .dylib) via vcpkg / CMake
function(kotonoha_copy_game_dependencies target)
    if(WIN32)
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
    elseif(UNIX AND NOT APPLE AND NOT ANDROID)
        # No Linux, copia bibliotecas compartilhadas (.so) que estejam como dependência em runtime
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                "$<TARGET_FILE_DIR:${target}>"
            COMMAND_EXPAND_LISTS
            VERBATIM)
            
        # Garante que o executável encontre as .so na própria pasta ($ORIGIN)
        set_target_properties(${target} PROPERTIES
            BUILD_RPATH "\$ORIGIN"
            INSTALL_RPATH "\$ORIGIN"
        )
    endif()
endfunction()

# Cria o executável ou biblioteca compartilhada (Android)
function(kotonoha_add_app target)
    if(ANDROID)
        add_library(${target} SHARED ${ARGN})
    else()
        add_executable(${target} ${ARGN})
    endif()
    
    # Habilita implantação local do vcpkg globalmente para o target
    set_target_properties(${target} PROPERTIES VCPKG_APPLOCAL_DEPS ON)
endfunction()

# Configurações gerais do app por plataforma
function(kotonoha_configure_app target)
    cmake_parse_arguments(APP "" "BUNDLE_ID;GAME;PYTHON_STDLIB;PYTHON_VERSION" "" ${ARGN})

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

        set(search_dirs
            "$<TARGET_FILE_DIR:KotonohaEngine>"
            "$<TARGET_FILE_DIR:kotonoha_imgui>"
            "$<TARGET_FILE_DIR:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:SDL3_ttf::SDL3_ttf>"
            ${FFMPEG_LIBRARY_DIRS} 
            ${FFMPEG_STATIC_LIBRARY_DIRS}
            ${JSON_C_LIBRARY_DIRS}
        )

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
        kotonoha_copy_game_dependencies(${target})
    endif()

    # Copia os assets do jogo para o diretório final do binário
    kotonoha_copy_assets(${target} GAME "${APP_GAME}")
endfunction()