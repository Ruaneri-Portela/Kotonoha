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
# Android resources are staged only by Gradle, never by native CMake:
# this prevents copying retail GPKs into .cxx/NDK outputs.
function(kotonoha_copy_assets target)
    if(ANDROID)
        return()
    endif()
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
    # design/ is the versioned source of truth, not a generic asset folder.
    # The only file staged into assets/ is the universal engine splash; icon
    # files go exclusively to their platform location (bundle root below,
    # WinRes in KotonohaApp.cmake, Android res/ in build.gradle). Never copy
    # the design/ tree into a bundle and never expose brand-icon.png/icons as
    # engine assets.
    # Replaced only inside the staged bundle/output, never in tracked assets.
    if(NOT ANDROID AND NOT EMSCRIPTEN AND KOTONOHA_BRAND_OUTPUT AND
       NOT ASSETS_GAME STREQUAL "school-days")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${KOTONOHA_BRAND_OUTPUT}/splash/SPLASH.PNG"
                "${destination}/assets/SPLASH.PNG"
            VERBATIM)
    endif()
    if(APPLE AND is_bundle AND KOTONOHA_BRAND_OUTPUT AND
       NOT ASSETS_GAME STREQUAL "school-days")
        # CFBundleIconFile resolves a sibling of Contents/Resources; this is
        # the only place the macOS app icon is installed.
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
    # O hub linka a KotonohaLauncherCore como SHARED; ela nao faz parte da
    # lista generica de targets acima e deve acompanhar apenas o launcher.
    if(target STREQUAL "KotonohaLauncher" AND TARGET KotonohaLauncherCore)
        get_target_property(_core_type KotonohaLauncherCore TYPE)
        if(_core_type STREQUAL "SHARED_LIBRARY")
            list(APPEND _cmake_shared "$<TARGET_FILE:KotonohaLauncherCore>")
        endif()
    endif()
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

# True for every build whose executables are produced by a MinGW toolchain.
# This is the only gate for runtime DLL deployment: no build type, target or
# preset condition is allowed to skip it.
function(kotonoha_is_mingw_toolchain _out)
    set(${_out} FALSE PARENT_SCOPE)
    if(NOT WIN32)
        return()
    endif()
    if(MINGW OR CMAKE_CXX_COMPILER MATCHES "[Mm][Ii][Nn][Gg][Ww]"
              OR CMAKE_C_COMPILER MATCHES "[Mm][Ii][Nn][Gg][Ww]"
              OR VCPKG_TARGET_TRIPLET MATCHES "-mingw")
        set(${_out} TRUE PARENT_SCOPE)
    endif()
endfunction()

# Locate the GCC runtime DLLs shipped with the active MinGW toolchain.
# CMAKE_CXX_COMPILER can be a symlink (Homebrew: /opt/homebrew/bin/<triple>-g++
# -> .../Cellar/mingw-w64/<version>/toolchain-<arch>/bin/<triple>-g++), so
# REALPATH it first and derive both the toolchain prefix and the target triple
# from the resolved compiler. No version, Cellar path or architecture-specific
# filename is hardcoded: libgcc_s_*.dll covers seh/sjlj/dw2 variants.
function(kotonoha_find_mingw_runtime_dlls _out)
    set(${_out} "" PARENT_SCOPE)
    kotonoha_is_mingw_toolchain(_mingw)
    if(NOT _mingw)
        return()
    endif()

    set(_compiler "${CMAKE_CXX_COMPILER}")
    if(NOT _compiler)
        set(_compiler "${CMAKE_C_COMPILER}")
    endif()
    get_filename_component(_compiler_real "${_compiler}" REALPATH)
    if(NOT EXISTS "${_compiler_real}")
        set(_compiler_real "${_compiler}")
    endif()
    get_filename_component(_bin_dir "${_compiler_real}" DIRECTORY)
    get_filename_component(_prefix "${_bin_dir}" DIRECTORY)
    get_filename_component(_compiler_file "${_compiler_real}" NAME)
    string(REGEX REPLACE "-(g\\+\\+|gcc|clang\\+\\+|clang)(\\.exe)?$" ""
        _triple "${_compiler_file}")

    # Homebrew keeps libwinpthread in <prefix>/<triple>/bin and libgcc_s /
    # libstdc++ in <prefix>/<triple>/lib; MSYS2 keeps all of them in <prefix>/bin.
    set(_search_dirs "${_bin_dir}" "${_prefix}/bin" "${_prefix}/lib")
    if(_triple MATCHES "-w64-mingw32$")
        list(APPEND _search_dirs
            "${_prefix}/${_triple}/bin" "${_prefix}/${_triple}/lib")
    else()
        file(GLOB _sysroots LIST_DIRECTORIES TRUE "${_prefix}/*-w64-mingw32")
        foreach(_root IN LISTS _sysroots)
            list(APPEND _search_dirs "${_root}/bin" "${_root}/lib")
        endforeach()
    endif()
    list(REMOVE_DUPLICATES _search_dirs)

    set(_dlls)
    foreach(_dir IN LISTS _search_dirs)
        foreach(_pattern IN ITEMS "libwinpthread-1.dll" "libgcc_s_*.dll"
                                  "libstdc++-6.dll" "libssp-0.dll")
            file(GLOB _matches LIST_DIRECTORIES FALSE "${_dir}/${_pattern}")
            list(APPEND _dlls ${_matches})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _dlls)

    set(_have_pthread FALSE)
    set(_have_gcc FALSE)
    set(_have_stdcxx FALSE)
    foreach(_dll IN LISTS _dlls)
        get_filename_component(_name "${_dll}" NAME)
        if(_name STREQUAL "libwinpthread-1.dll")
            set(_have_pthread TRUE)
        elseif(_name MATCHES "^libgcc_s_")
            set(_have_gcc TRUE)
        elseif(_name STREQUAL "libstdc++-6.dll")
            set(_have_stdcxx TRUE)
        endif()
    endforeach()
    if(NOT _have_pthread OR NOT _have_gcc OR NOT _have_stdcxx)
        message(FATAL_ERROR
            "[Kotonoha] Incomplete MinGW runtime DLLs for ${_compiler} in: "
            "${_search_dirs}")
    endif()

    set(${_out} "${_dlls}" PARENT_SCOPE)
endfunction()

# Always deploy the MinGW runtime DLLs plus every vcpkg bin DLL beside a
# Windows executable. Called for each configured app target; the helper above
# is the single platform predicate.
function(kotonoha_deploy_mingw_runtime target)
    kotonoha_is_mingw_toolchain(_mingw)
    if(NOT _mingw)
        return()
    endif()

    kotonoha_find_mingw_runtime_dlls(_runtime_dlls)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DTARGET_DIR=$<TARGET_FILE_DIR:${target}>"
            "-DMINGW_RUNTIME_DLLS=${_runtime_dlls}"
            "-DVCPKG_BIN_DIR=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
            -P "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/DeployMinGWRuntime.cmake"
        COMMENT "Deploying MinGW and vcpkg runtime DLLs for ${target}"
        VERBATIM)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${KOTONOHA_PROJECT_ROOT}/buildsystems/cmake/packaging/DeployMinGWRuntime.cmake")
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
