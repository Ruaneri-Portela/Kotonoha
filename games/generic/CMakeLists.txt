cmake_minimum_required(VERSION 3.18)
project(KotonohaGenericApp LANGUAGES C CXX)

if(NOT TARGET KotonohaRuntime)
    set(_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    set(KOTONOHA_INCLUDE_DIR "${_ROOT_DIR}/include")
    set(KOTONOHA_ENGINE_SOURCE_DIR "${_ROOT_DIR}/src/Kotonoha")
    add_subdirectory("${_ROOT_DIR}" kotonoha_build EXCLUDE_FROM_ALL)
endif()

# Generic Kotonoha application: the engine with its debug UI and default
# behaviour only (scripts passed on the command line / ImGui). No game profile.
# Produces Kotonoha.app on macOS and Kotonoha(.exe) elsewhere.
if(ANDROID)
    add_library(Kotonoha SHARED "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
    set_target_properties(Kotonoha PROPERTIES OUTPUT_NAME Kotonoha)
    target_link_libraries(Kotonoha PRIVATE KotonohaRuntime)
    return()
endif()

add_executable(KotonohaGeneric
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
target_link_libraries(KotonohaGeneric PRIVATE KotonohaRuntime)

kotonoha_configure_app(KotonohaGeneric
    INCLUDE_ASSETS_SCRIPTS
    OUTPUT_NAME Kotonoha
    BUNDLE_ID "me.hirameki.kotonoha"
    GAME_ASSET "generic"
    )