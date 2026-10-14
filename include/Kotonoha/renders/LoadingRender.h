#pragma once
#include <Kotonoha/Kotonoha.h>

#ifdef __cplusplus
extern "C" {
#endif

enum Kotonoha_Scene_Status KotonohaLoadingRender(KOTONOHA_SCENE_CALL);
void KotonohaLoadingRenderShutdown(void);

#ifdef __cplusplus
}
#endif