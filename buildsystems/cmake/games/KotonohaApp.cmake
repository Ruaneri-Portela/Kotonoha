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
if(EMSCRIPTEN)
    set(_kotonoha_python_development OFF)
else()
    set(_kotonoha_python_development ON)
endif()
set(KOTONOHA_HAS_PYTHON_DEVELOPMENT ${_kotonoha_python_development} CACHE INTERNAL
    "CPython Development components are available for native builds" FORCE)
if(EMSCRIPTEN)
    set(_kotonoha_web_python_stubs ON)
else()
    set(_kotonoha_web_python_stubs OFF)
endif()
set(KOTONOHA_USE_PYTHON_STUBS ${_kotonoha_web_python_stubs} CACHE INTERNAL
    "Use the browser-compatible Python stubs in port code" FORCE)

include("${CMAKE_CURRENT_LIST_DIR}/../packaging/KotonohaAppPackaging.cmake")

# Generate the same K icon for macOS/Linux/WinRes at configure time.
# Avoid checked-in generated output and stale legacy assets/icon.ico/icon.icns.
if(NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    # CACHE INTERNAL makes the generated icon directory visible to every
    # sibling game/launcher directory that calls kotonoha_configure_app().
    # A plain set() is scoped to this directory only, so callers see an empty
    # value and resolve "${KOTONOHA_BRAND_OUTPUT}/generic/icon.icns" to
    # "/generic/icon.icns", which fails target_sources() in every port.
    set(KOTONOHA_BRAND_OUTPUT "${CMAKE_BINARY_DIR}/kotonoha-brand" CACHE INTERNAL
        "Generated Kotonoha brand assets shared by all app targets" FORCE)
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

# WebAssembly preloads engine assets from a generated staging directory so
# the built-in splash follows the same brand without modifying tracked files.
if(EMSCRIPTEN)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    # Same scope rule as KOTONOHA_BRAND_OUTPUT: the staging directory is
    # consumed inside kotonoha_configure_app() from sibling game directories.
    # Without CACHE INTERNAL it expands empty there and --preload-file becomes
    # "@/assets", which makes file_packager fail for every WASM target.
    set(KOTONOHA_WEB_ENGINE_ASSETS "${CMAKE_BINARY_DIR}/kotonoha-web-engine-assets"
        CACHE INTERNAL "Staged engine assets for WASM preloads" FORCE)
    file(MAKE_DIRECTORY "${KOTONOHA_WEB_ENGINE_ASSETS}")
    file(COPY "${KOTONOHA_ENGINE_ASSETS}/" DESTINATION "${KOTONOHA_WEB_ENGINE_ASSETS}")
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${KOTONOHA_PROJECT_ROOT}/tools/generate_brand_assets.py"
        --kind web --out "${CMAKE_BINARY_DIR}/kotonoha-web-brand"
        RESULT_VARIABLE _web_brand_status)
    if(NOT _web_brand_status EQUAL 0)
        message(FATAL_ERROR "Cannot generate Kotonoha WebApp branding")
    endif()
    configure_file(
        "${CMAKE_BINARY_DIR}/kotonoha-web-brand/splash/SPLASH.PNG"
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
    if(APP_GAME_ASSET_SOURCE)
        set(_game_asset_source "${APP_GAME_ASSET_SOURCE}")
    else()
        set(_game_asset_source "${KOTONOHA_PROJECT_ROOT}/games/${APP_GAME}/game-assets")
    endif()

    # Embed the canonical icon in every Windows .exe (Explorer, taskbar, ZIP).
    if(WIN32)
        set(_winres "${CMAKE_CURRENT_BINARY_DIR}/${target}-brand.rc")
        if(KOTONOHA_BRAND_OUTPUT AND EXISTS "${KOTONOHA_BRAND_OUTPUT}/generic/icon.ico")
            file(TO_CMAKE_PATH "${KOTONOHA_BRAND_OUTPUT}/generic/icon.ico" _icon_path)
            file(WRITE "${_winres}" "1 ICON \"${_icon_path}\"\n")
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
            MACOSX_BUNDLE_BUNDLE_NAME "${target}"
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

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DAPP_EXECUTABLE=$<TARGET_FILE:${target}>"
                "-DBUNDLE_CONTENTS=$<TARGET_BUNDLE_CONTENT_DIR:${target}>"
                "-DAPP_GAME=${APP_GAME}"
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

            set(APP_TITLE "${target}")
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

