#pragma once
#include <string>
#include <vector>

extern "C"
{
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/Time.h>
#include <Kotonoha/utils/UserEvents.h>
}

namespace Kotonoha
{
	class Prompt
	{
	private:
		std::vector<std::string> prompt = std::vector<std::string>();
		int *promptResult = nullptr;
		Kotonoha_time *time = nullptr;
		Uint64 startTime = 0;
		Uint64 endTime = 0;
		TTF_Font *font = nullptr;

	public:
		Prompt(std::vector<std::string> prompt, int *promptResult, Uint64 start, Uint64 end, Kotonoha_time *time);
		static enum Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);
		~Prompt();
	};
}