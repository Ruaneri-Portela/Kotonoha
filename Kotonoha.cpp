#include "MainLoop.h"
#include "Game.h"
int main(int argc, char *argv[])
{
    gameWindows *gameMain;
    gameLoop *gameBehaviour;
    gameMain = new gameWindows;
    gameBehaviour = new gameLoop;
    gameMain->laucherGame();
    gameBehaviour->game(gameMain->window, gameMain->renderer);
    gameMain->closeGame();
    return 0;
}
