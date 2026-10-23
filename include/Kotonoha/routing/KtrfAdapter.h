#pragma once

#include "Kotonoha/routing/KtrfRouter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KOTONOHA_SDHQ_PROFILE_ID "overflow.school-days-hq"
#define KOTONOHA_SDHQ_PROFILE_VERSION "1.0.0"
#define KOTONOHA_SDHQ_SCENE_SCHEME "overflow.sdhq:scene-key"
#define KOTONOHA_SDHQ_SCENE_MEDIA_TYPE "application/x-overflow-ors"
#define KOTONOHA_SDHQ_HOOK_CONTRACT "overflow.sdhq.callback/1.0.0"
#define KOTONOHA_SDHQ_NEW_GAME_ENTRY "sdhq:entry:new-game"
#define KOTONOHA_SDHQ_MAX_TRANSITION_TRACE 64u

typedef enum Kotonoha_KtrfNodeKind {
  KOTONOHA_SDHQ_NODE_SCENE = 1,
  KOTONOHA_SDHQ_NODE_DISPATCHER = 2
} Kotonoha_KtrfNodeKind;

typedef enum Kotonoha_KtrfAdvanceStatus {
  KOTONOHA_SDHQ_ADVANCE_UNRESOLVED = 0,
  KOTONOHA_SDHQ_ADVANCE_BLOCKED_CHOICE = 1,
  KOTONOHA_SDHQ_ADVANCE_ADVANCED = 2,
  KOTONOHA_SDHQ_ADVANCE_TERMINAL = 3
} Kotonoha_KtrfAdvanceStatus;

typedef struct Kotonoha_KtrfNodeView {
  uint8_t kind;
  uint8_t reserved[3];
  uint32_t node_index;
  uint32_t resource_index;
  const char *scene_key;
  size_t scene_key_length;
} Kotonoha_KtrfNodeView;

typedef struct Kotonoha_KtrfAdvanceResult {
  uint8_t status;
  uint8_t reserved[3];
  uint32_t hop_count;
  uint32_t transition_trace_count;
  uint32_t transition_trace[KOTONOHA_SDHQ_MAX_TRANSITION_TRACE];
  Kotonoha_KtrfRouteResult route;
  Kotonoha_KtrfNodeView scene;
} Kotonoha_KtrfAdvanceResult;

typedef int (*Kotonoha_EndingFn)(
    int64_t ending_code, void *userdata, Kotonoha_KtrfError *error);

typedef int (*Kotonoha_HookFn)(
    const char *symbol, size_t symbol_length, void *userdata,
    Kotonoha_KtrfError *error);

typedef int (*Kotonoha_EffectAppliedFn)(
    uint32_t effect_index, void *userdata, Kotonoha_KtrfError *error);

typedef struct Kotonoha_KtrfCallbacks {
  Kotonoha_EndingFn register_ending;
  Kotonoha_HookFn call_hook;
  Kotonoha_EffectAppliedFn effect_applied;
  void *userdata;
} Kotonoha_KtrfCallbacks;

typedef struct Kotonoha_KtrfAdapter {
  const Kotonoha_KtrfDocument *document;
  Kotonoha_KtrfRouter router;
  Kotonoha_KtrfCallbacks callbacks;
  uint32_t new_game_entry_index;
  uint32_t max_dispatch_hops;
} Kotonoha_KtrfAdapter;

void Kotonoha_KtrfAdapterInit(
    Kotonoha_KtrfAdapter *adapter);
void Kotonoha_KtrfAdapterClean(
    Kotonoha_KtrfAdapter *adapter);

int Kotonoha_KtrfAdapterBind(
    Kotonoha_KtrfAdapter *adapter,
    const Kotonoha_KtrfDocument *document,
    const Kotonoha_KtrfCallbacks *callbacks,
    Kotonoha_KtrfError *error);

int Kotonoha_KtrfGetNodeView(
    const Kotonoha_KtrfAdapter *adapter, uint32_t node_index,
    Kotonoha_KtrfNodeView *out, Kotonoha_KtrfError *error);

int Kotonoha_KtrfCurrentNodeView(
    const Kotonoha_KtrfAdapter *adapter,
    Kotonoha_KtrfNodeView *out, Kotonoha_KtrfError *error);

int Kotonoha_KtrfResetNewGame(
    Kotonoha_KtrfAdapter *adapter,
    Kotonoha_KtrfNodeView *scene, Kotonoha_KtrfError *error);

int Kotonoha_KtrfCommitChoice(
    Kotonoha_KtrfAdapter *adapter, int64_t value, int *accepted,
    Kotonoha_KtrfError *error);

int Kotonoha_KtrfAdvance(
    Kotonoha_KtrfAdapter *adapter, const char *trigger,
    Kotonoha_KtrfAdvanceResult *out, Kotonoha_KtrfError *error);

int Kotonoha_KtrfAdvanceToScene(
    Kotonoha_KtrfAdapter *adapter, const char *trigger,
    Kotonoha_KtrfAdvanceResult *out, Kotonoha_KtrfError *error);

#ifdef __cplusplus
}
#endif
