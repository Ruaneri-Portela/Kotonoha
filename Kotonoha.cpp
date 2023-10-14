#include "Headers.h"
// This function below is a entry point
#ifdef _WIN32
int WinMain()
#else
int main()
#endif
{
    kotonoha::set game;
    kotonoha::menuReturn opts;
    kotonoha::logger log;
    kotonoha::loop *behaviour;
    // Export version to log and time to start execution
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
    std::string st(buffer);
    log.appendLog("----------------------------\nStarted application, Kotonoha Project ver."+ kotonoha::version +"\nDate: " + st + "\n----------------------------\n");
    //Open game compnets (SDL)
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
        default:
            opts = kotonoha::menu(game.window, game.renderer, opts.returnCode, &log);
            break;
        };
    };
    game.close();
    return 0;
}
