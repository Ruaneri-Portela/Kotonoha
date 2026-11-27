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
# FindPython3 always resolves the HOST CPython. Linking its Development
# artifacts into a cross-compiled binary pulls Mach-O/ELF libpython plus
# macOS-only flags (-framework CoreFoundation, -ldl) into the MinGW link.
# vcpkg does not provide a MinGW CPython port, so cross builds use the same
# interpreter stubs as WebAssembly.
set(_kotonoha_host_python_usable ON)
if(EMSCRIPTEN OR MINGW OR (CMAKE_CROSSCOMPILING AND WIN32))
    set(_kotonoha_host_python_usable OFF)
endif()
if(_kotonoha_host_python_usable)
    set(_kotonoha_python_development ON)
else()
    set(_kotonoha_python_development OFF)
endif()
set(KOTONOHA_HAS_PYTHON_DEVELOPMENT ${_kotonoha_python_development} CACHE INTERNAL
    "CPython Development components are available for native builds" FORCE)
if(_kotonoha_host_python_usable)
    set(_kotonoha_web_python_stubs OFF)
else()
    set(_kotonoha_web_python_stubs ON)
endif()
set(KOTONOHA_USE_PYTHON_STUBS ${_kotonoha_web_python_stubs} CACHE INTERNAL
    "Use the browser-compatible Python stubs in port code" FORCE)

include("${CMAKE_CURRENT_LIST_DIR}/../packaging/KotonohaAppPackaging.cmake")

# design/generated/ is the versioned source of truth for the visual identity.
# Every buildsystem imports only the specific files it needs from there; the
# folder itself is never packaged as a generic engine/game asset.
# CACHE INTERNAL keeps the path visible to sibling game/launcher directories
# that call kotonoha_configure_app(): a plain set() is scoped to lib/ and the
# callers would resolve "${KOTONOHA_BRAND_OUTPUT}/generic/icon.icns" to
# "/generic/icon.icns", which fails target_sources() in every port.
set(KOTONOHA_BRAND_OUTPUT "${KOTONOHA_PROJECT_ROOT}/design/generated"
    CACHE INTERNAL "Versioned Kotonoha visual identity outputs")
if(NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    # This module is included from lib/, while kotonoha_configure_app() is
    # called from sibling game directories. A local directory variable would
    # expand to an empty string there (1 ICON "/generic/icon.ico" on Windows).
    # INTERNAL cache exposes the configured output path to every game target.
    set(KOTONOHA_BRAND_OUTPUT "${CMAKE_BINARY_DIR}/kotonoha-brand"
        CACHE INTERNAL "Generated desktop branding assets" FORCE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${KOTONOHA_PROJECT_ROOT}/tools/generate_brand_assets.py")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}"
            "${KOTONOHA_PROJECT_ROOT}/tools/generate_brand_assets.py"
            --kind desktop --out "${KOTONOHA_BRAND_OUTPUT}"
        RESULT_VARIABLE _brand_status)
    if(NOT _brand_status EQUAL 0)
        message(FATAL_ERROR "Cannot generate Kotonoha platform icons")
    endif()
endif()

# WebAssembly preloads only a staged copy of the engine's SPLASH.PNG; the
# design/ folder itself never enters the --preload-file MEMFS payload.
if(EMSCRIPTEN)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    # The staging directory is consumed inside kotonoha_configure_app() from
    # sibling game directories. Without CACHE INTERNAL it expands empty there
    # and --preload-file becomes "@/assets", which makes file_packager fail.
    set(KOTONOHA_WEB_ENGINE_ASSETS "${CMAKE_BINARY_DIR}/kotonoha-web-engine-assets"
        CACHE INTERNAL "Staged engine assets for WASM preloads" FORCE)
    file(MAKE_DIRECTORY "${KOTONOHA_WEB_ENGINE_ASSETS}")
    file(COPY "${KOTONOHA_ENGINE_ASSETS}/" DESTINATION "${KOTONOHA_WEB_ENGINE_ASSETS}")
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${KOTONOHA_PROJECT_ROOT}/tools/generate_brand_assets.py"
        --kind web --out "${KOTONOHA_BRAND_OUTPUT}"
        RESULT_VARIABLE _web_brand_status)
    if(NOT _web_brand_status EQUAL 0)
        message(FATAL_ERROR "Cannot generate Kotonoha WebApp branding")
    endif()
    # Only this single engine asset is staged for the WASM preload; importing
    # the whole design/ tree would bloat MEMFS and leak design sources.
    configure_file(
        "${KOTONOHA_BRAND_OUTPUT}/splash/SPLASH.PNG"
        "${KOTONOHA_WEB_ENGINE_ASSETS}/SPLASH.PNG"
        COPYONLY)
endif()

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
    cmake_parse_arguments(APP "" "BUNDLE_ID;GAME;GAME_ASSET_SOURCE;PYTHON_STDLIB;PYTHON_VERSION" "" ${ARGN})

    # Only the unified launcher is presented as "Kotonoha"; each game bundle
    # keeps its own game/engine display name.
    if(target STREQUAL "KotonohaLauncher")
        set(_display_name "Kotonoha")
    else()
        set(_display_name "${target}")
    endif()
    if(APP_GAME_ASSET_SOURCE)
        set(_game_asset_source "${APP_GAME_ASSET_SOURCE}")
    else()
        set(_game_asset_source "${KOTONOHA_PROJECT_ROOT}/games/${APP_GAME}/game-assets")
    endif()

    # Embed the canonical icon in every Windows .exe (Explorer, taskbar, ZIP).
    # VERSIONINFO carries the player-facing name: without it Windows falls
    # back to the target/filename ("KotonohaLauncher") in Explorer and Task
    # Manager. Only the unified launcher is presented as "Kotonoha"; every
    # individual game port keeps its own name.
    if(WIN32)
    # School Days has its own primary WinRes icon (resource ID 1), supplied
    # by the fork. Do not append the generic engine icon with the same ID.
    # Other Windows game ports use the shared K branding.
    if(WIN32 AND NOT APP_GAME STREQUAL "school-days")
        set(_winres "${CMAKE_CURRENT_BINARY_DIR}/${target}-brand.rc")
        if(KOTONOHA_BRAND_OUTPUT AND EXISTS "${KOTONOHA_BRAND_OUTPUT}/generic/icon.ico")
            file(TO_CMAKE_PATH "${KOTONOHA_BRAND_OUTPUT}/generic/icon.ico" _icon_path)
            string(REPLACE "." "," _win_version "${PROJECT_VERSION}.0")
            file(WRITE "${_winres}"
                "1 ICON \"${_icon_path}\"\n"
                "VS_VERSION_INFO VERSIONINFO\n"
                " FILEVERSION ${_win_version}\n"
                " PRODUCTVERSION ${_win_version}\n"
                " FILEFLAGSMASK 0x3fL\n"
                " FILEFLAGS 0x0L\n"
                " FILEOS 0x40004L\n"
                " FILETYPE 0x1L\n"
                " FILESUBTYPE 0x0L\n"
                "BEGIN\n"
                "    BLOCK \"StringFileInfo\"\n"
                "    BEGIN\n"
                "        BLOCK \"040904b0\"\n"
                "        BEGIN\n"
                "            VALUE \"CompanyName\", \"Kotonoha contributors\"\n"
                "            VALUE \"FileDescription\", \"${_display_name}\"\n"
                "            VALUE \"FileVersion\", \"${PROJECT_VERSION}\"\n"
                "            VALUE \"ProductName\", \"${_display_name}\"\n"
                "            VALUE \"ProductVersion\", \"${PROJECT_VERSION}\"\n"
                "        END\n"
                "    END\n"
                "    BLOCK \"VarFileInfo\"\n"
                "    BEGIN\n"
                "        VALUE \"Translation\", 0x409, 1200\n"
                "    END\n"
                "END\n")
            target_sources(${target} PRIVATE "${_winres}")
        else()
            message(FATAL_ERROR
                "Kotonoha brand icon missing for ${target}: ${KOTONOHA_BRAND_OUTPUT}")
        endif()
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
            MACOSX_BUNDLE_INFO_PLIST "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${_display_name}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_ID}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_ICON_FILE "icon.icns"
            BUILD_RPATH "@executable_path/../Frameworks"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        )

        kotonoha_configure_app_output(${target})

        if(KOTONOHA_BRAND_OUTPUT AND EXISTS "${KOTONOHA_BRAND_OUTPUT}/generic/icon.icns")
            set(icon "${KOTONOHA_BRAND_OUTPUT}/generic/icon.icns")
            set_source_files_properties("${icon}" PROPERTIES
                MACOSX_PACKAGE_LOCATION Resources
                HEADER_FILE_ONLY TRUE
            )
            target_sources(${target} PRIVATE "${icon}")
        else()
            message(FATAL_ERROR
                "Kotonoha brand icon missing for ${target}: ${KOTONOHA_BRAND_OUTPUT}")
        endif()

        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/BundleApp.cmake"
            "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/PruneNestedPythonApp.cmake"
            "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/CheckBundleContents.cmake"
            "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/PackageAssets.cmake"
            "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/CheckBundledAssets.cmake"
        )

        # Assets must be in place before BundleApp validates the bundle.
        kotonoha_copy_assets(${target} GAME "${APP_GAME}"
            GAME_ASSET_SOURCE "${_game_asset_source}")

        # With a system package provider the bundle is intentionally not
        # relocated: binaries keep absolute /opt/local (or Homebrew) paths and
        # resolve through the native installation. vcpkg builds are self
        # contained and go through CMake's normal fixup_bundle.
        set(_kotonoha_system_deps OFF)
        if(KOTONOHA_DEPS_PROVIDER_RESOLVED STREQUAL "SYSTEM")
            set(_kotonoha_system_deps ON)
        endif()

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DAPP_EXECUTABLE=$<TARGET_FILE:${target}>"
                "-DBUNDLE_CONTENTS=$<TARGET_BUNDLE_CONTENT_DIR:${target}>"
                "-DAPP_GAME=${APP_GAME}"
                "-DKOTONOHA_SYSTEM_DEPS=${_kotonoha_system_deps}"
                "-DKOTONOHA_GAME_HOST=$<IF:$<AND:$<STREQUAL:${APP_GAME},launcher>,$<TARGET_EXISTS:KotonohaGameHost>>,$<TARGET_FILE:KotonohaGameHost>,>"
                "-DDEPENDENCY_DIRS=${_kotonoha_dependency_dirs}"
                "-DPYTHON_STDLIB=${APP_PYTHON_STDLIB}"
                "-DPYTHON_VERSION=${APP_PYTHON_VERSION}"
                -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/BundleApp.cmake"
            VERBATIM)

        if(BUILD_TESTING)
            add_test(NAME "Kotonoha.BundleLayout.${target}"
                COMMAND "${CMAKE_COMMAND}"
                    "-DBUNDLE_CONTENTS=$<TARGET_BUNDLE_CONTENT_DIR:${target}>"
                    "-DAPP_GAME=${APP_GAME}"
                    "-DPYTHON_VERSION=${APP_PYTHON_VERSION}"
                    -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/CheckBundleContents.cmake"
            )
            set_tests_properties("Kotonoha.BundleLayout.${target}" PROPERTIES LABELS "synthetic")
        endif()
    else()
        # 2. Configuração para plataformas fora do macOS Bundle (Windows/Linux/Executáveis padrão)
        kotonoha_configure_app_output(${target})
        if(NOT EMSCRIPTEN)
            kotonoha_copy_runtime_libraries(${target} "${_kotonoha_dependency_dirs}")
        endif()
        # Every MinGW .exe needs its GCC runtime and the full vcpkg bin set
        # beside it, regardless of build type, target or selected game ports.
        kotonoha_deploy_mingw_runtime(${target})

        if(EMSCRIPTEN)
            if(NOT KOTONOHA_WEB_ENGINE_ASSETS OR
               NOT IS_DIRECTORY "${KOTONOHA_WEB_ENGINE_ASSETS}")
                message(FATAL_ERROR
                    "Kotonoha WASM asset staging directory missing for ${target}: ${KOTONOHA_WEB_ENGINE_ASSETS}")
            endif()
            target_compile_options(${target} PRIVATE
                -fexceptions
            )
            target_link_options(${target} PRIVATE
                -fexceptions
                "SHELL:-sDISABLE_EXCEPTION_CATCHING=0"
                "SHELL:-sEXPORTED_RUNTIME_METHODS=['FS','ccall','FS_readFile','HEAPU8']"
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
                "SHELL:-lidbfs.js"
                "SHELL:--preload-file ${KOTONOHA_WEB_ENGINE_ASSETS}@/assets"
            )

            # The browser tab/PWA name must follow the player-facing name,
            # not the CMake target ("KotonohaLauncher" for the unified hub).
            set(APP_TITLE "${_display_name}")
            set(TARGET_NAME "${target}")
            # Every output directory is a standalone PWA with its own
            # start URL, installation identity and Service Worker scope.
            # A stable cache key keeps installations isolated by release.
            string(SHA256 PWA_CACHE_KEY
                "${target}|${KOTONOHA_VERSION_FULL}|${CMAKE_BUILD_TYPE}")

            configure_file(
                "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/index.html.in"
                "${KOTONOHA_OUTPUT_CONFIG_DIR}/${target}/index.html"
                @ONLY
            )

            configure_file(
                "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/pwa.webmanifest.in"
                "${KOTONOHA_OUTPUT_CONFIG_DIR}/${target}/manifest.webmanifest"
                @ONLY
            )
            configure_file(
                "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/service-worker.js.in"
                "${KOTONOHA_OUTPUT_CONFIG_DIR}/${target}/service-worker.js"
                @ONLY
            )
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND python3
                    "${KOTONOHA_PROJECT_ROOT}/tools/generate_pwa_icons.py"
                    "${target}"
                    "${KOTONOHA_OUTPUT_CONFIG_DIR}/${target}"
                COMMENT "Generating installable PWA icons for ${target}"
                VERBATIM
            )

            # Serve the persistent IndexedDB/HTTP bridge beside the generated
            # shell. It must load before the Emscripten main JS file.
            configure_file(
                "${KOTONOHA_PROJECT_ROOT}/lib/web/KotonohaPreloadCache.js"
                "${KOTONOHA_OUTPUT_CONFIG_DIR}/${target}/KotonohaPreloadCache.js"
                COPYONLY
            )

            # Detect a mismatched/broken JS + WASM Asyncify pair at build
            # time, before browsers can abort with _asyncify_start_unwind.
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND python3 "${KOTONOHA_PROJECT_ROOT}/tools/check_wasm_asyncify.py"
                        "$<TARGET_FILE:${target}>"
                COMMENT "Checking required Asyncify exports in WASM binary"
                VERBATIM
            )

        endif()

        kotonoha_copy_assets(${target} GAME "${APP_GAME}"
            GAME_ASSET_SOURCE "${_game_asset_source}")
        if(EMSCRIPTEN)
            # Index the final served directory after the asset copy.
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND python3 "${KOTONOHA_PROJECT_ROOT}/tools/generate_manifest.py"
                        "${KOTONOHA_OUTPUT_CONFIG_DIR}/${target}/game-assets"
                        "${KOTONOHA_OUTPUT_CONFIG_DIR}/${target}/manifest.json"
                COMMENT "Generating mandatory HTTP manifest.json beside index.html"
                VERBATIM
            )
        endif()
    endif()
    # Every native game must parse --help without retail assets or a running
    # renderer. This catches broken SDL callback entrypoints and missing
    # runtime dependencies across *all* enabled ports.
    if(BUILD_TESTING AND NOT ANDROID AND NOT EMSCRIPTEN AND
       NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_test(NAME "Kotonoha.PortHelp.${target}"
            COMMAND $<TARGET_FILE:${target}> --help)
        set_tests_properties("Kotonoha.PortHelp.${target}" PROPERTIES
            TIMEOUT 20
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
    endif()
endfunction()

