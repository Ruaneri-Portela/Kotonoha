#include "Kotonoha/routing/KtrfAdapter.h"
#include "KtrfRouterInternal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void adapter_clear_error(Kotonoha_KtrfError *error) {
  if (error != NULL) {
    error->code = KOTONOHA_KTRF_OK;
    error->message[0] = '\0';
  }
}

static int adapter_fail(Kotonoha_KtrfError *error,
                        Kotonoha_KtrfErrorCode code,
                        const char *format, ...) {
  if (error != NULL) {
    va_list args;
    error->code = code;
    va_start(args, format);
#if defined(_MSC_VER)
    _vsnprintf_s(error->message, sizeof(error->message), _TRUNCATE, format,
                 args);
#else
    vsnprintf(error->message, sizeof(error->message), format, args);
#endif
    va_end(args);
    error->message[sizeof(error->message) - 1u] = '\0';
  }
  return 0;
}

static int adapter_string(const Kotonoha_KtrfDocument *document,
                          uint32_t string_index, const char **data,
                          size_t *size, Kotonoha_KtrfError *error) {
  return Kotonoha_KtrfGetString(document, string_index, data, size, error);
}

static int adapter_string_equals(const Kotonoha_KtrfDocument *document,
                                 uint32_t string_index, const char *expected,
                                 Kotonoha_KtrfError *error) {
  const char *data = NULL;
  size_t size = 0u;
  size_t expected_size = strlen(expected);
  if (!adapter_string(document, string_index, &data, &size, error))
    return -1;
  return size == expected_size &&
                 (size == 0u || memcmp(data, expected, size) == 0)
             ? 1
             : 0;
}

static int adapter_on_ending(Kotonoha_KtrfRuntime *runtime,
                             uint32_t ending_index, void *userdata,
                             Kotonoha_KtrfError *error) {
  Kotonoha_KtrfAdapter *adapter =
      (Kotonoha_KtrfAdapter *)userdata;
  Kotonoha_KtrfEnding ending;
  (void)runtime;
  if (!Kotonoha_KtrfGetEnding(adapter->document, ending_index,
                              &ending, error))
    return 0;
  if (!ending.has_code ||
      ending.code.kind != KOTONOHA_KTRF_SCALAR_INT64)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "KTRF ending callback has invalid code");
  if (adapter->callbacks.register_ending != NULL)
    return adapter->callbacks.register_ending(
        ending.code.as.int64_value, adapter->callbacks.userdata, error);
  return 1;
}

static int adapter_on_hook(Kotonoha_KtrfRuntime *runtime,
                           uint32_t hook_index,
                           const Kotonoha_KtrfRuntimeValue *arguments,
                           uint32_t argument_count, void *userdata,
                           Kotonoha_KtrfError *error) {
  Kotonoha_KtrfAdapter *adapter =
      (Kotonoha_KtrfAdapter *)userdata;
  Kotonoha_KtrfHook hook;
  const char *symbol = NULL;
  size_t symbol_length = 0u;
  (void)runtime;
  (void)arguments;
  if (argument_count != 0u)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "KTRF hook unexpectedly has arguments");
  if (!Kotonoha_KtrfGetHook(adapter->document, hook_index, &hook, error) ||
      !Kotonoha_KtrfGetString(adapter->document, hook.symbol_str,
                              &symbol, &symbol_length, error))
    return 0;
  if (adapter->callbacks.call_hook != NULL)
    return adapter->callbacks.call_hook(
        symbol, symbol_length, adapter->callbacks.userdata, error);
  return 1;
}

static int adapter_on_effect_applied(uint32_t effect_index, void *userdata,
                                     Kotonoha_KtrfError *error) {
  Kotonoha_KtrfAdapter *adapter =
      (Kotonoha_KtrfAdapter *)userdata;
  if (adapter == NULL)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "KTRF effect observer adapter is null");
  if (adapter->callbacks.effect_applied != NULL)
    return adapter->callbacks.effect_applied(
        effect_index, adapter->callbacks.userdata, error);
  return 1;
}

void Kotonoha_KtrfAdapterInit(
    Kotonoha_KtrfAdapter *adapter) {
  if (adapter == NULL)
    return;
  memset(adapter, 0, sizeof(*adapter));
  Kotonoha_KtrfRouterInit(&adapter->router);
  adapter->new_game_entry_index = KOTONOHA_KTRF_NULL_INDEX;
}

void Kotonoha_KtrfAdapterClean(
    Kotonoha_KtrfAdapter *adapter) {
  if (adapter == NULL)
    return;
  Kotonoha_KtrfRouterClean(&adapter->router);
  memset(adapter, 0, sizeof(*adapter));
  adapter->new_game_entry_index = KOTONOHA_KTRF_NULL_INDEX;
}

int Kotonoha_KtrfAdapterBind(
    Kotonoha_KtrfAdapter *adapter,
    const Kotonoha_KtrfDocument *document,
    const Kotonoha_KtrfProfileConfig *profile,
    const Kotonoha_KtrfCallbacks *callbacks,
    Kotonoha_KtrfError *error) {
  Kotonoha_KtrfRuntimeCallbacks runtime_callbacks;
  uint32_t entry_index = KOTONOHA_KTRF_NULL_INDEX;

  adapter_clear_error(error);
  if (adapter == NULL || document == NULL || profile == NULL ||
      profile->validate_document == NULL || profile->max_dispatch_hops == 0u ||
      profile->max_dispatch_hops > KOTONOHA_KTRF_MAX_TRANSITION_TRACE)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "KTRF adapter/document/profile argument is invalid");

  if (!profile->validate_document(document, &entry_index, error))
    return 0;

  Kotonoha_KtrfAdapterClean(adapter);
  adapter->document = document;
  adapter->profile = profile;
  adapter->new_game_entry_index = entry_index;
  adapter->max_dispatch_hops = profile->max_dispatch_hops;
  if (callbacks != NULL)
    adapter->callbacks = *callbacks;

  memset(&runtime_callbacks, 0, sizeof(runtime_callbacks));
  runtime_callbacks.register_ending = adapter_on_ending;
  runtime_callbacks.call_hook = adapter_on_hook;
  runtime_callbacks.userdata = adapter;

  if (!Kotonoha_KtrfRouterBind(&adapter->router, document,
                                &runtime_callbacks, error)) {
    Kotonoha_KtrfAdapterClean(adapter);
    return 0;
  }
  return 1;
}

int Kotonoha_KtrfGetNodeView(
    const Kotonoha_KtrfAdapter *adapter, uint32_t node_index,
    Kotonoha_KtrfNodeView *out, Kotonoha_KtrfError *error) {
  Kotonoha_KtrfNode node;
  int is_scene;
  int is_dispatcher;

  adapter_clear_error(error);
  if (adapter == NULL || adapter->document == NULL || out == NULL)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "KTRF node-view argument is null");
  memset(out, 0, sizeof(*out));
  out->node_index = node_index;
  out->resource_index = KOTONOHA_KTRF_NULL_INDEX;

  if (!Kotonoha_KtrfGetNode(adapter->document, node_index, &node, error))
    return 0;
  is_scene = adapter_string_equals(adapter->document, node.kind_str,
                                   "ktrf:scene", error);
  if (is_scene < 0)
    return 0;
  is_dispatcher = adapter_string_equals(adapter->document, node.kind_str,
                                        "ktrf:dispatcher", error);
  if (is_dispatcher < 0)
    return 0;

  if (is_dispatcher) {
    out->kind = KOTONOHA_KTRF_NODE_DISPATCHER;
    return 1;
  }
  if (!is_scene || node.resource_count != 1u)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "KTRF node %u is not a valid scene", node_index);

  {
    Kotonoha_KtrfResource resource;
    uint32_t resource_index;
    if (!Kotonoha_KtrfGetNodeResource(adapter->document, node_index, 0u,
                                       &resource_index, error) ||
        !Kotonoha_KtrfGetResource(adapter->document, resource_index,
                                  &resource, error) ||
        !Kotonoha_KtrfGetString(adapter->document, resource.value_str,
                                &out->scene_key, &out->scene_key_length,
                                error))
      return 0;
    out->kind = KOTONOHA_KTRF_NODE_SCENE;
    out->resource_index = resource_index;
  }
  return 1;
}

int Kotonoha_KtrfCurrentNodeView(
    const Kotonoha_KtrfAdapter *adapter,
    Kotonoha_KtrfNodeView *out, Kotonoha_KtrfError *error) {
  if (adapter == NULL)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "KTRF adapter is null");
  if (adapter->router.current_node_index == KOTONOHA_KTRF_NULL_INDEX)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "KTRF adapter has no active node");
  return Kotonoha_KtrfGetNodeView(
      adapter, adapter->router.current_node_index, out, error);
}

int Kotonoha_KtrfResetNewGame(
    Kotonoha_KtrfAdapter *adapter,
    Kotonoha_KtrfNodeView *scene, Kotonoha_KtrfError *error) {
  adapter_clear_error(error);
  if (adapter == NULL || adapter->document == NULL || scene == NULL)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "KTRF reset argument is null");
  if (!Kotonoha_KtrfRouterResetEntry(
          &adapter->router, adapter->new_game_entry_index, error))
    return 0;
  if (!Kotonoha_KtrfCurrentNodeView(adapter, scene, error))
    return 0;
  if (scene->kind != KOTONOHA_KTRF_NODE_SCENE)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "KTRF new-game entry is not a physical scene");
  return 1;
}

int Kotonoha_KtrfCommitChoice(
    Kotonoha_KtrfAdapter *adapter, int64_t value, int *accepted,
    Kotonoha_KtrfError *error) {
  uint32_t count = 0u;
  uint32_t choice_index = KOTONOHA_KTRF_NULL_INDEX;
  Kotonoha_KtrfRuntimeValue runtime_value;

  adapter_clear_error(error);
  if (adapter == NULL || adapter->document == NULL || accepted == NULL)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "KTRF choice argument is null");
  *accepted = 0;

  if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router,
                                              &count, error))
    return 0;
  if (count != 1u)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "current KTRF node has %u choices", count);
  if (!Kotonoha_KtrfRouterCurrentChoiceAt(
          &adapter->router, 0u, &choice_index, error))
    return 0;

  memset(&runtime_value, 0, sizeof(runtime_value));
  runtime_value.kind = KOTONOHA_KTRF_RUNTIME_INT64;
  runtime_value.as.int64_value = value;
  return Kotonoha_KtrfRouterCommitChoiceValueObserved(
      &adapter->router, choice_index, &runtime_value, accepted,
      adapter_on_effect_applied, adapter, error);
}

static void adapter_init_advance_result(
    Kotonoha_KtrfAdvanceResult *out) {
  uint32_t i;
  memset(out, 0, sizeof(*out));
  for (i = 0u; i < KOTONOHA_KTRF_MAX_TRANSITION_TRACE; ++i)
    out->transition_trace[i] = KOTONOHA_KTRF_NULL_INDEX;
  out->route.transition_index = KOTONOHA_KTRF_NULL_INDEX;
  out->route.source_node_index = KOTONOHA_KTRF_NULL_INDEX;
  out->route.destination_node_index = KOTONOHA_KTRF_NULL_INDEX;
  out->scene.node_index = KOTONOHA_KTRF_NULL_INDEX;
  out->scene.resource_index = KOTONOHA_KTRF_NULL_INDEX;
}

int Kotonoha_KtrfAdvance(
    Kotonoha_KtrfAdapter *adapter, const char *trigger,
    Kotonoha_KtrfAdvanceResult *out, Kotonoha_KtrfError *error) {
  adapter_clear_error(error);
  if (adapter == NULL || adapter->document == NULL || out == NULL)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "KTRF advance argument is null");

  adapter_init_advance_result(out);
  if (!Kotonoha_KtrfRouterTriggerObserved(
          &adapter->router, trigger, &out->route,
          adapter_on_effect_applied, adapter, error))
    return 0;
  out->hop_count = 1u;
  if (out->route.transition_index != KOTONOHA_KTRF_NULL_INDEX) {
    out->transition_trace[0] = out->route.transition_index;
    out->transition_trace_count = 1u;
  }

  switch (out->route.status) {
  case KOTONOHA_KTRF_ROUTE_BLOCKED_CHOICE:
    out->status = KOTONOHA_KTRF_ADVANCE_BLOCKED_CHOICE;
    return 1;
  case KOTONOHA_KTRF_ROUTE_NONE:
    out->status = KOTONOHA_KTRF_ADVANCE_UNRESOLVED;
    return 1;
  case KOTONOHA_KTRF_ROUTE_TERMINAL:
    out->status = KOTONOHA_KTRF_ADVANCE_TERMINAL;
    return 1;
  case KOTONOHA_KTRF_ROUTE_ADVANCED:
    if (!Kotonoha_KtrfCurrentNodeView(
            adapter, &out->scene, error))
      return 0;
    out->status = KOTONOHA_KTRF_ADVANCE_ADVANCED;
    return 1;
  default:
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "KTRF router returned unknown status %u",
                        out->route.status);
  }
}

int Kotonoha_KtrfAdvanceToScene(
    Kotonoha_KtrfAdapter *adapter, const char *trigger,
    Kotonoha_KtrfAdvanceResult *out, Kotonoha_KtrfError *error) {
  const char *next_trigger = trigger;
  uint32_t hops = 0u;
  uint32_t trace_count = 0u;
  uint32_t trace[KOTONOHA_KTRF_MAX_TRANSITION_TRACE];

  adapter_clear_error(error);
  if (adapter == NULL || adapter->document == NULL || out == NULL)
    return adapter_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                        "KTRF advance-to-scene argument is null");

  memset(trace, 0xff, sizeof(trace));
  for (;;) {
    Kotonoha_KtrfAdvanceResult step;
    if (hops >= adapter->max_dispatch_hops)
      return adapter_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                          "KTRF dispatcher chain exceeded %u hops",
                          adapter->max_dispatch_hops);
    if (!Kotonoha_KtrfAdvance(
            adapter, next_trigger, &step, error))
      return 0;
    ++hops;
    if (step.transition_trace_count != 0u) {
      uint32_t i;
      if (trace_count + step.transition_trace_count >
          KOTONOHA_KTRF_MAX_TRANSITION_TRACE)
        return adapter_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                            "KTRF transition trace exceeded %u hops",
                            KOTONOHA_KTRF_MAX_TRANSITION_TRACE);
      for (i = 0u; i < step.transition_trace_count; ++i)
        trace[trace_count++] = step.transition_trace[i];
    }
    *out = step;
    out->hop_count = hops;
    out->transition_trace_count = trace_count;
    memcpy(out->transition_trace, trace,
           sizeof(uint32_t) * KOTONOHA_KTRF_MAX_TRANSITION_TRACE);

    if (step.status != KOTONOHA_KTRF_ADVANCE_ADVANCED)
      return 1;
    if (step.scene.kind == KOTONOHA_KTRF_NODE_SCENE)
      return 1;
    if (step.scene.kind != KOTONOHA_KTRF_NODE_DISPATCHER)
      return adapter_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                          "KTRF adapter reached unknown node kind");
    next_trigger = "ktrf:next";
  }
}
