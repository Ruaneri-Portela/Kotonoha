#include <Kotonoha/Kotonoha.hpp>
extern "C"
{
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
	/* Função chamada uma vez na inicialização */
	SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
	{
		SDL_AppResult status;
#if defined(KOTONOHA_MOBILE)
		// Configurações específicas do Mobile
		Kotonoha_MobileSetup();
#endif
		// Inicializa o Kotonoha com os argumentos da linha de comando
		*appstate = new Kotonoha::Kotonoha(argc, argv, &status);
		return status;
	}

	/* Função chamada para tratar eventos (input do mouse, teclas, etc) */
	SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
	{
		return static_cast<Kotonoha::Kotonoha*>(appstate)->Event(event);
	}

	/* Função chamada uma vez por frame, coração do programa */
	SDL_AppResult SDL_AppIterate(void* appstate)
	{
		return static_cast<Kotonoha::Kotonoha*>(appstate)->Main();
	}

	/* Função chamada na finalização do programa */
	void SDL_AppQuit(void* appstate, SDL_AppResult result)
	{
		(void)result;
		delete static_cast<Kotonoha::Kotonoha*>(appstate);
	}
}