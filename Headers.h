#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL.h>
#include <ass/ass.h>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <chrono>
#include <thread>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_sdlrenderer2.h"
namespace kotonoha{
    const std::string version = "0.0.1 Alpha";
}
#include "Utils.h"
#include "Data.h"
#include "Prompt.h"
#include "Text.h"
#include "Video.h"
#include "Audio.h"
#include "Image.h"
#include "Ors.h"
#include "Ui.h"
#include "MainLoop.h"
#include "Menu.h"
#include "Game.h"