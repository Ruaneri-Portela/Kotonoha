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
	class set
	{
	public:
		SDL_Renderer* renderer = NULL;
		SDL_Window* window = NULL;
		ImGuiIO* io = NULL;
		int laucher()
		{
			SDL_Init(SDL_INIT_EVERYTHING);
			SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
			window = SDL_CreateWindow("Kotonoha Project", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, windowFlags);
			// Set Windows Icon - (Exclame In Builds make on Visual Studio the icons use is as set in resorces, this icons is aplicable on Linux or Mingw/UCRT64_MSVC2 build)
			setIcon(window);
			renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
			Mix_Init(MIX_INIT_OGG);
			Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
			TTF_Init();
			SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
			ImGui::SetCurrentContext(NULL);
			ImGui::CreateContext();
			ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
			ImGui_ImplSDLRenderer2_Init(renderer);
			ImGui::StyleColorsDark();
			io = &ImGui::GetIO();
			(void)io;
			io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
			return 0;
		}
		int close()
		{
			ImGui_ImplSDLRenderer2_Shutdown();
			ImGui_ImplSDL2_Shutdown();
			ImGui::DestroyContext();
			SDL_DestroyWindow(window);
			SDL_DestroyRenderer(renderer);
			Mix_CloseAudio();
			Mix_Quit();
			TTF_Quit();
			SDL_Quit();
			return 0;
		}
	};
	void helpMsg() {
		std::cout << "Kotonoha Project ver." << kotonoha::version << std::endl;
		std::cout << "Options:" << std::endl;
		std::cout << "  -v\tShow version" << std::endl;
		std::cout << "  -h\tShow this help" << std::endl;
		std::cout << "  -f\t[file]\tOpen script in direct mode, need config is seted" << std::endl;
		std::cout << "  -t\t[value]\tSet minimal delay per tick on threads" << std::endl;
	};
}