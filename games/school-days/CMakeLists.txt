cmake_minimum_required(VERSION 3.18)
project(SchoolDays VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()
if(NOT TARGET KotonohaEngine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

find_package(ZLIB REQUIRED)
if(NOT TARGET kotonoha_provider_gpk)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/gpk"
        "${CMAKE_CURRENT_BINARY_DIR}/providers/gpk")
endif()
if(NOT TARGET kotonoha_rich_presence)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib/src/integrations"
        "${CMAKE_CURRENT_BINARY_DIR}/rich-presence")
endif()
add_library(school_days STATIC
    src/SchoolDaysPresencePreference.cpp
    src/SchoolDaysDiscordPresence.cpp
    src/SchoolDaysPresencePolicy.cpp
    src/SchoolDaysRuntimePresence.cpp
    src/KtrfAssetResolver.cpp
    src/KtrfStateStore.cpp
    src/SchoolDaysHistory.cpp
    src/SchoolDaysPersistenceCodec.cpp
    src/SchoolDaysPersistence.cpp
    src/SchoolDaysBackRules.cpp
    src/SchoolDaysKtrfProfile.cpp
    src/SchoolDaysKtrfSession.cpp
    src/SchoolDaysKtrfGameplayBridge.cpp
    src/SchoolDaysGpkArchive.cpp
    src/SchoolDaysGpkIndex.cpp
    src/SchoolDaysAssetMount.cpp
    src/SchoolDaysEmbeddedAssets.cpp
    src/SchoolDaysCmap.cpp
    src/SchoolDaysTitleModel.cpp
    src/SchoolDaysTitleRenderPlan.cpp
    src/SchoolDaysTitleProfile.cpp
    src/SchoolDaysEndBackgroundResolver.cpp
    src/SchoolDaysRouteMapModel.cpp
    src/SchoolDaysRouteMapOracle.cpp
    src/SchoolDaysConfigMenuModel.cpp
    src/SchoolDaysConfigMenuRenderPlan.cpp
    src/SchoolDaysRetailConfigPolicy.cpp
    src/SchoolDaysDisplayPolicy.cpp
    src/SchoolDaysConfigStorage.cpp
    src/SchoolDaysMenuBarPresenter.cpp
    src/SchoolDaysMenuBarLayout.cpp
    src/SchoolDaysTopMeterModel.cpp
    src/SchoolDaysTopMeterRenderPlan.cpp
    src/SchoolDaysChoiceTextPlan.cpp
    src/SchoolDaysSaveLoadModel.cpp
    src/SchoolDaysSaveLoadRenderPlan.cpp
    src/SchoolDaysSaveFeelModel.cpp
    src/SchoolDaysSaveFeelRenderPlan.cpp
    src/SchoolDaysExitViewModel.cpp
    src/SchoolDaysExitViewRenderPlan.cpp
    src/SchoolDaysReplayModel.cpp
    src/SchoolDaysReplayRenderPlan.cpp
    src/SchoolDaysReplayScriptOracle.cpp
    src/SchoolDaysReplayPlaybackSession.cpp
    src/SchoolDaysPlayDataSession.cpp
    src/SchoolDaysRouteMapRenderPlan.cpp
    src/SchoolDaysFontData.cpp
    src/SchoolDaysSaveProfile.cpp
    src/SchoolDaysAtomicFile.cpp
    src/SchoolDaysSaveStorage.cpp
    src/SchoolDaysSaveProvenance.cpp
    src/SchoolDaysSaveMetadata.cpp
    src/SchoolDaysSaveMetadataState.cpp
    src/SchoolDaysSaveService.cpp
    src/SchoolDaysSaveServiceRead.cpp
    src/SchoolDaysBackLog.cpp
    src/SchoolDaysBackLogPresenter.cpp
    src/SchoolDaysSubtitleTypography.cpp
    src/SchoolDaysSystemUi.cpp
    src/SchoolDaysAssets.cpp
    src/SchoolDaysDisplayNarrativeMarkup.cpp
    src/SchoolDaysLxhPreference.cpp
    src/SchoolDaysTranslationCoverage.cpp
    src/SchoolDaysRuntime.cpp
    src/SchoolDaysScenes.cpp
    src/SchoolDaysLauncherLocale.cpp
    src/SchoolDaysLauncher.cpp
)
target_include_directories(school_days PUBLIC include src)
if(EMSCRIPTEN OR IOS OR CMAKE_SYSTEM_NAME MATCHES "iOS|tvOS|watchOS")
    target_compile_definitions(school_days PRIVATE KOTONOHA_HAS_DISCORD_RPC=0)
else()
    target_compile_definitions(school_days PRIVATE KOTONOHA_HAS_DISCORD_RPC=1)
endif()
target_link_libraries(school_days PUBLIC KotonohaEngine kotonoha_provider_gpk
    PRIVATE ZLIB::ZLIB kotonoha_rich_presence)
target_compile_features(school_days PUBLIC cxx_std_17)
if(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    target_compile_definitions(school_days PUBLIC KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY=1)
endif()
# Provider exposes only shared interface headers. The game-specific archive,
# translation and LxH implementations above have one owner in this target.
set_target_properties(school_days PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(SchoolDays src/main.cpp)
target_link_libraries(SchoolDays PRIVATE school_days)
if(ANDROID)
    target_sources(SchoolDays PRIVATE src/SchoolDaysEmbeddedAssetsAndroid.cpp)
endif()
set(KOTONOHA_SCHOOL_DAYS_RUNTIME_ASSETS_SOURCE "" CACHE PATH
    "Exact approved private School Days runtime assets directory")
option(KOTONOHA_SCHOOL_DAYS_REQUIRE_EMBEDDED_ASSETS
    "Require the approved native runtime assets pack" OFF)
if(KOTONOHA_SCHOOL_DAYS_RUNTIME_ASSETS_SOURCE)
    set_property(TARGET SchoolDays PROPERTY KOTONOHA_EMBED_ENGINE_ASSETS TRUE)
endif()
kotonoha_configure_app(SchoolDays
    BUNDLE_ID me.hirameki.kotonoha.schooldays GAME school-days)

# The engine's DEV_CONTROL definition is PRIVATE, so propagate it explicitly
# to the School Days entry point as well. Otherwise its dev-startup.log trace
# is silently compiled out even when the developer bridge is enabled.
if(WIN32 AND NOT ANDROID AND KOTONOHA_DEV_CONTROL AND
   NOT KOTONOHA_DISTRIBUTION_BUILD)
    target_compile_definitions(SchoolDays PRIVATE KOTONOHA_DEV_CONTROL=1)
endif()

# A player-facing Windows build is a GUI application: the SDL callback entry
# point opens our launcher directly. Never attach a second console window just
# to display SDL logging. Keep the development executable's console for QA.
if(WIN32 AND NOT ANDROID AND KOTONOHA_DISTRIBUTION_BUILD)
    set_target_properties(SchoolDays PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# The Windows executable uses the black-and-white icon supplied specifically
# for School Days HQ. Only register the resource once the approved artwork has
# been converted to an .ico; a missing icon must not break existing L4 builds.
if(WIN32 AND NOT ANDROID)
    set(SCHOOL_DAYS_WINDOWS_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/SchoolDays.ico")
    if(NOT EXISTS "${SCHOOL_DAYS_WINDOWS_ICON}")
        get_filename_component(_school_days_repo_root
            "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
        if(EXISTS "${_school_days_repo_root}/assets/SchoolDays.ico")
            set(SCHOOL_DAYS_WINDOWS_ICON
                "${_school_days_repo_root}/assets/SchoolDays.ico")
        endif()
    endif()
    if(EXISTS "${SCHOOL_DAYS_WINDOWS_ICON}")
        get_filename_component(_school_days_icon_dir
            "${SCHOOL_DAYS_WINDOWS_ICON}" DIRECTORY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${SCHOOL_DAYS_WINDOWS_ICON}")
        enable_language(RC)
        set(SCHOOL_DAYS_WINDOWS_RC
            "${CMAKE_CURRENT_SOURCE_DIR}/windows/SchoolDays.rc")
        set_source_files_properties("${SCHOOL_DAYS_WINDOWS_RC}" PROPERTIES
            INCLUDE_DIRECTORIES "${_school_days_icon_dir}")
        target_sources(SchoolDays PRIVATE "${SCHOOL_DAYS_WINDOWS_RC}")
    else()
        message(STATUS "School Days Windows icon pending: ${SCHOOL_DAYS_WINDOWS_ICON}")
    endif()
endif()

# This font is a separately supplied local QA asset, not tracked game content.
if(KOTONOHA_SCHOOL_DAYS_RUNTIME_ASSETS_SOURCE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_runtime_assets_source "${KOTONOHA_SCHOOL_DAYS_RUNTIME_ASSETS_SOURCE}")
    file(GLOB_RECURSE _runtime_assets_inputs CONFIGURE_DEPENDS
        LIST_DIRECTORIES FALSE "${_runtime_assets_source}/*")
    set(_runtime_assets_script "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_runtime_assets.py")
    set(_runtime_assets_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/runtime-assets")
    set(_runtime_assets_header "${_runtime_assets_dir}/SchoolDaysEmbeddedAssets.generated.hpp")
    add_custom_command(OUTPUT "${_runtime_assets_header}"
        COMMAND "${Python3_EXECUTABLE}" "${_runtime_assets_script}"
            "${_runtime_assets_source}" "${_runtime_assets_header}"
        DEPENDS "${_runtime_assets_script}" ${_runtime_assets_inputs}
        COMMENT "Embedding the approved private School Days runtime assets"
        VERBATIM)
    target_sources(school_days PRIVATE "${_runtime_assets_header}")
    target_include_directories(school_days PRIVATE "${_runtime_assets_dir}")
    target_compile_definitions(school_days PUBLIC KOTONOHA_SCHOOL_DAYS_EMBEDDED_ASSETS=1)
elseif(KOTONOHA_SCHOOL_DAYS_REQUIRE_EMBEDDED_ASSETS)
    message(FATAL_ERROR "Approved School Days runtime assets source is required")
endif()

# Stage it AFTER kotonoha_configure_app's generic asset POST_BUILD copy so a
# placeholder in the engine asset directory cannot overwrite the real font.
# A separate verify target is mandatory before assembling a private QA ZIP.
if(WIN32 AND NOT ANDROID AND NOT KOTONOHA_SCHOOL_DAYS_RUNTIME_ASSETS_SOURCE)
    set(KOTONOHA_SCHOOL_DAYS_LAUNCHER_FONT_SOURCE "" CACHE FILEPATH
        "Path to approved local NotoSansJP-Regular.ttf for School Days Windows QA")
    set(_school_days_font_script
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/StageLauncherFont.cmake")
    set(_school_days_font_output
        "$<TARGET_FILE_DIR:SchoolDays>/assets/fonts/NotoSansJP-Regular.ttf")
    if(KOTONOHA_SCHOOL_DAYS_LAUNCHER_FONT_SOURCE)
        add_custom_command(TARGET SchoolDays POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DKOTONOHA_FONT_ACTION=stage"
                "-DKOTONOHA_FONT_SOURCE=${KOTONOHA_SCHOOL_DAYS_LAUNCHER_FONT_SOURCE}"
                "-DKOTONOHA_FONT_DESTINATION=${_school_days_font_output}"
                -P "${_school_days_font_script}"
            COMMENT "Stage and verify the approved School Days Windows launcher font"
            VERBATIM)
    endif()
    add_custom_target(SchoolDaysVerifyLauncherFont
        COMMAND "${CMAKE_COMMAND}"
            "-DKOTONOHA_FONT_ACTION=verify"
            "-DKOTONOHA_FONT_DESTINATION=${_school_days_font_output}"
            -P "${_school_days_font_script}"
        DEPENDS SchoolDays
        COMMENT "Fail closed if the School Days Windows QA launcher font is missing or a stub"
        VERBATIM)
endif()

# Use only the player's authorized School Days HQ key visual. Always copy it
# from the source tree to the binary folder, including asset-only rebuilds.
# The single SchoolDays.exe loads this PNG; no separate launcher EXE is needed.
if(WIN32 AND NOT ANDROID AND NOT KOTONOHA_SCHOOL_DAYS_RUNTIME_ASSETS_SOURCE)
    set(KOTONOHA_SCHOOL_DAYS_LAUNCHER_ART_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/game-assets/launcher-art/SchoolDaysHQ-Launcher.png"
        CACHE FILEPATH "Private School Days HQ launcher artwork source")
    set(SCHOOL_DAYS_LAUNCHER_HERO "${KOTONOHA_SCHOOL_DAYS_LAUNCHER_ART_SOURCE}")
    if(NOT EXISTS "${SCHOOL_DAYS_LAUNCHER_HERO}")
        message(WARNING "School Days HQ launcher artwork missing: ${SCHOOL_DAYS_LAUNCHER_HERO}")
    endif()
    # The executable is emitted under outputs/<config>/SchoolDays, not the
    # CMake subdirectory. Read the configured target property rather than using
    # $<TARGET_FILE_DIR:SchoolDays>, which would create a CMake dependency cycle
    # because SchoolDays itself depends on this staging target.
    get_target_property(_school_days_runtime_output SchoolDays RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _school_days_runtime_output)
        message(FATAL_ERROR "School Days runtime output directory is undefined")
    endif()
    set(_school_days_hero_out "${_school_days_runtime_output}/assets")
    # Keep this target even when optional art is currently missing, so a new
    # file can be supplied and staged by the next build without reconfiguring.
    add_custom_target(SchoolDaysLauncherArtwork
        COMMAND "${CMAKE_COMMAND}"
            "-DKOTONOHA_ASSET_KIND=launcher"
            "-DKOTONOHA_ASSET_SOURCE=${SCHOOL_DAYS_LAUNCHER_HERO}"
            "-DKOTONOHA_ASSET_DESTINATION=${_school_days_hero_out}/SchoolDaysHQ-Launcher.png"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/windows/StageOptionalAssets.cmake"
        COMMENT "Sync School Days HQ launcher image (no retail GPK changes)"
        VERBATIM)
    add_dependencies(SchoolDays SchoolDaysLauncherArtwork)
endif()

# Ship the tracked route through the engine's platform-specific asset hook.
kotonoha_preload_asset(SchoolDays
    "${CMAKE_CURRENT_SOURCE_DIR}/game-assets/school-days-hq.ktnroute"
    "/game-assets/school-days-hq.ktnroute")

# Embed approved custom Display UI, identically in Windows and Android
# DEV/DIST. Retail-derived General/Sound edits require an explicit private QA
# opt-in. Source bytes and the generated header stay outside Git.
set(_school_days_overlay_source "${CMAKE_CURRENT_SOURCE_DIR}/game-assets/overlay")
set(_school_days_overlay_legacy "${CMAKE_CURRENT_SOURCE_DIR}/game-assets/display-options")
set(KOTONOHA_SCHOOL_DAYS_DISPLAY_OVERLAY_SOURCE "" CACHE PATH
    "Private source of approved School Days Display artwork")
option(KOTONOHA_SCHOOL_DAYS_REQUIRE_EMBEDDED_OVERLAY
    "Fail configuration without the approved embedded Display UI" OFF)
option(KOTONOHA_SCHOOL_DAYS_EMBED_PRIVATE_OPTION_ART
    "Embed exact user-edited retail General/Sound art for private QA only" OFF)
set(_school_days_embed_source "${KOTONOHA_SCHOOL_DAYS_DISPLAY_OVERLAY_SOURCE}")
if(NOT _school_days_embed_source AND DEFINED ENV{KOTONOHA_SCHOOL_DAYS_DISPLAY_OVERLAY_SOURCE})
    set(_school_days_embed_source "$ENV{KOTONOHA_SCHOOL_DAYS_DISPLAY_OVERLAY_SOURCE}")
endif()
set(_school_days_embed_files
    Option_Display.png Option_Display_Chip.png Option_Display.cmap
    Option_Display_Wide.cmap Option_Display_Wide_Full.cmap Option_Display_Wide_Note.cmap)
set(_school_days_embed_flags "")
set(_school_days_embed_mode display)
if(KOTONOHA_SCHOOL_DAYS_EMBED_PRIVATE_OPTION_ART)
    list(APPEND _school_days_embed_files
        Option_Def.png Option_Def_Chip.png Option_Sound.png Option_Sound_Chip.png)
    list(APPEND _school_days_embed_flags --private-option-art)
    set(_school_days_embed_mode private)
    message(WARNING "Private School Days General/Sound artwork enabled; do not redistribute these binaries without permission")
endif()
if(NOT _school_days_embed_source OR
   _school_days_embed_source STREQUAL _school_days_overlay_source OR
   _school_days_embed_source STREQUAL _school_days_overlay_legacy)
    set(_school_days_embed_source "")
    foreach(_candidate IN ITEMS "${_school_days_overlay_source}" "${_school_days_overlay_legacy}")
        set(_complete TRUE)
        foreach(_name IN LISTS _school_days_embed_files)
            if(NOT EXISTS "${_candidate}/System/Option/${_name}")
                set(_complete FALSE)
            endif()
        endforeach()
        if(_complete)
            set(_school_days_embed_source "${_candidate}")
            break()
        endif()
    endforeach()
endif()
if(_school_days_embed_source)
    set(_school_days_embed_dependencies "")
    foreach(_name IN LISTS _school_days_embed_files)
        set(_file "${_school_days_embed_source}/System/Option/${_name}")
        if(NOT EXISTS "${_file}")
            message(FATAL_ERROR "Missing approved Display UI resource: ${_file}")
        endif()
        list(APPEND _school_days_embed_dependencies "${_file}")
    endforeach()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_school_days_embed_script "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_display_overlay.py")
    set(_school_days_embed_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${_school_days_embed_mode}")
    set(_school_days_embed_header "${_school_days_embed_dir}/SchoolDaysEmbeddedDisplay.generated.hpp")
    add_custom_command(OUTPUT "${_school_days_embed_header}"
        COMMAND "${Python3_EXECUTABLE}" "${_school_days_embed_script}"
            "${_school_days_embed_source}" "${_school_days_embed_header}" ${_school_days_embed_flags}
        DEPENDS "${_school_days_embed_script}" ${_school_days_embed_dependencies}
        COMMENT "Embedding approved School Days Display UI resources"
        VERBATIM)
    target_sources(school_days PRIVATE "${_school_days_embed_header}")
    target_include_directories(school_days PRIVATE "${_school_days_embed_dir}")
    target_compile_definitions(school_days PUBLIC KOTONOHA_SCHOOL_DAYS_EMBEDDED_DISPLAY=1)
    if(KOTONOHA_SCHOOL_DAYS_EMBED_PRIVATE_OPTION_ART)
        target_compile_definitions(school_days PUBLIC KOTONOHA_SCHOOL_DAYS_EMBED_PRIVATE_OPTION_ART=1)
    endif()
elseif(KOTONOHA_SCHOOL_DAYS_REQUIRE_EMBEDDED_OVERLAY OR
       KOTONOHA_SCHOOL_DAYS_EMBED_PRIVATE_OPTION_ART)
    message(FATAL_ERROR "Approved private School Days Display UI source is required")
else()
    message(STATUS "School Days embedded Display UI omitted (private artwork not supplied)")
endif()

if(BUILD_TESTING AND NOT ANDROID)
    add_subdirectory(tests)
endif()
