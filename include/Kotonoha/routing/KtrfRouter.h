#pragma once

#include "Kotonoha/routing/KtrfRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Kotonoha_KtrfRouteStatus {
  KOTONOHA_KTRF_ROUTE_NONE = 0,
  KOTONOHA_KTRF_ROUTE_BLOCKED_CHOICE = 1,
  KOTONOHA_KTRF_ROUTE_ADVANCED = 2,
  KOTONOHA_KTRF_ROUTE_TERMINAL = 3
} Kotonoha_KtrfRouteStatus;

typedef struct Kotonoha_KtrfRouteResult {
  uint8_t status;
  uint8_t reserved[3];
  uint32_t transition_index;
  uint32_t source_node_index;
  uint32_t destination_node_index;
  uint32_t priority;
} Kotonoha_KtrfRouteResult;

typedef struct Kotonoha_KtrfRouter {
  Kotonoha_KtrfRuntime runtime;
  const Kotonoha_KtrfDocument *document;
  uint32_t current_node_index;
  int terminal;

  uint8_t *choice_committed;
  Kotonoha_KtrfRuntimeValue *choice_values;
  uint32_t choice_count;
} Kotonoha_KtrfRouter;

void Kotonoha_KtrfRouterInit(Kotonoha_KtrfRouter *router);
void Kotonoha_KtrfRouterClean(Kotonoha_KtrfRouter *router);

int Kotonoha_KtrfRouterBind(Kotonoha_KtrfRouter *router,
                            const Kotonoha_KtrfDocument *document,
                            const Kotonoha_KtrfRuntimeCallbacks *callbacks,
                            Kotonoha_KtrfError *error);

int Kotonoha_KtrfRouterResetEntry(Kotonoha_KtrfRouter *router,
                                  uint32_t entry_point_index,
                                  Kotonoha_KtrfError *error);

int Kotonoha_KtrfRouterActivateNode(Kotonoha_KtrfRouter *router,
                                    uint32_t node_index,
                                    Kotonoha_KtrfError *error);

int Kotonoha_KtrfRouterCurrentChoiceCount(const Kotonoha_KtrfRouter *router,
                                          uint32_t *count,
                                          Kotonoha_KtrfError *error);

int Kotonoha_KtrfRouterCurrentChoiceAt(const Kotonoha_KtrfRouter *router,
                                       uint32_t ordinal,
                                       uint32_t *choice_index,
                                       Kotonoha_KtrfError *error);

int Kotonoha_KtrfRouterCommitChoiceValue(
    Kotonoha_KtrfRouter *router, uint32_t choice_index,
    const Kotonoha_KtrfRuntimeValue *value, int *accepted,
    Kotonoha_KtrfError *error);

int Kotonoha_KtrfRouterCommitChoiceOption(
    Kotonoha_KtrfRouter *router, uint32_t choice_index,
    uint32_t local_option_index, int *accepted, Kotonoha_KtrfError *error);

int Kotonoha_KtrfRouterCommitChoiceTimeout(
    Kotonoha_KtrfRouter *router, uint32_t choice_index, int *accepted,
    Kotonoha_KtrfError *error);

int Kotonoha_KtrfRouterTrigger(Kotonoha_KtrfRouter *router,
                               const char *trigger,
                               Kotonoha_KtrfRouteResult *out,
                               Kotonoha_KtrfError *error);

#ifdef __cplusplus
}
#endif
