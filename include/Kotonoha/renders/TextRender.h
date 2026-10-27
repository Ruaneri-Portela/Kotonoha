#pragma once
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/Time.h>
#include <ass/ass.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kotonoha_Subtitles {
  ASS_Library *ass_library;
  ASS_Renderer *ass_renderer;
  ASS_Track *track;

  Kotonoha_Time *time;
  Kotonoha_Game *gameCtx;
  SDL_Renderer *glyphRenderer;
  SDL_Texture *glyphTexture;
  int glyphTextureWidth;
  int glyphTextureHeight;
  bool messageLayerHiddenLastFrame;
  bool hasRenderedFrame;
  bool lastFrameHasSubtitle;
} Kotonoha_Subtitles;

Kotonoha_Subtitles *KotonohaTextRenderInit(Kotonoha_Time *time,
                                           Kotonoha_Game *gameCtx);

void KotonohaTextRenderShutdown(Kotonoha_Subtitles **object);

enum Kotonoha_Scene_Status KotonohaTextRenderDraw(KOTONOHA_SCENE_CALL);

#ifdef __cplusplus
}
#endif
