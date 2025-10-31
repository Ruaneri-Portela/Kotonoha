#pragma once
#include <ass/ass.h>
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/Time.h>

struct Kotonoha_textData
{
	ASS_Library* ass_library;
	ASS_Renderer* ass_renderer;
	ASS_Track* track;

	SDL_Texture* subTexture;
	void* lastFrame;
	struct Kotonoha_time* time;
};

struct Kotonoha_textData* Kotonoha_TextRenderInit(struct Kotonoha_time* time, struct Kotonoha_Game* gameCtx);

void Kotonoha_TextRenderShutdown(struct Kotonoha_textData** object);

enum Kotonoha_Scene_Status Kotonoha_TextRenderDraw(KOTONOHA_SCENE_CALL);