#include "MainLoop.h"
#include "Game.h"
int WinMain(int argc, char *argv[])
{
    gameWindows *gameMain;
    gameLoop *gameBehaviour;
    gameMain = new gameWindows;
    gameBehaviour = new gameLoop;
    gameMain->laucherGame();
    bool run = true;
    while (run){
        run = gameBehaviour->game(gameMain->window, gameMain->renderer);
    };
    gameMain->closeGame();
    return 0;
}
