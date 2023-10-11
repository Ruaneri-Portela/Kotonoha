 #include "Headers.h"
int WinMain()
{
    kotonoha::set game;
    kotonoha::menuReturn opts;
    kotonoha::logger log;
    kotonoha::loop *behaviour;
    game.laucher();
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
