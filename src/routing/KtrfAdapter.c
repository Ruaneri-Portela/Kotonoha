#include "Kotonoha/routing/KtrfAdapter.h"
#include "KtrfRouterInternal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SDHQ_EXPECTED_NODES UINT32_C(1857)
#define SDHQ_EXPECTED_RESOURCES UINT32_C(1855)
#define SDHQ_EXPECTED_CHOICES UINT32_C(287)
#define SDHQ_EXPECTED_HOOKS UINT32_C(3)
#define SDHQ_EXPECTED_ENDINGS UINT32_C(22)
#define SDHQ_DEFAULT_MAX_DISPATCH_HOPS UINT32_C(64)

static void sdhq_clear_error(Kotonoha_KtrfError *error) {
  if (error != NULL) {
    error->code = KOTONOHA_KTRF_OK;
    error->message[0] = '\0';
  }
}

static int sdhq_fail(Kotonoha_KtrfError *error,
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

static uint32_t sdhq_count(const Kotonoha_KtrfDocument *document,
                           const char type[4]) {
  const Kotonoha_KtrfSection *section =
      Kotonoha_KtrfFindSection(document, type);
  return section != NULL ? section->item_count : 0u;
}

static int sdhq_string(const Kotonoha_KtrfDocument *document,
                       uint32_t string_index, const char **data,
                       size_t *size, Kotonoha_KtrfError *error) {
  return Kotonoha_KtrfGetString(document, string_index, data, size, error);
}

static int sdhq_string_equals(const Kotonoha_KtrfDocument *document,
                              uint32_t string_index, const char *expected,
                              Kotonoha_KtrfError *error) {
  const char *data = NULL;
  size_t size = 0u;
  size_t expected_size = strlen(expected);
  if (!sdhq_string(document, string_index, &data, &size, error))
    return -1;
  return size == expected_size &&
                 (size == 0u || memcmp(data, expected, size) == 0)
             ? 1
             : 0;
}

static int sdhq_string_starts_with(const Kotonoha_KtrfDocument *document,
                                   uint32_t string_index,
                                   const char *prefix,
                                   Kotonoha_KtrfError *error) {
  const char *data = NULL;
  size_t size = 0u;
  size_t prefix_size = strlen(prefix);
  if (!sdhq_string(document, string_index, &data, &size, error))
    return -1;
  return size >= prefix_size &&
                 (prefix_size == 0u ||
                  memcmp(data, prefix, prefix_size) == 0)
             ? 1
             : 0;
}

static int sdhq_find_entry(const Kotonoha_KtrfDocument *document,
                           const char *id, uint32_t *entry_index,
                           Kotonoha_KtrfError *error) {
  uint32_t count = sdhq_count(document, "ENTR");
  uint32_t i;
  for (i = 0u; i < count; ++i) {
    Kotonoha_KtrfEntryPoint entry;
    int equal;
    if (!Kotonoha_KtrfGetEntryPoint(document, i, &entry, error))
      return 0;
    equal = sdhq_string_equals(document, entry.id_str, id, error);
    if (equal < 0)
      return 0;
    if (equal) {
      *entry_index = i;
      return 1;
    }
  }
  return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                   "KTRF entry point '%s' is missing", id);
}

static int sdhq_validate_profile(const Kotonoha_KtrfDocument *document,
                                 uint32_t *new_game_entry,
                                 Kotonoha_KtrfError *error) {
  Kotonoha_KtrfMeta meta;
  uint32_t i;
  uint32_t scene_nodes = 0u;
  uint32_t dispatch_nodes = 0u;
  uint32_t seen_ending_mask = 0u;
  int equal;

  if (!Kotonoha_KtrfValidateTypedCore(document, error))
    return 0;
  if (!Kotonoha_KtrfGetMeta(document, &meta, error))
    return 0;

  equal = sdhq_string_equals(document, meta.profile_id_str,
                             KOTONOHA_SDHQ_PROFILE_ID, error);
  if (equal <= 0)
    return equal < 0
               ? 0
               : sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                           "KTRF profile is not %s",
                           KOTONOHA_SDHQ_PROFILE_ID);

  equal = sdhq_string_equals(document, meta.profile_version_str,
                             KOTONOHA_SDHQ_PROFILE_VERSION, error);
  if (equal <= 0)
    return equal < 0
               ? 0
               : sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                           "unsupported KTRF profile version");

  if (sdhq_count(document, "NODE") != SDHQ_EXPECTED_NODES ||
      sdhq_count(document, "RSRC") != SDHQ_EXPECTED_RESOURCES ||
      sdhq_count(document, "CHOI") != SDHQ_EXPECTED_CHOICES ||
      sdhq_count(document, "HOOK") != SDHQ_EXPECTED_HOOKS ||
      sdhq_count(document, "ENDG") != SDHQ_EXPECTED_ENDINGS) {
    return sdhq_fail(
        error, KOTONOHA_KTRF_ERROR_FORMAT,
        "KTRF profile inventory mismatch");
  }

  if (!sdhq_find_entry(document, KOTONOHA_SDHQ_NEW_GAME_ENTRY,
                       new_game_entry, error))
    return 0;

  for (i = 0u; i < SDHQ_EXPECTED_NODES; ++i) {
    Kotonoha_KtrfNode node;
    int is_scene;
    int is_dispatcher;
    if (!Kotonoha_KtrfGetNode(document, i, &node, error))
      return 0;
    is_scene = sdhq_string_equals(document, node.kind_str,
                                  "ktrf:scene", error);
    if (is_scene < 0)
      return 0;
    is_dispatcher = sdhq_string_equals(document, node.kind_str,
                                       "ktrf:dispatcher", error);
    if (is_dispatcher < 0)
      return 0;

    if (is_scene) {
      uint32_t resource_index;
      Kotonoha_KtrfResource resource;
      int scheme_ok;
      int media_ok;
      if (node.resource_count != 1u)
        return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                         "KTRF scene node %u has %u resources",
                         i, node.resource_count);
      if (!Kotonoha_KtrfGetNodeResource(document, i, 0u,
                                         &resource_index, error) ||
          !Kotonoha_KtrfGetResource(document, resource_index,
                                    &resource, error))
        return 0;
      scheme_ok = sdhq_string_equals(document, resource.scheme_str,
                                     KOTONOHA_SDHQ_SCENE_SCHEME, error);
      if (scheme_ok < 0)
        return 0;
      media_ok = sdhq_string_equals(document, resource.media_type_str,
                                    KOTONOHA_SDHQ_SCENE_MEDIA_TYPE, error);
      if (media_ok < 0)
        return 0;
      if (!scheme_ok || !media_ok ||
          (resource.flags & (KOTONOHA_KTRF_RSRC_REQUIRED |
                             KOTONOHA_KTRF_RSRC_HAS_REQUIRED)) !=
              (KOTONOHA_KTRF_RSRC_REQUIRED |
               KOTONOHA_KTRF_RSRC_HAS_REQUIRED))
        return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                         "KTRF scene resource %u is malformed",
                         resource_index);
      ++scene_nodes;
    } else if (is_dispatcher) {
      if (node.resource_count != 0u)
        return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                         "KTRF dispatcher node %u owns resources", i);
      ++dispatch_nodes;
    } else {
      return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "KTRF node %u has unsupported kind", i);
    }
  }

  if (scene_nodes != SDHQ_EXPECTED_RESOURCES || dispatch_nodes != 2u)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "KTRF scene/dispatcher inventory mismatch");

  for (i = 0u; i < SDHQ_EXPECTED_HOOKS; ++i) {
    Kotonoha_KtrfHook hook;
    int symbol_ok;
    int contract_ok;
    if (!Kotonoha_KtrfGetHook(document, i, &hook, error))
      return 0;
    symbol_ok = sdhq_string_starts_with(document, hook.symbol_str,
                                        "overflow.sdhq:", error);
    if (symbol_ok < 0)
      return 0;
    contract_ok = sdhq_string_equals(document, hook.contract_str,
                                     KOTONOHA_SDHQ_HOOK_CONTRACT, error);
    if (contract_ok < 0)
      return 0;
    if (!symbol_ok || !contract_ok)
      return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "KTRF hook %u is malformed", i);
  }

  for (i = 0u; i < SDHQ_EXPECTED_ENDINGS; ++i) {
    Kotonoha_KtrfEnding ending;
    int64_t code;
    if (!Kotonoha_KtrfGetEnding(document, i, &ending, error))
      return 0;
    if (!ending.has_code ||
        ending.code.kind != KOTONOHA_KTRF_SCALAR_INT64)
      return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "KTRF ending %u lacks int64 code", i);
    code = ending.code.as.int64_value;
    if (code < 0 || code >= (int64_t)SDHQ_EXPECTED_ENDINGS)
      return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "KTRF ending code out of range: %lld",
                       (long long)code);
    if ((seen_ending_mask & (UINT32_C(1) << (uint32_t)code)) != 0u)
      return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "duplicate KTRF ending code: %lld",
                       (long long)code);
    seen_ending_mask |= UINT32_C(1) << (uint32_t)code;
  }

  if (seen_ending_mask != ((UINT32_C(1) << SDHQ_EXPECTED_ENDINGS) - 1u))
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "KTRF ending catalog is incomplete");

  return 1;
}

static int sdhq_on_ending(Kotonoha_KtrfRuntime *runtime,
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
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "KTRF ending callback has invalid code");
  if (adapter->callbacks.register_ending != NULL)
    return adapter->callbacks.register_ending(
        ending.code.as.int64_value, adapter->callbacks.userdata, error);
  return 1;
}

static int sdhq_on_hook(Kotonoha_KtrfRuntime *runtime,
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
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
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

static int sdhq_on_effect_applied(uint32_t effect_index, void *userdata,
                                  Kotonoha_KtrfError *error) {
  Kotonoha_KtrfAdapter *adapter =
      (Kotonoha_KtrfAdapter *)userdata;
  if (adapter == NULL)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
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
  adapter->max_dispatch_hops = SDHQ_DEFAULT_MAX_DISPATCH_HOPS;
}

void Kotonoha_KtrfAdapterClean(
    Kotonoha_KtrfAdapter *adapter) {
  if (adapter == NULL)
    return;
  Kotonoha_KtrfRouterClean(&adapter->router);
  memset(adapter, 0, sizeof(*adapter));
  adapter->new_game_entry_index = KOTONOHA_KTRF_NULL_INDEX;
  adapter->max_dispatch_hops = SDHQ_DEFAULT_MAX_DISPATCH_HOPS;
}

int Kotonoha_KtrfAdapterBind(
    Kotonoha_KtrfAdapter *adapter,
    const Kotonoha_KtrfDocument *document,
    const Kotonoha_KtrfCallbacks *callbacks,
    Kotonoha_KtrfError *error) {
  Kotonoha_KtrfRuntimeCallbacks runtime_callbacks;
  uint32_t entry_index = KOTONOHA_KTRF_NULL_INDEX;

  sdhq_clear_error(error);
  if (adapter == NULL || document == NULL)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "KTRF adapter/document argument is null");

  if (!sdhq_validate_profile(document, &entry_index, error))
    return 0;

  Kotonoha_KtrfAdapterClean(adapter);
  adapter->document = document;
  adapter->new_game_entry_index = entry_index;
  if (callbacks != NULL)
    adapter->callbacks = *callbacks;

  memset(&runtime_callbacks, 0, sizeof(runtime_callbacks));
  runtime_callbacks.register_ending = sdhq_on_ending;
  runtime_callbacks.call_hook = sdhq_on_hook;
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

  sdhq_clear_error(error);
  if (adapter == NULL || adapter->document == NULL || out == NULL)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "KTRF node-view argument is null");
  memset(out, 0, sizeof(*out));
  out->node_index = node_index;
  out->resource_index = KOTONOHA_KTRF_NULL_INDEX;

  if (!Kotonoha_KtrfGetNode(adapter->document, node_index, &node, error))
    return 0;
  is_scene = sdhq_string_equals(adapter->document, node.kind_str,
                                "ktrf:scene", error);
  if (is_scene < 0)
    return 0;
  is_dispatcher = sdhq_string_equals(adapter->document, node.kind_str,
                                     "ktrf:dispatcher", error);
  if (is_dispatcher < 0)
    return 0;

  if (is_dispatcher) {
    out->kind = KOTONOHA_SDHQ_NODE_DISPATCHER;
    return 1;
  }
  if (!is_scene || node.resource_count != 1u)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
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
    out->kind = KOTONOHA_SDHQ_NODE_SCENE;
    out->resource_index = resource_index;
  }
  return 1;
}

int Kotonoha_KtrfCurrentNodeView(
    const Kotonoha_KtrfAdapter *adapter,
    Kotonoha_KtrfNodeView *out, Kotonoha_KtrfError *error) {
  if (adapter == NULL)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "KTRF adapter is null");
  if (adapter->router.current_node_index == KOTONOHA_KTRF_NULL_INDEX)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "KTRF adapter has no active node");
  return Kotonoha_KtrfGetNodeView(
      adapter, adapter->router.current_node_index, out, error);
}

int Kotonoha_KtrfResetNewGame(
    Kotonoha_KtrfAdapter *adapter,
    Kotonoha_KtrfNodeView *scene, Kotonoha_KtrfError *error) {
  sdhq_clear_error(error);
  if (adapter == NULL || adapter->document == NULL || scene == NULL)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "KTRF reset argument is null");
  if (!Kotonoha_KtrfRouterResetEntry(
          &adapter->router, adapter->new_game_entry_index, error))
    return 0;
  if (!Kotonoha_KtrfCurrentNodeView(adapter, scene, error))
    return 0;
  if (scene->kind != KOTONOHA_SDHQ_NODE_SCENE)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "KTRF new-game entry is not a physical scene");
  return 1;
}

int Kotonoha_KtrfCommitChoice(
    Kotonoha_KtrfAdapter *adapter, int64_t value, int *accepted,
    Kotonoha_KtrfError *error) {
  uint32_t count = 0u;
  uint32_t choice_index = KOTONOHA_KTRF_NULL_INDEX;
  Kotonoha_KtrfRuntimeValue runtime_value;

  sdhq_clear_error(error);
  if (adapter == NULL || adapter->document == NULL || accepted == NULL)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "KTRF choice argument is null");
  *accepted = 0;

  if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router,
                                              &count, error))
    return 0;
  if (count != 1u)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "current KTRF node has %u choices", count);
  if (!Kotonoha_KtrfRouterCurrentChoiceAt(
          &adapter->router, 0u, &choice_index, error))
    return 0;

  memset(&runtime_value, 0, sizeof(runtime_value));
  runtime_value.kind = KOTONOHA_KTRF_RUNTIME_INT64;
  runtime_value.as.int64_value = value;
  return Kotonoha_KtrfRouterCommitChoiceValueObserved(
      &adapter->router, choice_index, &runtime_value, accepted,
      sdhq_on_effect_applied, adapter, error);
}

static void sdhq_init_advance_result(
    Kotonoha_KtrfAdvanceResult *out) {
  uint32_t i;
  memset(out, 0, sizeof(*out));
  for (i = 0u; i < KOTONOHA_SDHQ_MAX_TRANSITION_TRACE; ++i)
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
  sdhq_clear_error(error);
  if (adapter == NULL || adapter->document == NULL || out == NULL)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "KTRF advance argument is null");

  sdhq_init_advance_result(out);
  if (!Kotonoha_KtrfRouterTriggerObserved(
          &adapter->router, trigger, &out->route,
          sdhq_on_effect_applied, adapter, error))
    return 0;
  out->hop_count = 1u;
  if (out->route.transition_index != KOTONOHA_KTRF_NULL_INDEX) {
    out->transition_trace[0] = out->route.transition_index;
    out->transition_trace_count = 1u;
  }

  switch (out->route.status) {
  case KOTONOHA_KTRF_ROUTE_BLOCKED_CHOICE:
    out->status = KOTONOHA_SDHQ_ADVANCE_BLOCKED_CHOICE;
    return 1;
  case KOTONOHA_KTRF_ROUTE_NONE:
    out->status = KOTONOHA_SDHQ_ADVANCE_UNRESOLVED;
    return 1;
  case KOTONOHA_KTRF_ROUTE_TERMINAL:
    out->status = KOTONOHA_SDHQ_ADVANCE_TERMINAL;
    return 1;
  case KOTONOHA_KTRF_ROUTE_ADVANCED:
    if (!Kotonoha_KtrfCurrentNodeView(
            adapter, &out->scene, error))
      return 0;
    out->status = KOTONOHA_SDHQ_ADVANCE_ADVANCED;
    return 1;
  default:
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
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
  uint32_t trace[KOTONOHA_SDHQ_MAX_TRANSITION_TRACE];

  sdhq_clear_error(error);
  if (adapter == NULL || adapter->document == NULL || out == NULL)
    return sdhq_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "KTRF advance-to-scene argument is null");

  memset(trace, 0xff, sizeof(trace));
  for (;;) {
    Kotonoha_KtrfAdvanceResult step;
    if (hops >= adapter->max_dispatch_hops)
      return sdhq_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                       "KTRF dispatcher chain exceeded %u hops",
                       adapter->max_dispatch_hops);
    if (!Kotonoha_KtrfAdvance(
            adapter, next_trigger, &step, error))
      return 0;
    ++hops;
    if (step.transition_trace_count != 0u) {
      uint32_t i;
      if (trace_count + step.transition_trace_count >
          KOTONOHA_SDHQ_MAX_TRANSITION_TRACE)
        return sdhq_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                         "KTRF transition trace exceeded %u hops",
                         KOTONOHA_SDHQ_MAX_TRANSITION_TRACE);
      for (i = 0u; i < step.transition_trace_count; ++i)
        trace[trace_count++] = step.transition_trace[i];
    }
    *out = step;
    out->hop_count = hops;
    out->transition_trace_count = trace_count;
    memcpy(out->transition_trace, trace,
           sizeof(uint32_t) * KOTONOHA_SDHQ_MAX_TRANSITION_TRACE);

    if (step.status != KOTONOHA_SDHQ_ADVANCE_ADVANCED)
      return 1;
    if (step.scene.kind == KOTONOHA_SDHQ_NODE_SCENE)
      return 1;
    if (step.scene.kind != KOTONOHA_SDHQ_NODE_DISPATCHER)
      return sdhq_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "KTRF adapter reached unknown node kind");
    next_trigger = "ktrf:next";
  }
}
