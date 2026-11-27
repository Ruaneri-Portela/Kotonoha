cmake_minimum_required(VERSION 3.16)

project(KotonohaUnifiedLauncher VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT KOTONOHA_BUILD_ALL AND NOT KOTONOHA_BUILD_LAUNCHER)
    message(FATAL_ERROR
        "[KotonohaLauncher] Enable KOTONOHA_BUILD_ALL=ON or KOTONOHA_BUILD_LAUNCHER=ON to build the unified launcher")
endif()

# ---------------------------------------------------------------------------
# JNI bindings. bridge/java/JavaBridge.cpp is compiled into the core on every
# native platform (Android plus macOS/Linux/Windows) so a Java host loads the
# very same library with System.loadLibrary("KotonohaLauncherCore") -- no
# separate shim. Android gets jni.h from the NDK; desktop needs a JDK, found
# via find_package(JNI) (honours JAVA_HOME, including JBR installations).
# The option defaults to ON when headers are available and OFF otherwise so a
# JDK-less configure never breaks; KOTONOHA_ENABLE_JNI=ON forces it.
# ---------------------------------------------------------------------------
if(ANDROID)
    set(_kotonoha_jni_available TRUE)
    set(_kotonoha_jni_include_dirs "")
else()
    find_package(JNI QUIET)
    set(_kotonoha_jni_include_dirs "")
    if(JNI_FOUND AND JAVA_INCLUDE_PATH)
        list(APPEND _kotonoha_jni_include_dirs "${JAVA_INCLUDE_PATH}")
        if(JAVA_INCLUDE_PATH2)
            list(APPEND _kotonoha_jni_include_dirs "${JAVA_INCLUDE_PATH2}")
        endif()
    elseif(DEFINED ENV{JAVA_HOME} AND EXISTS "$ENV{JAVA_HOME}/include/jni.h")
        # FindJNI can miss JBR layouts installed under ~/Library; honour JAVA_HOME.
        list(APPEND _kotonoha_jni_include_dirs "$ENV{JAVA_HOME}/include")
        if(APPLE AND EXISTS "$ENV{JAVA_HOME}/include/darwin/jni.h")
            list(APPEND _kotonoha_jni_include_dirs "$ENV{JAVA_HOME}/include/darwin")
        elseif(WIN32 AND EXISTS "$ENV{JAVA_HOME}/include/win32/jni.h")
            list(APPEND _kotonoha_jni_include_dirs "$ENV{JAVA_HOME}/include/win32")
        endif()
    endif()
    if(_kotonoha_jni_include_dirs)
        set(_kotonoha_jni_available TRUE)
    else()
        set(_kotonoha_jni_available FALSE)
    endif()
endif()
if(_kotonoha_jni_available)
    set(_kotonoha_jni_default ON)
else()
    set(_kotonoha_jni_default OFF)
endif()
option(KOTONOHA_JNI_BINDINGS
    "Compile Java/JNI entry points into KotonohaLauncherCore on every native platform"
    ${_kotonoha_jni_default})
# Friendly alias used by explicit opt-ins (e.g. -DKOTONOHA_ENABLE_JNI=ON).
if(DEFINED KOTONOHA_ENABLE_JNI)
    set(KOTONOHA_JNI_BINDINGS "${KOTONOHA_ENABLE_JNI}" CACHE BOOL
        "Compile Java/JNI entry points into KotonohaLauncherCore on every native platform" FORCE)
endif()
if(KOTONOHA_JNI_BINDINGS AND NOT _kotonoha_jni_available)
    message(FATAL_ERROR
        "[KotonohaLauncher] KOTONOHA_JNI_BINDINGS=ON requires JNI headers; set JAVA_HOME to a JDK or turn the option OFF")
endif()

# ---------------------------------------------------------------------------
# KotonohaLauncherCore: the reusable launcher business logic (engine detection,
# save-profile isolation, parameter persistence, route validation and the
# shared C ABI). Sources live in core/; platform bridges live in bridge/.
# Every frontend -- the desktop/Apple app, Swift, Java/JNI, .NET P/Invoke and
# Emscripten -- consumes this single library.
#
# Layout: core/ = framework, bridge/ = C ABI consumers, app/ = executable.
# ---------------------------------------------------------------------------
set(KOTONOHA_LAUNCHER_CORE_SOURCES
    core/UnifiedLauncher.cpp
    core/GameFactories.cpp
    core/LauncherBridge.cpp
)
if(KOTONOHA_JNI_BINDINGS AND NOT EMSCRIPTEN)
    list(APPEND KOTONOHA_LAUNCHER_CORE_SOURCES bridge/java/JavaBridge.cpp)
endif()
if(EMSCRIPTEN)
    list(APPEND KOTONOHA_LAUNCHER_CORE_SOURCES bridge/js/JsBridge.cpp)
endif()

# The core is a dynamic library on every native platform so the launcher app,
# Java/JNI and .NET P/Invoke all consume the very same binary. Emscripten is
# the sole exception: wasm32 has no dlopen-based shared-library model, so the
# core is linked as an archive into the game page with its JS bridge.
if(EMSCRIPTEN)
    add_library(KotonohaLauncherCore STATIC ${KOTONOHA_LAUNCHER_CORE_SOURCES})
else()
    add_library(KotonohaLauncherCore SHARED ${KOTONOHA_LAUNCHER_CORE_SOURCES})
    target_compile_definitions(KotonohaLauncherCore PRIVATE
        KOTONOHA_LAUNCHER_CORE_BUILD=1)
    set_target_properties(KotonohaLauncherCore PROPERTIES
        OUTPUT_NAME KotonohaLauncherCore
        MACOSX_RPATH ON
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH ON)
    if(WIN32)
        # Sem "lib" no Windows: KotonohaLauncherCore.dll + .lib. A core
        # exporta tambem as classes C++ consumidas pelo KotonohaLauncher.exe.
        set_target_properties(KotonohaLauncherCore PROPERTIES
            PREFIX "" IMPORT_PREFIX "" IMPORT_SUFFIX ".lib"
            WINDOWS_EXPORT_ALL_SYMBOLS ON)
        if(NOT MSVC)
            target_link_options(KotonohaLauncherCore PRIVATE
                "-Wl,--export-all-symbols")
        endif()
    endif()
endif()

# Universal detached host. The core spawns this helper instead of forking the
# UI process; it is built on Windows, Linux and macOS and shipped next to the
# launcher so every frontend (.NET, Java, SwiftUI, CLI) shares one mechanism.
if(NOT EMSCRIPTEN)
    add_executable(KotonohaGameHost host/GameHost.cpp)
    target_link_libraries(KotonohaGameHost PRIVATE KotonohaLauncherCore)
    set_target_properties(KotonohaGameHost PROPERTIES CXX_EXTENSIONS OFF)
    if(APPLE)
        # The helper is bundled in Contents/MacOS of the Launcher app; resolve
        # Python3.framework and the other app dylibs relative to the bundle.
        target_link_options(KotonohaGameHost PRIVATE
            "-Wl,-rpath,@executable_path/../Frameworks")
    endif()
    if(APPLE AND KOTONOHA_BUILD_MACOS_BUNDLES)
        set(_host_bundle_dir
            "${CMAKE_BINARY_DIR}/outputs/KotonohaLauncher.app/Contents/MacOS")
        add_custom_command(TARGET KotonohaGameHost POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_host_bundle_dir}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:KotonohaGameHost>" "${_host_bundle_dir}/KotonohaGameHost"
            VERBATIM)
    endif()
endif()

if(KOTONOHA_JNI_BINDINGS AND NOT EMSCRIPTEN)
    target_include_directories(KotonohaLauncherCore PRIVATE ${_kotonoha_jni_include_dirs})
    # JavaBridge.cpp uses this instead of its old __ANDROID__-only guard.
    target_compile_definitions(KotonohaLauncherCore PRIVATE KOTONOHA_JNI_BINDINGS=1)
    message(STATUS "[KotonohaLauncher] JNI bindings: ON (JavaBridge.cpp in the core)")
else()
    message(STATUS "[KotonohaLauncher] JNI bindings: OFF")
endif()
target_include_directories(KotonohaLauncherCore PUBLIC include)
target_link_libraries(KotonohaLauncherCore PUBLIC KotonohaEngine)

# LauncherBridge.cpp parses the RUNDLL32 launch request with json-c; the
# engine links it privately, so the core needs its own dependency edge.
if(TARGET json-c::json-c)
    target_link_libraries(KotonohaLauncherCore PRIVATE json-c::json-c)
elseif(TARGET PkgConfig::JSON_C)
    target_link_libraries(KotonohaLauncherCore PRIVATE PkgConfig::JSON_C)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(JSON_C QUIET IMPORTED_TARGET json-c)
        if(JSON_C_FOUND)
            target_link_libraries(KotonohaLauncherCore PRIVATE PkgConfig::JSON_C)
        endif()
    endif()
endif()
target_compile_features(KotonohaLauncherCore PUBLIC cxx_std_17)
set_target_properties(KotonohaLauncherCore PROPERTIES CXX_EXTENSIONS OFF)

# Backwards-compatible name for existing consumers and source-contract tools.
add_library(kotonoha_unified_launcher ALIAS KotonohaLauncherCore)

# Link every real game runtime, but never the Example demo.
# The hub is only available when KOTONOHA_BUILD_ALL=ON.
set(_kotonoha_launcher_libraries
    "AMAGAMI|amagami_port"
    "ZERO|zero_no_tsukaima"
    "SNOW|snow_radish_vacation"
    "SUMMER|summer_radish_vacation"
    "MEO|summer_radish_vacation"
    "DDLC|doki_doki_literature_club"
    "KATAWA|doki_doki_literature_club"
    "SCHOOL_DAYS|school_days"
    "REALLIVE|reallive_port"
    "KIRIKIRI|kirikiri_formats"
    "LUCA_SYSTEM|luca_system_formats"
    "PMAIL|PmailPort"
    "N2SYSTEM|n2system_formats"
    "PONSCRIPTER|ponscripter_port"
    "ARTEMIS|artemis_port"
)
foreach(_linked IN LISTS _kotonoha_launcher_libraries)
    string(REPLACE "|" ";" _fields "${_linked}")
    list(GET _fields 0 _game_option)
    list(GET _fields 1 _runtime_library)
    if(NOT KOTONOHA_BUILD_${_game_option} OR NOT TARGET ${_runtime_library})
        # Emscripten has no CPython-backed runtime; the core still compiles
        # and exposes its C ABI with the remaining linked engines.
        if(EMSCRIPTEN AND NOT TARGET ${_runtime_library})
            continue()
        endif()
        message(FATAL_ERROR
            "[KotonohaLauncher] ALL build is missing the ${_game_option} runtime library ${_runtime_library}")
    endif()
    target_link_libraries(KotonohaLauncherCore PUBLIC ${_runtime_library})
    target_compile_definitions(KotonohaLauncherCore
        PUBLIC KOTONOHA_LAUNCH_${_game_option}=1)
    message(STATUS "[KotonohaLauncher] Core linked game runtime: ${_game_option}")
endforeach()
unset(_kotonoha_launcher_libraries)

# The app/hub is intentionally not produced for Emscripten (the core is built
# there so the JS bridge can be embedded into any WASM game page) or Android
# (the Material UI launcher consumes only KotonohaLauncherCore through JNI).
# ---------------------------------------------------------------------------
# KotonohaLauncher: the executable/framework client. Only the SDL entrypoint,
# native hosts and SwiftUI UI belong here -- never launcher logic.
# Bundle resources are kept under app/Resources/.
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN AND NOT ANDROID)
    kotonoha_add_app(KotonohaLauncher app/main.cpp)
    if(APPLE AND (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS"))
        target_sources(KotonohaLauncher PRIVATE app/AppleDocuments.mm)
        target_link_libraries(KotonohaLauncher PRIVATE "-framework Foundation")
    endif()
    target_link_libraries(KotonohaLauncher PRIVATE KotonohaLauncherCore)
    set_target_properties(KotonohaLauncher PROPERTIES CXX_EXTENSIONS OFF)
    # Keep the global engine resources, but do not copy the Example demo data.
    # An empty game-assets directory preserves the shared bundle layout contract.
    set(_kotonoha_launcher_empty_assets "${CMAKE_CURRENT_BINARY_DIR}/empty-game-assets")
    file(MAKE_DIRECTORY "${_kotonoha_launcher_empty_assets}")
    # The unified hub links DDLC as a library. A macOS .app must therefore
    # bundle the Python standard library just as the standalone DDLC app does.
    # Without this, a packaged hub may fail during startup or when DDLC loads.
    if(APPLE AND KOTONOHA_BUILD_MACOS_BUNDLES AND KOTONOHA_BUILD_DDLC
       AND NOT KOTONOHA_USE_PYTHON_STUBS)
        find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
        kotonoha_configure_app(KotonohaLauncher
            BUNDLE_ID me.hirameki.kotonoha
            GAME launcher
            GAME_ASSET_SOURCE "${_kotonoha_launcher_empty_assets}"
            PYTHON_STDLIB "${KOTONOHA_RENPY_PYTHON_STDLIB}"
            PYTHON_VERSION "${KOTONOHA_RENPY_PYTHON_VERSION}")
    else()
        kotonoha_configure_app(KotonohaLauncher
            BUNDLE_ID me.hirameki.kotonoha
            GAME launcher
            GAME_ASSET_SOURCE "${_kotonoha_launcher_empty_assets}")
    endif()

    # The launcher bundle stages KotonohaGameHost; make the ordering explicit.
    if(TARGET KotonohaGameHost)
        add_dependencies(KotonohaLauncher KotonohaGameHost)
    endif()

    # School Days uses a tracked, non-commercial route definition. Ship this
    # together with the in-process hub instead of assuming the individual game's
    # standalone output directory is available at runtime.
    if(KOTONOHA_BUILD_SCHOOL_DAYS)
        kotonoha_preload_asset(KotonohaLauncher
            "${KOTONOHA_PROJECT_ROOT}/games/school-days/game-assets/school-days-hq.ktnroute"
            "/game-assets/school-days-hq.ktnroute")
    endif()

    if(BUILD_TESTING)
        add_executable(KotonohaUnifiedLauncherTests tests/UnifiedLauncherTests.cpp)
        target_link_libraries(KotonohaUnifiedLauncherTests PRIVATE KotonohaLauncherCore)
        target_compile_features(KotonohaUnifiedLauncherTests PRIVATE cxx_std_17)
        set_target_properties(KotonohaUnifiedLauncherTests PROPERTIES CXX_EXTENSIONS OFF)
        add_test(NAME Kotonoha.UnifiedLauncher COMMAND KotonohaUnifiedLauncherTests)
        set_tests_properties(Kotonoha.UnifiedLauncher PROPERTIES
            LABELS "synthetic;fast;host"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    endif()
endif()
