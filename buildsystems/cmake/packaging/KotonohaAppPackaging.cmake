# Asset installation, runtime deployment and dependency search paths for game outputs.
include_guard(GLOBAL)

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

    # Emscripten mounts the engine assets inside the WASM filesystem via
    # --preload-file ${KOTONOHA_ENGINE_ASSETS}@/assets, so a physical copy in
    # every output directory is redundant (~6.6 MiB x number of game targets).
    set(include_engine ON)
    if(EMSCRIPTEN)
        set(include_engine OFF)
    endif()

    if(ASSETS_GAME)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DRESOURCE_SOURCE=${KOTONOHA_ENGINE_ASSETS}"
                "-DGAME_SOURCE=${_game_asset_source}"
                "-DRESOURCE_DESTINATION=${destination}"
                "-DINCLUDE_GAME_ASSETS=${include_game}"
                "-DINCLUDE_ENGINE_ASSETS=${include_engine}"
                -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/PackageAssets.cmake"
            VERBATIM)
    else()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DASSET_SOURCE=${KOTONOHA_ENGINE_ASSETS}"
                "-DASSET_DESTINATION=${destination}/assets"
                -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/CopyAssetDirectory.cmake"
            VERBATIM)
    endif()
    # Replaced only inside the staged bundle/output, never in tracked assets.
    if(NOT ANDROID AND NOT EMSCRIPTEN AND KOTONOHA_BRAND_OUTPUT)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${KOTONOHA_BRAND_OUTPUT}/splash/SPLASH.PNG"
                "${destination}/assets/SPLASH.PNG"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${KOTONOHA_BRAND_OUTPUT}/generic/icon.png"
                "${destination}/assets/brand-icon.png"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${KOTONOHA_BRAND_OUTPUT}/generic/icon.ico"
                "${destination}/assets/icon.ico"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${KOTONOHA_BRAND_OUTPUT}/generic/icon.icns"
                "${destination}/assets/icon.icns"
            VERBATIM)
    endif()
    if(APPLE AND is_bundle AND KOTONOHA_BRAND_OUTPUT)
        # An .app requires the icon next to Info.plist's Resources directory.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${KOTONOHA_BRAND_OUTPUT}/generic/icon.icns"
                "${destination}/icon.icns"
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
    foreach(_lib IN ITEMS KotonohaEngine ${KOTONOHA_IMGUI_TARGET} SDL3::SDL3 SDL3_ttf::SDL3_ttf)
        if(TARGET ${_lib})
            get_target_property(_alias ${_lib} ALIASED_TARGET)
            if(_alias)
                set(_real "${_alias}")
            else()
                set(_real "${_lib}")
            endif()
            get_target_property(_type ${_real} TYPE)
            get_target_property(_imported ${_real} IMPORTED)
            # Native system libraries remain provided by the OS. Do not
            # duplicate /usr/lib SDL dependencies into every game bundle.
            if(_imported AND
               ((_lib MATCHES "^SDL3" AND KOTONOHA_SDL_PROVIDER_RESOLVED STREQUAL "SYSTEM") OR
                (_lib MATCHES "imgui" AND KOTONOHA_IMGUI_PROVIDER_RESOLVED STREQUAL "SYSTEM")))
                continue()
            endif()
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
            -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/CopyRuntimeLibraries.cmake"
        VERBATIM)
endfunction()

# Helper to get the default dependency directories for tools/apps
function(kotonoha_get_default_dependency_dirs _out_dirs)
    if(ANDROID OR EMSCRIPTEN)
        set(${_out_dirs} "" PARENT_SCOPE)
        return()
    endif()

    set(_dirs)
    list(APPEND _dirs "$<TARGET_FILE_DIR:KotonohaEngine>")
    # Pkg-config backed SDL3 targets are INTERFACE libraries; TARGET_FILE_DIR
    # is invalid for them. System runtime libraries are discovered by ld.so.
    foreach(_kotonoha_sdl_target IN ITEMS SDL3::SDL3 SDL3_ttf::SDL3_ttf)
        if(TARGET ${_kotonoha_sdl_target})
            get_target_property(_kotonoha_alias ${_kotonoha_sdl_target} ALIASED_TARGET)
            if(_kotonoha_alias)
                set(_kotonoha_real_target "${_kotonoha_alias}")
            else()
                set(_kotonoha_real_target "${_kotonoha_sdl_target}")
            endif()
            get_target_property(_kotonoha_type ${_kotonoha_real_target} TYPE)
            if(NOT _kotonoha_type STREQUAL "INTERFACE_LIBRARY")
                list(APPEND _dirs "$<TARGET_FILE_DIR:${_kotonoha_sdl_target}>")
            endif()
        endif()
    endforeach()
    list(APPEND _dirs
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
