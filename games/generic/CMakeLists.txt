# Generic Kotonoha application: the engine with its debug UI and default
# behaviour only (scripts passed on the command line / ImGui). No game profile.
# Produces Kotonoha.app on macOS and Kotonoha(.exe) elsewhere.
if(ANDROID)
    # Não compila nada no Android
    return()
endif()

add_executable(KotonohaGeneric
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
target_link_libraries(KotonohaGeneric PRIVATE KotonohaRuntime)

kotonoha_configure_app(KotonohaGeneric
    INCLUDE_ASSETS_SCRIPTS
    OUTPUT_NAME Kotonoha
    BUNDLE_ID "me.hirameki.kotonoha")