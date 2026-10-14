#pragma once
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/Time.h>
#include <ass/ass.h>

typedef struct Kotonoha_Subtitles {
  ASS_Library *ass_library;
  ASS_Renderer *ass_renderer;
  ASS_Track *track;

  Kotonoha_Time *time;
} Kotonoha_Subtitles;

struct Kotonoha_Subtitles *
KotonohaTextRenderInit(struct Kotonoha_Time *time,
                       struct Kotonoha_Game *gameCtx);

void KotonohaTextRenderShutdown(struct Kotonoha_Subtitles **object);

enum Kotonoha_Scene_Status KotonohaTextRenderDraw(KOTONOHA_SCENE_CALL);