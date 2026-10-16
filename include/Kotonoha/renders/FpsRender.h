#pragma once
#include <Kotonoha/Kotonoha.h>
#include <SDL3_ttf/SDL_ttf.h>

#ifdef __cplusplus
extern "C" {
#endif

enum Kotonoha_Scene_Status KotonohaFPSRender(KOTONOHA_SCENE_CALL);
void KotonohaFPSRenderShutdown(void);

#ifdef __cplusplus
}
#endif
