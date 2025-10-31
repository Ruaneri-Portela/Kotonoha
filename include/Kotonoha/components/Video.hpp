#pragma once
#include <vector>

extern "C"
{
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/Time.h>
#include <Kotonoha/utils/UserEvents.h>
#include <Kotonoha/renders/VideoRender.h>
}

namespace Kotonoha
{
	class Video
	{
	private:
		Kotonoha_time *timeManager = NULL;
		std::vector<Kotonoha_videoData *> videos = std::vector<Kotonoha_videoData *>();
		SDL_Mutex *videoLock = NULL;

	public:
		Video(Kotonoha_time *timeManager);

		bool Register(const char *path, Uint64 startTime, Uint64 endTime);

		static Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);

		~Video();
	};
}
