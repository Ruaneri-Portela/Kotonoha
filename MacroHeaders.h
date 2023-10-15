#ifdef _MSC_VER
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>
#include <SDL_image.h>
#define COMPILE "MSVC Compile Build"
#define STRCPYFIX strcpy_s
#elif defined(__GNUG__)
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_image.h>
#define COMPILE "GNU Compile Build"
#define STRCPYFIX strcpy
#else
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_image.h>
#define COMPILE "Unknown"
#define STRCPYFIX strcpy
#endif