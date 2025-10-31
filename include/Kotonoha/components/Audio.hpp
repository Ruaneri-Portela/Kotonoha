#pragma once
#include <Kotonoha/components/Sound.hpp>
#include <tuple>
#include <vector>

extern "C"
{
#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>
}

namespace Kotonoha
{
	class Audio
	{
	private:
		std::vector<std::tuple<Sound::Channel *, std::vector<std::tuple<struct Kotonoha_audioDecode *, Sound::Channel::Pipe *>>>> mediaObjects;
		Sound *soundCtx = nullptr;
		struct Kotonoha_time *timeCtx = nullptr;

	public:
		Audio(Sound *soundCtx, struct Kotonoha_time *timeCtx);
		struct Kotonoha_audioDecode *AddMedia(const char *path, Uint64 start, Uint64 end, bool inLoop, const char *channel);
		void RemoveMedia(struct Kotonoha_audioDecode *mediaPtr);
		static int RenderMedia(void *data, Uint8 **target, int *size);
		~Audio();
	};
}