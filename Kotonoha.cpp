#include "Headers.h"
int WinMain()
{
    kotonoha::set game;
    kotonoha::menuReturn opts;
    kotonoha::logger log;
    kotonoha::loop *mainBehaviour;
    game.laucher();
    bool exit = false;
    while (!exit)
    {
        switch (opts.returnCode)
        {
        case 1:
            exit = true;
            break;
        case 2:
            mainBehaviour = new kotonoha::loop();
            opts.returnCode = mainBehaviour->game(game.window, game.renderer, opts.filenameString, &log);
            delete mainBehaviour;
            break;
        case 3:
            opts = menu(game.window, game.renderer, opts.returnCode, &log);
            break;
        default:
            opts = menu(game.window, game.renderer, opts.returnCode, &log);
            break;
        };
    };
    game.close();
    return 0;
}
