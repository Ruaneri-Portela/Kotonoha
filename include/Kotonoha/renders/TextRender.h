#pragma once
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/Time.h>
#include <ass/ass.h>

struct Kotonoha_subtitles {
  ASS_Library *ass_library;
  ASS_Renderer *ass_renderer;
  ASS_Track *track;

  SDL_Texture *subTexture;
  struct Kotonoha_time *time;
};

struct Kotonoha_subtitles *
Kotonoha_TextRenderInit(struct Kotonoha_time *time,
                        struct Kotonoha_Game *gameCtx);

void Kotonoha_TextRenderShutdown(struct Kotonoha_subtitles **object);

enum Kotonoha_Scene_Status Kotonoha_TextRenderDraw(KOTONOHA_SCENE_CALL);