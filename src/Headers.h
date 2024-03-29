#include "MacroHeaders.h"
#include <ass/ass.h>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <chrono>
#include <thread>
#include <filesystem>
#include "../imgui/backends/imgui_impl_sdl2.h"
#include "../imgui/backends/imgui_impl_sdlrenderer2.h"
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include "Utils.h"
#include "Data.h"
#include "Game.h"
#include "Start.h"
#include "Menu.h"
#include "Prompt.h"
#include "Text.h"
#include "Video.h"
#include "Audio.h"
#include "Image.h"
#include "Ors.h"
#include "Ui.h"
#include "MainLoop.h"