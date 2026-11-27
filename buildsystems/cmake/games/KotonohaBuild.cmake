# Build the selected game ports, then the optional unified desktop launcher.
include_guard(GLOBAL)

# Each port is configured only when explicitly selected (or BUILD_ALL).
set(_KOTONOHA_ENABLED_GAME_DETAILS "")
foreach(game IN LISTS KOTONOHA_GAMES)
    string(REPLACE "|" ";" fields "${game}")
    list(GET fields 0 game_option)
    list(GET fields 1 game_id)
    list(GET fields 2 game_directory)

    if(KOTONOHA_BUILD_${game_option} AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${game_directory}/CMakeLists.txt")
        list(APPEND _KOTONOHA_ENABLED_GAME_DETAILS "${game_id}|${game_directory}")
        add_subdirectory("${game_directory}")
    endif()
endforeach()

# The unified hub exists for full native desktop builds and the Android
# launcher flavor (`KOTONOHA_BUILD_GAME=all`). Individual port builds,
# including the Example demo, do not build a launcher. Emscripten builds the
# reusable KotonohaLauncherCore target (for the JS bridge) but not the hub app.
if((KOTONOHA_BUILD_ALL OR KOTONOHA_BUILD_LAUNCHER) AND
   NOT (APPLE AND CMAKE_SYSTEM_NAME MATCHES "iOS|tvOS|watchOS") AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/launcher/CMakeLists.txt")
    add_subdirectory(launcher)
endif()
