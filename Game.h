// This file used to Open and Close game contexts
namespace kotonoha
{
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
	static int start(kotonohaData::envComponets *data) {
		SDL_Init(SDL_INIT_EVERYTHING);
		SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
		data->window = SDL_CreateWindow("Kotonoha Project", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, windowFlags);
		// Set Windows Icon - (Exclame In Builds make on Visual Studio the icons use is as set in resorces, this icons is aplicable on Linux or Mingw/UCRT64_MSVC2 build)
		setIcon(data->window);
		data->renderer = SDL_CreateRenderer(data->window, -1, SDL_RENDERER_ACCELERATED);
		Mix_Init(MIX_INIT_OGG);
		Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
		Mix_AllocateChannels(11);
		TTF_Init();
		SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
		data->context = ImGui::CreateContext();
		ImGui_ImplSDL2_InitForSDLRenderer(data->window, data->renderer);
		ImGui_ImplSDLRenderer2_Init(data->renderer);
		ImGui::StyleColorsDark();
		data->io = &ImGui::GetIO();
		(void)data->io;
		data->io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		data->io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
#ifdef ISANDROID // This is enable using macro when compile to android on NDK
		std::filesystem::path newDir = "/sdcard/Kotonoha";
		std::filesystem::create_directory(newDir);
		std::filesystem::current_path(newDir);
		while (true) {
			if (SDL_AndroidGetExternalStorageState() == 0) {
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Please, enable storage permission", NULL);
				SDL_AndroidRequestPermission("android.permission.READ_EXTERNAL_STORAGE");
				SDL_AndroidRequestPermission("android.permission.WRITE_EXTERNAL_STORAGE");
				SDL_AndroidRequestPermission("android.permission.MANAGE_EXTERNAL_STORAGE");
			}
			else {
				break;
			}
		}
#endif
		return 0;
	};
	static int close(kotonohaData::envComponets *data) {
		ImGui::DestroyContext(data->context);
		ImGui_ImplSDLRenderer2_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		SDL_DestroyWindow(data->window);
		SDL_DestroyRenderer(data->renderer);
		Mix_CloseAudio();
		Mix_Quit();
		TTF_Quit();
		SDL_Quit();
		return 0;
	};
}