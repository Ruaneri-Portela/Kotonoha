#include "Headers.h"
// This function below is a entry point
int WinMain()
{
    kotonoha::set game;
    kotonoha::menuReturn opts;
    kotonoha::logger log;
    kotonoha::loop *behaviour;
    game.laucher();
    // Select game comportament, if game is end, to menu or play scene
    while (opts.returnCode != 1)
    {
        switch (opts.returnCode)
        {
        case 1:
            break;
        case 2:
            behaviour = new kotonoha::loop();
            opts.returnCode = behaviour->game(game.window, game.renderer, opts.filenameString, opts.configs, &log);
            delete behaviour;
            break;
        case 3:
            opts = kotonoha::menu(game.window, game.renderer, opts.returnCode, &log);
            break;
        default:
            opts = kotonoha::menu(game.window, game.renderer, opts.returnCode, &log);
            break;
        };
    };
    game.close();
    return 0;
}
