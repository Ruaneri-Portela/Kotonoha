set(root "${TEST_DIRECTORY}/package-assets-test")
file(MAKE_DIRECTORY "${root}/source/assets/fonts" "${root}/source/game/game")
file(WRITE "${root}/source/assets/fonts/test.ttf" "font")
file(WRITE "${root}/source/game/game/test.rpa" "game")
foreach(mode ON OFF)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DRESOURCE_SOURCE=${root}/source/assets"
        "-DGAME_SOURCE=${root}/source/game"
        "-DRESOURCE_DESTINATION=${root}/${mode}"
        "-DINCLUDE_GAME_ASSETS=${mode}"
        -P "${PACKAGE_SCRIPT}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT EXISTS "${root}/${mode}/assets/fonts/test.ttf" OR
       NOT IS_DIRECTORY "${root}/${mode}/game-assets")
        message(FATAL_ERROR "Asset directories were not packaged (${mode})")
    endif()
endforeach()
if(NOT EXISTS "${root}/ON/game-assets/game/test.rpa" OR
   EXISTS "${root}/OFF/game-assets/game/test.rpa" OR
   EXISTS "${root}/ON/games")
    message(FATAL_ERROR "Unexpected packaged game layout")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DRESOURCE_SOURCE=${root}/source/assets"
    "-DGAME_SOURCE=${root}/source/game"
    "-DRESOURCE_DESTINATION=${root}/ON"
    -DINCLUDE_GAME_ASSETS=OFF -P "${PACKAGE_SCRIPT}"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0)
    message(FATAL_ERROR "Previously included assets must not be silently redistributed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DRESOURCE_SOURCE=${root}/source/assets"
    "-DGAME_SOURCE=${root}/source/missing-game"
    "-DRESOURCE_DESTINATION=${root}/no-game"
    -DINCLUDE_GAME_ASSETS=OFF -P "${PACKAGE_SCRIPT}"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(NOT result EQUAL 0 OR NOT IS_DIRECTORY "${root}/no-game/assets" OR
   NOT IS_DIRECTORY "${root}/no-game/game-assets")
    message(FATAL_ERROR "Excluding game assets should not require a local game directory")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DRESOURCE_SOURCE=${root}/source/assets"
    "-DGAME_SOURCE=${root}/source/missing-game"
    "-DRESOURCE_DESTINATION=${root}/no-game"
    -DINCLUDE_GAME_ASSETS=ON -P "${PACKAGE_SCRIPT}"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0)
    message(FATAL_ERROR "Missing game assets must fail when inclusion is requested")
endif()
file(REMOVE_RECURSE "${root}")
