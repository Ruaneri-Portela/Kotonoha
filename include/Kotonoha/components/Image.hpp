#pragma once
#include <vector>

extern "C"
{
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/ImageRender.h>
#include <Kotonoha/utils/Time.h>
#include <Kotonoha/utils/UserEvents.h>
}

namespace Kotonoha
{
	class Image
	{
	private:
		Kotonoha_time *timeManager = NULL;
		std::vector<Kotonoha_Picture *> pictures = std::vector<Kotonoha_Picture *>();
		SDL_Mutex *lockImage = NULL;

	public:
		Image(Kotonoha_time *time);

		void Register(const char *path, Uint64 startTime, Uint64 endTime, Uint8 id);

		static Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);

		~Image();
	};
}