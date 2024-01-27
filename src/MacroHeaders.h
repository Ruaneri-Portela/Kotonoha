#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_image.h>
#include "../imgui/imgui.h"
#ifdef _MSC_VER
#define COMPILE "MSVC Compile Build"
#define STRCPYFIX strcpy_s
#elif defined(__GNUG__)
#define COMPILE "GNU Compile Build"
#define STRCPYFIX strcpy
#else
#define COMPILE "Unknown"
#define STRCPYFIX strcpy
#endif
#define KOTONOHA_VERSION "0.0.1"
#define TPS_DELAY 4;