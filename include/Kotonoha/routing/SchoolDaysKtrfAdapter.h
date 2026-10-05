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

typedef enum Kotonoha_SchoolDaysKtrfNodeKind {
  KOTONOHA_SDHQ_NODE_SCENE = 1,
  KOTONOHA_SDHQ_NODE_DISPATCHER = 2
} Kotonoha_SchoolDaysKtrfNodeKind;

typedef enum Kotonoha_SchoolDaysKtrfAdvanceStatus {
  KOTONOHA_SDHQ_ADVANCE_UNRESOLVED = 0,
  KOTONOHA_SDHQ_ADVANCE_BLOCKED_CHOICE = 1,
  KOTONOHA_SDHQ_ADVANCE_ADVANCED = 2,
  KOTONOHA_SDHQ_ADVANCE_TERMINAL = 3
} Kotonoha_SchoolDaysKtrfAdvanceStatus;

typedef struct Kotonoha_SchoolDaysKtrfNodeView {
  uint8_t kind;
  uint8_t reserved[3];
  uint32_t node_index;
  uint32_t resource_index;
  const char *scene_key;
  size_t scene_key_length;
} Kotonoha_SchoolDaysKtrfNodeView;

typedef struct Kotonoha_SchoolDaysKtrfAdvanceResult {
  uint8_t status;
  uint8_t reserved[3];
  uint32_t hop_count;
  Kotonoha_KtrfRouteResult route;
  Kotonoha_SchoolDaysKtrfNodeView scene;
} Kotonoha_SchoolDaysKtrfAdvanceResult;

typedef int (*Kotonoha_SchoolDaysEndingFn)(
    int64_t ending_code, void *userdata, Kotonoha_KtrfError *error);

typedef int (*Kotonoha_SchoolDaysHookFn)(
    const char *symbol, size_t symbol_length, void *userdata,
    Kotonoha_KtrfError *error);

typedef struct Kotonoha_SchoolDaysKtrfCallbacks {
  Kotonoha_SchoolDaysEndingFn register_ending;
  Kotonoha_SchoolDaysHookFn call_hook;
  void *userdata;
} Kotonoha_SchoolDaysKtrfCallbacks;

typedef struct Kotonoha_SchoolDaysKtrfAdapter {
  const Kotonoha_KtrfDocument *document;
  Kotonoha_KtrfRouter router;
  Kotonoha_SchoolDaysKtrfCallbacks callbacks;
  uint32_t new_game_entry_index;
  uint32_t max_dispatch_hops;
} Kotonoha_SchoolDaysKtrfAdapter;

void Kotonoha_SchoolDaysKtrfAdapterInit(
    Kotonoha_SchoolDaysKtrfAdapter *adapter);
void Kotonoha_SchoolDaysKtrfAdapterClean(
    Kotonoha_SchoolDaysKtrfAdapter *adapter);

int Kotonoha_SchoolDaysKtrfAdapterBind(
    Kotonoha_SchoolDaysKtrfAdapter *adapter,
    const Kotonoha_KtrfDocument *document,
    const Kotonoha_SchoolDaysKtrfCallbacks *callbacks,
    Kotonoha_KtrfError *error);

int Kotonoha_SchoolDaysKtrfGetNodeView(
    const Kotonoha_SchoolDaysKtrfAdapter *adapter, uint32_t node_index,
    Kotonoha_SchoolDaysKtrfNodeView *out, Kotonoha_KtrfError *error);

int Kotonoha_SchoolDaysKtrfCurrentNodeView(
    const Kotonoha_SchoolDaysKtrfAdapter *adapter,
    Kotonoha_SchoolDaysKtrfNodeView *out, Kotonoha_KtrfError *error);

int Kotonoha_SchoolDaysKtrfResetNewGame(
    Kotonoha_SchoolDaysKtrfAdapter *adapter,
    Kotonoha_SchoolDaysKtrfNodeView *scene, Kotonoha_KtrfError *error);

int Kotonoha_SchoolDaysKtrfCommitChoice(
    Kotonoha_SchoolDaysKtrfAdapter *adapter, int64_t value, int *accepted,
    Kotonoha_KtrfError *error);

int Kotonoha_SchoolDaysKtrfAdvance(
    Kotonoha_SchoolDaysKtrfAdapter *adapter, const char *trigger,
    Kotonoha_SchoolDaysKtrfAdvanceResult *out, Kotonoha_KtrfError *error);

int Kotonoha_SchoolDaysKtrfAdvanceToScene(
    Kotonoha_SchoolDaysKtrfAdapter *adapter, const char *trigger,
    Kotonoha_SchoolDaysKtrfAdvanceResult *out, Kotonoha_KtrfError *error);

#ifdef __cplusplus
}
#endif
