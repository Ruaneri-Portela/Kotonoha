# Build topology
# --------------
# KotonohaEngine is the reusable engine core. KotonohaRuntime (libKotonoha on
# desktop) packages the scene/gameplay runtime and debug UI. Applications live
# in games/<name>/CMakeLists.txt and are added by the root CMakeLists:
#   games/generic      Kotonoha.app    engine + debug UI, default behaviour
#   games/school-days  SchoolDays.app  School Days HQ profile and SDL callbacks

file(GLOB KOTONOHA_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${KOTONOHA_ENGINE_SOURCE_DIR}/*.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/*.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/audio/*.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/components/*.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/graphics/*.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/mobile/*.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/*.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/routing/*.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/renders/*.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/*.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/*.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/utils/*.c"
)

file(GLOB_RECURSE KOTONOHA_ENGINE_HEADERS CONFIGURE_DEPENDS
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/*.h"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/*.hpp"
)

# Higher-level component/parser/render implementations use the shared engine
# tree but still contain ORS and School Days policies; package them in
# libKotonoha without pulling those policies into the reusable core archive.
set(KOTONOHA_PROFILE_RUNTIME_SOURCES
    "${KOTONOHA_ENGINE_SOURCE_DIR}/components/Audio.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/components/Events.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/components/Prompt.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/components/Panel.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/components/GameplayFlow.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/components/TextBox.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/components/SaveStore.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/components/ChoiceMenu.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/components/Video.cpp"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/parsers/Ors.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/renders/AudioRender.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/renders/TextRender.c"
    "${KOTONOHA_ENGINE_SOURCE_DIR}/renders/VideoRender.c"
)
file(GLOB_RECURSE KOTONOHA_PROFILE_DEBUG_SOURCES CONFIGURE_DEPENDS
    "${KOTONOHA_ENGINE_SOURCE_DIR}/debug/*.cpp"
)
list(APPEND KOTONOHA_PROFILE_RUNTIME_SOURCES ${KOTONOHA_PROFILE_DEBUG_SOURCES})
list(REMOVE_ITEM KOTONOHA_ENGINE_SOURCES ${KOTONOHA_PROFILE_RUNTIME_SOURCES})

# These runtime extension contracts are public to libKotonoha and its profile
# adapter, but intentionally excluded from the game-independent core header gate.
set(KOTONOHA_PROFILE_RUNTIME_HEADERS
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/components/Audio.hpp"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/components/Events.hpp"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/components/Prompt.hpp"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/components/TextBox.hpp"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/components/SaveStore.hpp"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/components/ChoiceMenu.hpp"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/components/Video.hpp"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/parsers/Ors.h"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/renders/AudioRender.h"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/renders/TextRender.h"
    "${KOTONOHA_INCLUDE_DIR}/Kotonoha/renders/VideoRender.h"
)
list(REMOVE_ITEM KOTONOHA_ENGINE_HEADERS ${KOTONOHA_PROFILE_RUNTIME_HEADERS})

file(GLOB_RECURSE KOTONOHA_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${KOTONOHA_RUNTIME_SOURCE_DIR}/*.cpp"
    "${KOTONOHA_RUNTIME_SOURCE_DIR}/*.c"
)
set(KOTONOHA_GAME_PROFILE_RUNTIME_SOURCES
    "${KOTONOHA_RUNTIME_SOURCE_DIR}/Gameplay.cpp"
)
list(REMOVE_ITEM KOTONOHA_RUNTIME_SOURCES ${KOTONOHA_GAME_PROFILE_RUNTIME_SOURCES})
list(APPEND KOTONOHA_PROFILE_RUNTIME_SOURCES ${KOTONOHA_GAME_PROFILE_RUNTIME_SOURCES})
set(KOTONOHA_GAMEPLAY_ADAPTER "${KOTONOHA_RUNTIME_SOURCE_DIR}/Gameplay.cpp")
list(REMOVE_ITEM KOTONOHA_RUNTIME_SOURCES "${KOTONOHA_GAMEPLAY_ADAPTER}")
list(APPEND KOTONOHA_PROFILE_RUNTIME_SOURCES "${KOTONOHA_GAMEPLAY_ADAPTER}")

# Prompt.cpp renders choice text through this pure layout helper, so the runtime
# library owns the one School Days unit it links against; the game targets
# exclude it from their own source list.
set(KOTONOHA_RUNTIME_GAME_POLICY_SOURCES
    "${SCHOOL_DAYS_SOURCE_DIR}/SchoolDaysChoiceTextPlan.cpp"
)
list(APPEND KOTONOHA_PROFILE_RUNTIME_SOURCES
    ${KOTONOHA_RUNTIME_GAME_POLICY_SOURCES})

# Hard build-boundary check: a School Days implementation file must live under
# games/school-days and may never silently re-enter the engine source glob.
foreach(_source IN LISTS KOTONOHA_ENGINE_SOURCES KOTONOHA_RUNTIME_SOURCES)
    get_filename_component(_name "${_source}" NAME)
    if(_name MATCHES "^SchoolDays")
        message(FATAL_ERROR
            "School Days source leaked into KotonohaEngine: ${_source}. "
            "Move it under games/school-days/src instead.")
    endif()
    file(READ "${_source}" _engine_source_text)
    if(_engine_source_text MATCHES "SchoolDays|schoolDays")
        message(FATAL_ERROR
            "School Days implementation dependency leaked into KotonohaEngine: "
            "${_source}. Keep profile policy in libKotonoha.")
    endif()
endforeach()

foreach(_header IN LISTS KOTONOHA_ENGINE_HEADERS)
    get_filename_component(_name "${_header}" NAME)
    if(_name MATCHES "^SchoolDays")
        message(FATAL_ERROR
            "School Days header leaked into the engine include tree: ${_header}. "
            "Move it under games/school-days/include instead.")
    endif()
    file(READ "${_header}" _engine_header_text)
    if(_engine_header_text MATCHES "SchoolDays|schoolDays|School Days")
        message(FATAL_ERROR
            "School Days API dependency leaked into the engine include tree: "
            "${_header}. Keep game/profile APIs under games/school-days/include.")
    endif()
endforeach()

if(APPLE)
    list(APPEND KOTONOHA_ENGINE_SOURCES
        "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/MetalFX.mm"
        "${KOTONOHA_ENGINE_SOURCE_DIR}/mobile/IOS.mm"
    )
endif()

# -----------------------------------------------------------------------------
# Android
# -----------------------------------------------------------------------------
if(ANDROID)
    message(STATUS
        "Configuração Android | ABI: ${ANDROID_ABI} | Platform: ${ANDROID_PLATFORM}")

    unset(harfbuzz_DIR CACHE)
    unset(harfbuzz_ROOT CACHE)
    unset(Freetype_DIR CACHE)
    unset(plutosvg_DIR CACHE)
    unset(plutovg_DIR CACHE)

    find_package(BZip2 REQUIRED)
    find_package(PNG REQUIRED)
    find_package(expat CONFIG REQUIRED)

    add_link_options("-Wl,-z,max-page-size=16384")

    add_library(SDL3_shared_bundle SHARED "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/bundle.cpp")
    target_link_libraries(SDL3_shared_bundle PRIVATE
        "-Wl,--whole-archive" ${KOTONOHA_SDL3_TARGET} "-Wl,--no-whole-archive"
        android log atomic m)

    add_library(SDL3_ttf_shared_bundle SHARED "${KOTONOHA_ENGINE_SOURCE_DIR}/specials/bundle.cpp")
    target_link_libraries(SDL3_ttf_shared_bundle PRIVATE
        "-Wl,--whole-archive" ${KOTONOHA_SDL3_TTF_TARGET} "-Wl,--no-whole-archive"
        SDL3_shared_bundle android log atomic m)

    add_library(KotonohaEngine STATIC ${KOTONOHA_ENGINE_SOURCES})
    add_library(Kotonoha::Engine ALIAS KotonohaEngine)
    set_target_properties(KotonohaEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)

    target_include_directories(KotonohaEngine PUBLIC
        "${KOTONOHA_INCLUDE_DIR}"
        ${LIBASS_INCLUDE_DIRS})

    target_link_directories(KotonohaEngine PUBLIC ${LIBASS_LIBRARY_DIRS})
    target_compile_options(KotonohaEngine PRIVATE ${LIBASS_CFLAGS_OTHER})
    target_link_options(KotonohaEngine PUBLIC ${LIBASS_LDFLAGS_OTHER})
    target_compile_definitions(KotonohaEngine PUBLIC KOTONOHA_HAS_MPV_UPSCALER=1)

    target_link_libraries(KotonohaEngine PUBLIC
        SDL3_shared_bundle
        SDL3_ttf_shared_bundle
        PkgConfig::FFMPEG
        ${LIBASS_LIBRARIES}
        imgui::imgui
        expat::expat
        android
        mediandk
        log
        atomic
        m
        GLESv2)

    # Static on Android: SDLActivity loads a single libKotonoha.so, which is
    # the game library defined in games/school-days/CMakeLists.txt.
    add_library(KotonohaRuntime STATIC
        ${KOTONOHA_PROFILE_RUNTIME_SOURCES}
        ${KOTONOHA_RUNTIME_SOURCES})
    set_target_properties(KotonohaRuntime PROPERTIES
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(KotonohaRuntime PUBLIC
        "${SCHOOL_DAYS_INCLUDE_DIR}"
        "${KOTONOHA_INCLUDE_DIR}"
        "${KOTONOHA_ENGINE_SOURCE_DIR}/routing")
    target_link_libraries(KotonohaRuntime PUBLIC Kotonoha::Engine)

# -----------------------------------------------------------------------------
# Desktop
# -----------------------------------------------------------------------------
else()
    message(STATUS "Configuração Desktop | Sistema: ${CMAKE_SYSTEM_NAME}")

    add_library(KotonohaEngine STATIC ${KOTONOHA_ENGINE_SOURCES})
    add_library(Kotonoha::Engine ALIAS KotonohaEngine)
    set_target_properties(KotonohaEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)

    target_include_directories(KotonohaEngine PUBLIC "${KOTONOHA_INCLUDE_DIR}")
    target_link_libraries(KotonohaEngine PUBLIC
        PkgConfig::FFMPEG
        PkgConfig::LIBASS
        ${KOTONOHA_SDL3_TARGET}
        ${KOTONOHA_SDL3_TTF_TARGET}
        imgui::imgui)

    add_library(KotonohaRuntime SHARED
        ${KOTONOHA_PROFILE_RUNTIME_SOURCES}
        ${KOTONOHA_RUNTIME_SOURCES})
    set_target_properties(KotonohaRuntime PROPERTIES
        OUTPUT_NAME Kotonoha
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        MACOSX_RPATH ON
        INSTALL_RPATH "@loader_path")
    target_include_directories(KotonohaRuntime PUBLIC
        "${SCHOOL_DAYS_INCLUDE_DIR}"
        "${KOTONOHA_INCLUDE_DIR}"
        "${KOTONOHA_ENGINE_SOURCE_DIR}/routing")
    target_link_libraries(KotonohaRuntime PUBLIC Kotonoha::Engine)

    find_package(OpenGL QUIET)
    if(OpenGL_FOUND)
        target_compile_definitions(KotonohaEngine PUBLIC KOTONOHA_HAS_MPV_UPSCALER=1)
        target_link_libraries(KotonohaEngine PUBLIC OpenGL::GL)
    else()
        message(STATUS "OpenGL not found; MPVCompatibleUpscaler upscaling is disabled")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        target_compile_options(KotonohaEngine PRIVATE -fPIC)
    endif()

    if(APPLE)
        target_compile_definitions(KotonohaEngine PUBLIC KOTONOHA_HAS_METALFX=1)
        target_link_libraries(KotonohaEngine PUBLIC
            "-framework Foundation"
            "-framework CoreFoundation"
            "-framework Metal"
            "-framework MetalFX")
    endif()
endif()

if(WIN32)
    set_target_properties(KotonohaRuntime PROPERTIES PREFIX "lib")
endif()
