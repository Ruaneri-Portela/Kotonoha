# Generic Kotonoha application: the engine with its debug UI and default
# behaviour only (scripts passed on the command line / ImGui). No game profile.
# Produces Kotonoha.app on macOS and Kotonoha(.exe) elsewhere.
if(ANDROID)
    # Loaded by SDLActivity as libGeneric.so (Intent target "Generic").
    add_library(Kotonoha SHARED "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
    set_target_properties(Kotonoha PROPERTIES OUTPUT_NAME Generic)
    target_link_libraries(Kotonoha PRIVATE KotonohaRuntime)
    return()
endif()

add_executable(Kotonoha
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
target_link_libraries(Kotonoha PRIVATE KotonohaRuntime)

kotonoha_configure_app(Kotonoha
    OUTPUT_NAME Kotonoha
    BUNDLE_ID "me.hirameki.kotonoha")
