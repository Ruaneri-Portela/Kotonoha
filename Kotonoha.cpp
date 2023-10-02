#include "Headers.h"
/**
 * @brief The main function of the Kotonoha game.
 * 
 * @param argc The number of command-line arguments.
 * @param argv An array of command-line argument strings.
 * @return int The exit status of the program.
 */
int main(int argc, char **argv)
{
    kotonoha::set game;
    kotonoha::menuReturn opts;
    kotonoha::logger log;
    kotonoha::loop mainBehaviour;
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
            opts.returnCode = mainBehaviour.game(game.window, game.renderer, opts.filenameString, &log);
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
