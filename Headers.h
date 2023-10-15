#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>
#include <SDL_image.h>
#include <ass/ass.h>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <chrono>
#include <thread>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
namespace kotonoha
{
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