# Platform-specific build choices live in the engine's CMake layer,
# never in individual game ports.
if(ANDROID OR EMSCRIPTEN)
    set(_kotonoha_host_tools OFF)
else()
    set(_kotonoha_host_tools ON)
endif()
# Cache INTERNAL makes these platform capabilities available to all sibling
# game directories even though this module is included from lib/CMakeLists.
set(KOTONOHA_BUILD_HOST_TOOLS ${_kotonoha_host_tools} CACHE INTERNAL
    "Build host-only inspectors from game ports" FORCE)
set(KOTONOHA_HAS_PYTHON_DEVELOPMENT ${_kotonoha_host_tools} CACHE INTERNAL
    "CPython Development components are available for host builds" FORCE)
if(EMSCRIPTEN)
    set(_kotonoha_web_python_stubs ON)
else()
    set(_kotonoha_web_python_stubs OFF)
endif()
set(KOTONOHA_USE_PYTHON_STUBS ${_kotonoha_web_python_stubs} CACHE INTERNAL
    "Use the browser-compatible Python stubs in port code" FORCE)

# A game declares *what* asset to ship; the engine decides how.
function(kotonoha_preload_asset target source destination)
    if(EMSCRIPTEN)
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "Missing preloaded asset: ${source}")
        endif()
        target_link_options(${target} PRIVATE
            "SHELL:--preload-file ${source}@${destination}")
    endif()
endfunction()

# Copia os assets do jogo e do engine para o diretório do executável/bundle
function(kotonoha_copy_assets target)
    cmake_parse_arguments(ASSETS "" "GAME;GAME_ASSET_SOURCE" "" ${ARGN})
    if(ASSETS_GAME_ASSET_SOURCE)
        set(_game_asset_source "${ASSETS_GAME_ASSET_SOURCE}")
    else()
        set(_game_asset_source "${KOTONOHA_PROJECT_ROOT}/games/${ASSETS_GAME}/game-assets")
    endif()

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
                "-DGAME_SOURCE=${_game_asset_source}"
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

# Deploy CMake-produced shared libraries AND resolved runtime dependencies
# into each game's output. Windows loads DLLs adjacent to the .exe; Linux
# and macOS non-bundle use <game>/libs with a relative runpath/install name.
# System libraries stay system-provided outside of Homebrew / vcpkg on macOS.
function(kotonoha_copy_runtime_libraries target dependency_dirs)
    if(ANDROID OR EMSCRIPTEN)
        return()
    endif()
    get_target_property(is_bundle ${target} MACOSX_BUNDLE)
    if(APPLE AND is_bundle)
        return() # BundleApp.cmake handles .app/Contents/Frameworks.
    endif()

    # These shared CMake targets must be shipped even if they are hidden
    # behind an indirect dependency or imported from a toolchain.
    set(_cmake_shared)
    foreach(_lib IN ITEMS KotonohaEngine imgui SDL3::SDL3 SDL3_ttf::SDL3_ttf)
        if(TARGET ${_lib})
            get_target_property(_alias ${_lib} ALIASED_TARGET)
            if(_alias)
                set(_real "${_alias}")
            else()
                set(_real "${_lib}")
            endif()
            get_target_property(_type ${_real} TYPE)
            if(_type STREQUAL "SHARED_LIBRARY" OR
               _type STREQUAL "MODULE_LIBRARY" OR
               _type STREQUAL "UNKNOWN_LIBRARY")
                list(APPEND _cmake_shared "$<TARGET_FILE:${_lib}>")
            endif()
        endif()
    endforeach()
    if(WIN32)
        set(_library_dir "$<TARGET_FILE_DIR:${target}>")
    else()
        set(_library_dir "$<TARGET_FILE_DIR:${target}>/libs")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DEXECUTABLE=$<TARGET_FILE:${target}>"
            "-DLIBRARY_DIR=${_library_dir}"
            "-DSEARCH_DIRS=${dependency_dirs}"
            "-DCMAKE_SHARED_LIBRARIES=${_cmake_shared}"
            "-DKOTONOHA_BUILD_LIBRARY_ROOT=${CMAKE_BINARY_DIR}"
            "-DKOTONOHA_VCPKG_ROOT=${VCPKG_INSTALLED_DIR}"
            -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/CopyRuntimeLibraries.cmake"
        VERBATIM)
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

# Helper to get the default dependency directories for tools/apps
function(kotonoha_get_default_dependency_dirs _out_dirs)
    if(ANDROID OR EMSCRIPTEN)
        set(${_out_dirs} "" PARENT_SCOPE)
        return()
    endif()

    set(_dirs)
    list(APPEND _dirs
        "$<TARGET_FILE_DIR:KotonohaEngine>"
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

    # Package only resolved runtime dependencies. macOS can resolve
    # Homebrew dylibs from Cellar/opt, with or without application bundles.
    # On Windows/Linux, use CMake-produced and toolchain/vcpkg libraries.
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        list(APPEND _dirs
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib")
    endif()
    if(APPLE)
        list(APPEND _dirs "/opt/homebrew/lib" "/usr/local/lib")
        find_program(_kotonoha_brew brew)
        if(_kotonoha_brew)
            execute_process(COMMAND "${_kotonoha_brew}" --prefix
                OUTPUT_VARIABLE _brew_prefix
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
            if(_brew_prefix)
                list(APPEND _dirs "${_brew_prefix}/lib")
                file(GLOB _brew_opt_libs LIST_DIRECTORIES TRUE
                    "${_brew_prefix}/opt/*/lib")
                list(APPEND _dirs ${_brew_opt_libs})
            endif()
        endif()
    endif()
    list(REMOVE_DUPLICATES _dirs)
    set(${_out_dirs} "${_dirs}" PARENT_SCOPE)
endfunction()

# Configurações gerais do app por plataforma
function(kotonoha_configure_app target)
    cmake_parse_arguments(APP "" "BUNDLE_ID;GAME;GAME_ASSET_SOURCE;PYTHON_STDLIB;PYTHON_VERSION" "" ${ARGN})
    if(APP_GAME_ASSET_SOURCE)
        set(_game_asset_source "${APP_GAME_ASSET_SOURCE}")
    else()
        set(_game_asset_source "${KOTONOHA_PROJECT_ROOT}/games/${APP_GAME}/game-assets")
    endif()

    # Register configured desktop/browser applications for CPack. Registration
    # happens when the actual CMake target exists, not from game name guesses.
    if(NOT ANDROID)
        set_property(GLOBAL APPEND PROPERTY KOTONOHA_PACKAGE_TARGETS ${target})
    endif()

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
                "SHELL:-sINITIAL_MEMORY=${KOTONOHA_WASM_INITIAL_MEMORY}"
                "SHELL:-sMAXIMUM_MEMORY=${KOTONOHA_WASM_MAX_MEMORY}"
                # Asyncify lets synchronous VFS callers yield while the browser
                # completes an asynchronous Fetch (not a blocking network call).
                # Keep all engine/SDL stacks instrumented by linking the final
                # application with Asyncify, not just the static engine archive.
                # Release defaults to -O3. Some Emscripten/Binaryen versions
                # have emitted broken Asyncify exports under aggressive -O3
                # optimisation. Keep the final WASM link at -O2 until verified.
                # Position matters: this target option follows CMake's -O3.
                "SHELL:-O2"
                "SHELL:-sASYNCIFY=1"
                "SHELL:-sASYNCIFY_STACK_SIZE=262144"
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

            # Detect a mismatched/broken JS + WASM Asyncify pair at build
            # time, before browsers can abort with _asyncify_start_unwind.
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND python3 "${KOTONOHA_PROJECT_ROOT}/tools/check_wasm_asyncify.py"
                        "$<TARGET_FILE:${target}>"
                COMMENT "Checking required Asyncify exports in WASM binary"
                VERBATIM
            )

            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND python3 "${KOTONOHA_PROJECT_ROOT}/tools/generate_manifest.py"
                        "${_game_asset_source}"
                        "${KOTONOHA_OUTPUT_CONFIG_DIR}/${target}/game-assets/manifest.json"
                COMMENT "Generating manifest.json for lazy loading"
            )
        endif()

        kotonoha_copy_assets(${target} GAME "${APP_GAME}"
            GAME_ASSET_SOURCE "${_game_asset_source}")
    endif()
endfunction()

