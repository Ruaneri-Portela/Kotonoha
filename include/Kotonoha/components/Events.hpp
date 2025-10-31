#pragma once
#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/Video.hpp>

extern "C"
{
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/renders/TextRender.h>
}

namespace Kotonoha
{
	class Event
	{
	private:
		Kotonoha_orsData eventsFromScript;
		Uint64 lastTime = 0;
		bool inExit = false;
		bool closed = false;
		static int EventManager(void *data);

	public:
		Event(const char *orsPath, void *gameplay, struct Kotonoha_Game *gameCtx);
		bool CheckEnd(void *gameplay);
		~Event();
	};
}
