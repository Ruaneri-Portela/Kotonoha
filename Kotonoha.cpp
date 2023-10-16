#define _CRT_SECURE_NO_WARNINGS
#include "Headers.h"
static void setIcon(SDL_Window* window)
{
#include "Icon.h"
	// these masks are needed to tell SDL_CreateRGBSurface(From)
	// to assume the data it gets is byte-wise RGB(A) data
	Uint32 rmask, gmask, bmask, amask;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	int shift = (my_icon.bytes_per_pixel == 3) ? 8 : 0;
	rmask = 0xff000000 >> shift;
	gmask = 0x00ff0000 >> shift;
	bmask = 0x0000ff00 >> shift;
	amask = 0x000000ff >> shift;
#else // little endian, like x86
	rmask = 0x000000ff;
	gmask = 0x0000ff00;
	bmask = 0x00ff0000;
	amask = (icon.bytes_per_pixel == 3) ? 0 : 0xff000000;
#endif
	SDL_Surface* iconS = SDL_CreateRGBSurfaceFrom((void*)icon.pixel_data, icon.width, icon.height, icon.bytes_per_pixel * 8, icon.bytes_per_pixel * icon.width, rmask, gmask, bmask, amask);
	SDL_SetWindowIcon(window, iconS);
	SDL_FreeSurface(iconS);
};
// This function below is a entry point
int main(int argc, char* args[])
{

	kotonoha::set game;
	kotonoha::menuReturn opts;
	kotonoha::logger log;
	kotonoha::loop* behaviour;
	log.open("log.lkot");
	// Set Windows Icon - (Exclame In Builds make on Visual Studio the icons use is as set in resorces, this icons is aplicable on Linux or Mingw/UCRT64_MSVC2 build)
	setIcon(game.window);
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
