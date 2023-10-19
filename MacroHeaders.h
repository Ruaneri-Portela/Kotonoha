#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_image.h>
#ifdef _MSC_VER
#define COMPILE "MSVC Compile Build"
#include "imgui.h"
#define STRCPYFIX strcpy_s
#elif defined(__GNUG__)
#define COMPILE "GNU Compile Build"
#include "Dep/imgui/imgui.h"
#define STRCPYFIX strcpy
#else
#define COMPILE "Unknown"
#define STRCPYFIX strcpy
#endif