#define _CRT_SECURE_NO_WARNINGS
#include "Headers.h"

// This function below is a entry point
int main(int argc, char* args[])
{

	kotonoha::set game;
	kotonoha::menuReturn opts;
	kotonoha::logger log;
	kotonoha::loop* behaviour;
	log.open("log.lkot");
	// Get Sys Info
	SDL_version compiled;
	SDL_version linked;
	SDL_VERSION(&compiled);
	SDL_GetVersion(&linked);
	std::ostringstream program;
	// Format Version
	std::ostringstream compiledVersionStream;
	compiledVersionStream << std::to_string(compiled.major) << "." << std::to_string(compiled.minor) << "." << std::to_string(compiled.patch);
	std::string compiledVersion = compiledVersionStream.str();
	std::ostringstream linkedVersionStream;
	linkedVersionStream << std::to_string(linked.major) << "." << std::to_string(linked.minor) << "." << std::to_string(linked.patch);
	std::string linkedVersion = linkedVersionStream.str();
	program << std::endl << SDL_GetSystemRAM() << " RAM / " << SDL_GetCPUCount() << " CPU Cores / " << SDL_GetVideoDriver(0) << " / cSDLv:" << compiledVersion;
	program << " lSDLv:" << linkedVersion << " / " << "Avcodc: " << avcodec_version() << " Avformat:" << avformat_version() << " Avutil:" << avutil_version();
	program << " Ass: " << ass_library_version() << " ImGui:" << IMGUI_VERSION << std::endl << COMPILE << std::endl;
	// Export version to log and time to start execution
	auto now = std::chrono::system_clock::now();
	std::time_t time = std::chrono::system_clock::to_time_t(now);
	char buffer[80];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
	std::string st(buffer);
	log.appendLog("----------------------------\nStarted application, Kotonoha Project ver." + kotonoha::version + program.str() + "\nDate: " + st + "\n----------------------------\n");
	// Open game compnets (SDL)
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
			opts.returnCode = behaviour->game(game.window, game.renderer, opts.filenameString, opts.configs, &log, game.io);
			delete behaviour;
			break;
		default:
			opts = kotonoha::menu(game.window, game.renderer, opts.returnCode, &log, game.io);
			break;
		};
	};
	game.close();
	log.appendLog("----------------------------\nGame is end... Goodbye!");
	log.close();
	return 0;
}
