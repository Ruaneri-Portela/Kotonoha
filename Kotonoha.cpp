#include "MainLoop.h"
#include "Game.h"
int main(int argc, char **argv)
{
    std::cout << "Have " << argc << " arguments:" << std::endl;
    std::string filepath = "";
    gameWindows *gameMain;
    gameLoop *gameBehaviour;
    gameMain = new gameWindows;
    gameBehaviour = new gameLoop;
    gameMain->laucherGame();
    bool run = true;
    while (run)
    {
        //run = gameBehaviour->game(gameMain->window, gameMain->renderer,"Scripts/00/00-00-A00.ENG.ORS");
        run = gameBehaviour->game(gameMain->window, gameMain->renderer,"Scripts/00/00-00-A01.ENG.ORS");
        //run = gameBehaviour->game(gameMain->window, gameMain->renderer,"Scripts/00/00-00-A02.ENG.ORS");
        //run = gameBehaviour->game(gameMain->window, gameMain->renderer,"Scripts/00/00-00-A03.ENG.ORS");
    };
    gameMain->closeGame();
    return 0;
}
