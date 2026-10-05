#include "Kotonoha/routing/KtrfRouter.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rr_clear_error(Kotonoha_KtrfError *error) {
  if (error != NULL) {
    error->code = KOTONOHA_KTRF_OK;
    error->message[0] = '\0';
  }
}

static int rr_fail(Kotonoha_KtrfError *error, Kotonoha_KtrfErrorCode code,
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

static uint32_t rr_count(const Kotonoha_KtrfDocument *document,
                         const char type[4]) {
  const Kotonoha_KtrfSection *section =
      Kotonoha_KtrfFindSection(document, type);
  return section != NULL ? section->item_count : 0u;
}

static int rr_string_equals(const Kotonoha_KtrfDocument *document,
                            uint32_t string_index, const char *text,
                            Kotonoha_KtrfError *error) {
  const char *raw;
  size_t size;
  size_t expected;
  if (text == NULL)
    return 0;
  expected = strlen(text);
  if (!Kotonoha_KtrfGetString(document, string_index, &raw, &size, error))
    return -1;
  return size == expected &&
                 (size == 0u || memcmp(raw, text, size) == 0)
             ? 1
             : 0;
}

static int rr_is_numeric(uint8_t kind) {
  return kind == KOTONOHA_KTRF_RUNTIME_BOOL ||
         kind == KOTONOHA_KTRF_RUNTIME_INT64 ||
         kind == KOTONOHA_KTRF_RUNTIME_UINT64 ||
         kind == KOTONOHA_KTRF_RUNTIME_FLOAT64;
}

static long double rr_numeric(const Kotonoha_KtrfRuntimeValue *value) {
  switch (value->kind) {
  case KOTONOHA_KTRF_RUNTIME_BOOL:
    return value->as.boolean_value ? 1.0L : 0.0L;
  case KOTONOHA_KTRF_RUNTIME_INT64:
    return (long double)value->as.int64_value;
  case KOTONOHA_KTRF_RUNTIME_UINT64:
    return (long double)value->as.uint64_value;
  case KOTONOHA_KTRF_RUNTIME_FLOAT64:
    return (long double)value->as.float64_value;
  default:
    return 0.0L;
  }
}

static int rr_value_equal(const Kotonoha_KtrfRuntimeValue *a,
                          const Kotonoha_KtrfRuntimeValue *b) {
  if (rr_is_numeric(a->kind) && rr_is_numeric(b->kind))
    return rr_numeric(a) == rr_numeric(b);
  if (a->kind != b->kind)
    return 0;

  switch (a->kind) {
  case KOTONOHA_KTRF_RUNTIME_NULL:
    return 1;
  case KOTONOHA_KTRF_RUNTIME_STRING:
    return a->as.string_value.size == b->as.string_value.size &&
           (a->as.string_value.size == 0u ||
            memcmp(a->as.string_value.data, b->as.string_value.data,
                   a->as.string_value.size) == 0);
  case KOTONOHA_KTRF_RUNTIME_BYTES:
    return a->as.bytes_value.size == b->as.bytes_value.size &&
           (a->as.bytes_value.size == 0u ||
            memcmp(a->as.bytes_value.data, b->as.bytes_value.data,
                   a->as.bytes_value.size) == 0);
  case KOTONOHA_KTRF_RUNTIME_ENTITY:
    return a->entity_kind == b->entity_kind &&
           a->as.entity_index == b->as.entity_index;
  default:
    return 0;
  }
}

static int rr_truthy(const Kotonoha_KtrfRuntimeValue *value) {
  switch (value->kind) {
  case KOTONOHA_KTRF_RUNTIME_NULL:
    return 0;
  case KOTONOHA_KTRF_RUNTIME_BOOL:
    return value->as.boolean_value != 0;
  case KOTONOHA_KTRF_RUNTIME_INT64:
    return value->as.int64_value != 0;
  case KOTONOHA_KTRF_RUNTIME_UINT64:
    return value->as.uint64_value != 0u;
  case KOTONOHA_KTRF_RUNTIME_FLOAT64:
    return value->as.float64_value != 0.0;
  case KOTONOHA_KTRF_RUNTIME_STRING:
    return value->as.string_value.size != 0u;
  case KOTONOHA_KTRF_RUNTIME_BYTES:
    return value->as.bytes_value.size != 0u;
  case KOTONOHA_KTRF_RUNTIME_ENTITY:
    return 1;
  default:
    return 0;
  }
}

static int rr_scalar_to_value(const Kotonoha_KtrfDocument *document,
                              const Kotonoha_KtrfScalar *scalar,
                              Kotonoha_KtrfRuntimeValue *out,
                              Kotonoha_KtrfError *error) {
  memset(out, 0, sizeof(*out));
  switch (scalar->kind) {
  case KOTONOHA_KTRF_SCALAR_NULL:
    out->kind = KOTONOHA_KTRF_RUNTIME_NULL;
    return 1;
  case KOTONOHA_KTRF_SCALAR_BOOL:
    out->kind = KOTONOHA_KTRF_RUNTIME_BOOL;
    out->as.boolean_value = scalar->as.boolean_value != 0;
    return 1;
  case KOTONOHA_KTRF_SCALAR_INT64:
    out->kind = KOTONOHA_KTRF_RUNTIME_INT64;
    out->as.int64_value = scalar->as.int64_value;
    return 1;
  case KOTONOHA_KTRF_SCALAR_FLOAT64:
    out->kind = KOTONOHA_KTRF_RUNTIME_FLOAT64;
    out->as.float64_value = scalar->as.float64_value;
    return 1;
  case KOTONOHA_KTRF_SCALAR_STRING: {
    const char *data;
    size_t size;
    if (!Kotonoha_KtrfGetString(document, scalar->as.string_index, &data, &size,
                                error))
      return 0;
    out->kind = KOTONOHA_KTRF_RUNTIME_STRING;
    out->as.string_value.data = data;
    out->as.string_value.size = size;
    return 1;
  }
  default:
    return rr_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                   "router cannot convert scalar kind %u", scalar->kind);
  }
}

static void rr_clear_choice_commits(Kotonoha_KtrfRouter *router) {
  if (router->choice_committed != NULL && router->choice_count != 0u)
    memset(router->choice_committed, 0, router->choice_count);
  if (router->choice_values != NULL && router->choice_count != 0u)
    memset(router->choice_values, 0,
           (size_t)router->choice_count * sizeof(*router->choice_values));
}

static int rr_choice_belongs_to_current(const Kotonoha_KtrfRouter *router,
                                        uint32_t choice_index,
                                        Kotonoha_KtrfChoice *choice,
                                        Kotonoha_KtrfError *error) {
  if (!Kotonoha_KtrfGetChoice(router->document, choice_index, choice, error))
    return 0;
  if (router->current_node_index == KOTONOHA_KTRF_NULL_INDEX)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                   "router has no active node");
  if (choice->node_index != router->current_node_index)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "choice %u belongs to node %u, current node is %u",
                   choice_index, choice->node_index,
                   router->current_node_index);
  return 1;
}

static int rr_apply_choice_effects(Kotonoha_KtrfRouter *router,
                                   uint32_t choice_index,
                                   int timeout,
                                   uint32_t local_option_index,
                                   Kotonoha_KtrfError *error) {
  uint32_t count;
  uint32_t i;
  if (timeout) {
    Kotonoha_KtrfChoiceTimeout outcome;
    int has_timeout = 0;
    if (!Kotonoha_KtrfGetChoiceTimeout(router->document, choice_index,
                                       &has_timeout, &outcome, error))
      return 0;
    if (!has_timeout)
      return rr_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "choice %u has no timeout outcome", choice_index);
    count = outcome.effect_count;
    for (i = 0u; i < count; ++i) {
      uint32_t effect_index;
      if (!Kotonoha_KtrfGetChoiceTimeoutEffect(
              router->document, choice_index, i, &effect_index, error))
        return 0;
      if (!Kotonoha_KtrfRuntimeApplyEffect(&router->runtime, effect_index,
                                           error))
        return 0;
    }
    return 1;
  }

  {
    Kotonoha_KtrfChoiceOption option;
    if (!Kotonoha_KtrfGetChoiceOption(router->document, choice_index,
                                      local_option_index, &option, error))
      return 0;
    count = option.effect_count;
    for (i = 0u; i < count; ++i) {
      uint32_t effect_index;
      if (!Kotonoha_KtrfGetChoiceOptionEffect(
              router->document, choice_index, local_option_index, i,
              &effect_index, error))
        return 0;
      if (!Kotonoha_KtrfRuntimeApplyEffect(&router->runtime, effect_index,
                                           error))
        return 0;
    }
  }
  return 1;
}

static int rr_commit_selected(Kotonoha_KtrfRouter *router,
                              uint32_t choice_index,
                              const Kotonoha_KtrfChoice *choice,
                              const Kotonoha_KtrfRuntimeValue *value,
                              int timeout, uint32_t local_option_index,
                              int *accepted, Kotonoha_KtrfError *error) {
  if (router->choice_committed[choice_index] != 0u) {
    *accepted = rr_value_equal(&router->choice_values[choice_index], value);
    return 1;
  }

  if (!Kotonoha_KtrfRuntimeSetVariable(&router->runtime,
                                       choice->result_variable_index, value,
                                       error))
    return 0;

  if (!rr_apply_choice_effects(router, choice_index, timeout,
                               local_option_index, error))
    return 0;

  router->choice_values[choice_index] = *value;
  router->choice_committed[choice_index] = 1u;
  *accepted = 1;
  return 1;
}

static int rr_has_uncommitted_deferred_choice(
    const Kotonoha_KtrfRouter *router, int *blocked,
    Kotonoha_KtrfError *error) {
  uint32_t i;
  *blocked = 0;
  for (i = 0u; i < router->choice_count; ++i) {
    Kotonoha_KtrfChoice choice;
    int is_deferred;
    if (!Kotonoha_KtrfGetChoice(router->document, i, &choice, error))
      return 0;
    if (choice.node_index != router->current_node_index)
      continue;
    is_deferred =
        rr_string_equals(router->document, choice.routing_policy_str,
                         "ktrf:deferred", error);
    if (is_deferred < 0)
      return 0;
    if (is_deferred && router->choice_committed[i] == 0u) {
      *blocked = 1;
      return 1;
    }
  }
  return 1;
}

static int rr_transition_matches_trigger(
    const Kotonoha_KtrfRouter *router, uint32_t transition_index,
    const Kotonoha_KtrfTransition *transition, const char *trigger,
    int *matches, Kotonoha_KtrfError *error) {
  uint32_t i;
  const char *requested = trigger != NULL ? trigger : "ktrf:next";
  *matches = 0;

  if ((transition->flags & KOTONOHA_KTRF_TRAN_HAS_TRIGGERS) == 0u) {
    *matches = strcmp(requested, "ktrf:next") == 0;
    return 1;
  }

  for (i = 0u; i < transition->trigger_count; ++i) {
    uint32_t trigger_string_index;
    int equal;
    if (!Kotonoha_KtrfGetTransitionTrigger(
            router->document, transition_index, i, &trigger_string_index,
            error))
      return 0;
    equal = rr_string_equals(router->document, trigger_string_index,
                             requested, error);
    if (equal < 0)
      return 0;
    if (equal) {
      *matches = 1;
      return 1;
    }
  }
  return 1;
}

void Kotonoha_KtrfRouterInit(Kotonoha_KtrfRouter *router) {
  if (router == NULL)
    return;
  memset(router, 0, sizeof(*router));
  Kotonoha_KtrfRuntimeInit(&router->runtime);
  router->current_node_index = KOTONOHA_KTRF_NULL_INDEX;
}

void Kotonoha_KtrfRouterClean(Kotonoha_KtrfRouter *router) {
  if (router == NULL)
    return;
  free(router->choice_committed);
  free(router->choice_values);
  Kotonoha_KtrfRuntimeClean(&router->runtime);
  memset(router, 0, sizeof(*router));
  router->current_node_index = KOTONOHA_KTRF_NULL_INDEX;
}

int Kotonoha_KtrfRouterBind(Kotonoha_KtrfRouter *router,
                            const Kotonoha_KtrfDocument *document,
                            const Kotonoha_KtrfRuntimeCallbacks *callbacks,
                            Kotonoha_KtrfError *error) {
  uint32_t choice_count;
  rr_clear_error(error);
  if (router == NULL || document == NULL)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "router/document argument is null");

  Kotonoha_KtrfRouterClean(router);
  Kotonoha_KtrfRuntimeInit(&router->runtime);

  if (!Kotonoha_KtrfRuntimeBind(&router->runtime, document, callbacks, error)) {
    Kotonoha_KtrfRouterClean(router);
    return 0;
  }

  router->document = document;
  router->current_node_index = KOTONOHA_KTRF_NULL_INDEX;
  choice_count = rr_count(document, "CHOI");
  router->choice_count = choice_count;

  if (choice_count != 0u) {
    router->choice_committed = (uint8_t *)calloc(choice_count, 1u);
    router->choice_values = (Kotonoha_KtrfRuntimeValue *)calloc(
        choice_count, sizeof(*router->choice_values));
    if (router->choice_committed == NULL || router->choice_values == NULL) {
      Kotonoha_KtrfRouterClean(router);
      return rr_fail(error, KOTONOHA_KTRF_ERROR_MEMORY,
                     "out of memory allocating router choice state");
    }
  }

  return 1;
}

int Kotonoha_KtrfRouterResetEntry(Kotonoha_KtrfRouter *router,
                                  uint32_t entry_point_index,
                                  Kotonoha_KtrfError *error) {
  Kotonoha_KtrfEntryPoint entry;
  rr_clear_error(error);
  if (router == NULL || router->document == NULL)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "router is not bound");
  if (!Kotonoha_KtrfGetEntryPoint(router->document, entry_point_index, &entry,
                                  error))
    return 0;
  if (!Kotonoha_KtrfRuntimeResetDefaults(&router->runtime, error))
    return 0;

  router->current_node_index = entry.node_index;
  router->terminal = 0;
  rr_clear_choice_commits(router);
  return 1;
}

int Kotonoha_KtrfRouterActivateNode(Kotonoha_KtrfRouter *router,
                                    uint32_t node_index,
                                    Kotonoha_KtrfError *error) {
  Kotonoha_KtrfNode node;
  rr_clear_error(error);
  if (router == NULL || router->document == NULL)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "router is not bound");
  if (!Kotonoha_KtrfGetNode(router->document, node_index, &node, error))
    return 0;
  (void)node;
  router->current_node_index = node_index;
  router->terminal = 0;
  rr_clear_choice_commits(router);
  return 1;
}

int Kotonoha_KtrfRouterCurrentChoiceCount(const Kotonoha_KtrfRouter *router,
                                          uint32_t *count,
                                          Kotonoha_KtrfError *error) {
  uint32_t i;
  uint32_t found = 0u;
  rr_clear_error(error);
  if (router == NULL || router->document == NULL || count == NULL)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "router/count argument is null");
  if (router->current_node_index == KOTONOHA_KTRF_NULL_INDEX)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                   "router has no active node");

  for (i = 0u; i < router->choice_count; ++i) {
    Kotonoha_KtrfChoice choice;
    if (!Kotonoha_KtrfGetChoice(router->document, i, &choice, error))
      return 0;
    if (choice.node_index == router->current_node_index)
      ++found;
  }
  *count = found;
  return 1;
}

int Kotonoha_KtrfRouterCurrentChoiceAt(const Kotonoha_KtrfRouter *router,
                                       uint32_t ordinal,
                                       uint32_t *choice_index,
                                       Kotonoha_KtrfError *error) {
  uint32_t i;
  uint32_t seen = 0u;
  rr_clear_error(error);
  if (router == NULL || router->document == NULL || choice_index == NULL)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "router/choice_index argument is null");
  if (router->current_node_index == KOTONOHA_KTRF_NULL_INDEX)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                   "router has no active node");

  for (i = 0u; i < router->choice_count; ++i) {
    Kotonoha_KtrfChoice choice;
    if (!Kotonoha_KtrfGetChoice(router->document, i, &choice, error))
      return 0;
    if (choice.node_index != router->current_node_index)
      continue;
    if (seen == ordinal) {
      *choice_index = i;
      return 1;
    }
    ++seen;
  }

  return rr_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                 "current-node choice ordinal %u is out of range", ordinal);
}

int Kotonoha_KtrfRouterCommitChoiceValue(
    Kotonoha_KtrfRouter *router, uint32_t choice_index,
    const Kotonoha_KtrfRuntimeValue *value, int *accepted,
    Kotonoha_KtrfError *error) {
  Kotonoha_KtrfChoice choice;
  uint32_t i;
  rr_clear_error(error);
  if (accepted != NULL)
    *accepted = 0;
  if (router == NULL || router->document == NULL || value == NULL ||
      accepted == NULL)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "choice commit argument is null");
  if (router->terminal)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                   "cannot commit choice after terminal transition");
  if (choice_index >= router->choice_count)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                   "choice index %u is out of range", choice_index);
  if (!rr_choice_belongs_to_current(router, choice_index, &choice, error))
    return 0;

  if (router->choice_committed[choice_index] != 0u) {
    *accepted = rr_value_equal(&router->choice_values[choice_index], value);
    return 1;
  }

  for (i = 0u; i < choice.option_count; ++i) {
    Kotonoha_KtrfChoiceOption option;
    Kotonoha_KtrfRuntimeValue option_value;
    if (!Kotonoha_KtrfGetChoiceOption(router->document, choice_index, i,
                                      &option, error))
      return 0;
    if (!rr_scalar_to_value(router->document, &option.value, &option_value,
                            error))
      return 0;
    if (rr_value_equal(&option_value, value))
      return rr_commit_selected(router, choice_index, &choice, value, 0, i,
                                accepted, error);
  }

  {
    Kotonoha_KtrfChoiceTimeout timeout;
    Kotonoha_KtrfRuntimeValue timeout_value;
    int has_timeout = 0;
    if (!Kotonoha_KtrfGetChoiceTimeout(router->document, choice_index,
                                       &has_timeout, &timeout, error))
      return 0;
    if (has_timeout) {
      if (!rr_scalar_to_value(router->document, &timeout.value, &timeout_value,
                              error))
        return 0;
      if (rr_value_equal(&timeout_value, value))
        return rr_commit_selected(router, choice_index, &choice, value, 1, 0u,
                                  accepted, error);
    }
  }

  *accepted = 0;
  return 1;
}

int Kotonoha_KtrfRouterCommitChoiceOption(
    Kotonoha_KtrfRouter *router, uint32_t choice_index,
    uint32_t local_option_index, int *accepted, Kotonoha_KtrfError *error) {
  Kotonoha_KtrfChoiceOption option;
  Kotonoha_KtrfRuntimeValue value;
  rr_clear_error(error);
  if (router == NULL || router->document == NULL)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "router is not bound");
  if (!Kotonoha_KtrfGetChoiceOption(router->document, choice_index,
                                    local_option_index, &option, error))
    return 0;
  if (!rr_scalar_to_value(router->document, &option.value, &value, error))
    return 0;
  return Kotonoha_KtrfRouterCommitChoiceValue(
      router, choice_index, &value, accepted, error);
}

int Kotonoha_KtrfRouterCommitChoiceTimeout(
    Kotonoha_KtrfRouter *router, uint32_t choice_index, int *accepted,
    Kotonoha_KtrfError *error) {
  Kotonoha_KtrfChoiceTimeout timeout;
  Kotonoha_KtrfRuntimeValue value;
  int has_timeout = 0;
  rr_clear_error(error);
  if (router == NULL || router->document == NULL || accepted == NULL)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "timeout commit argument is null");
  *accepted = 0;
  if (!Kotonoha_KtrfGetChoiceTimeout(router->document, choice_index,
                                     &has_timeout, &timeout, error))
    return 0;
  if (!has_timeout)
    return 1;
  if (!rr_scalar_to_value(router->document, &timeout.value, &value, error))
    return 0;
  return Kotonoha_KtrfRouterCommitChoiceValue(
      router, choice_index, &value, accepted, error);
}

int Kotonoha_KtrfRouterTrigger(Kotonoha_KtrfRouter *router,
                               const char *trigger,
                               Kotonoha_KtrfRouteResult *out,
                               Kotonoha_KtrfError *error) {
  uint32_t transition_count;
  uint32_t i;
  uint32_t selected_index = KOTONOHA_KTRF_NULL_INDEX;
  Kotonoha_KtrfTransition selected;
  int blocked = 0;

  rr_clear_error(error);
  if (out != NULL) {
    memset(out, 0, sizeof(*out));
    out->transition_index = KOTONOHA_KTRF_NULL_INDEX;
    out->source_node_index = KOTONOHA_KTRF_NULL_INDEX;
    out->destination_node_index = KOTONOHA_KTRF_NULL_INDEX;
  }

  if (router == NULL || router->document == NULL || out == NULL)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "router/route-result argument is null");
  if (router->current_node_index == KOTONOHA_KTRF_NULL_INDEX)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                   "router has no active node");
  if (router->terminal)
    return rr_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                   "cannot route after terminal transition");

  if (!rr_has_uncommitted_deferred_choice(router, &blocked, error))
    return 0;
  if (blocked) {
    out->status = KOTONOHA_KTRF_ROUTE_BLOCKED_CHOICE;
    out->source_node_index = router->current_node_index;
    return 1;
  }

  transition_count = rr_count(router->document, "TRAN");
  memset(&selected, 0, sizeof(selected));

  for (i = 0u; i < transition_count; ++i) {
    Kotonoha_KtrfTransition candidate;
    int trigger_matches = 0;
    int predicate_matches = 1;

    if (!Kotonoha_KtrfGetTransition(router->document, i, &candidate, error))
      return 0;
    if (candidate.source_node_index != router->current_node_index)
      continue;

    if (!rr_transition_matches_trigger(router, i, &candidate, trigger,
                                       &trigger_matches, error))
      return 0;
    if (!trigger_matches)
      continue;

    if (candidate.predicate_expression_index != KOTONOHA_KTRF_NULL_INDEX) {
      Kotonoha_KtrfRuntimeValue predicate;
      if (!Kotonoha_KtrfRuntimeEvalExpression(
              &router->runtime, candidate.predicate_expression_index,
              &predicate, error))
        return 0;
      predicate_matches = rr_truthy(&predicate);
    }
    if (!predicate_matches)
      continue;

    if (selected_index == KOTONOHA_KTRF_NULL_INDEX ||
        candidate.priority < selected.priority ||
        (candidate.priority == selected.priority && i < selected_index)) {
      selected_index = i;
      selected = candidate;
    }
  }

  if (selected_index == KOTONOHA_KTRF_NULL_INDEX) {
    out->status = KOTONOHA_KTRF_ROUTE_NONE;
    out->source_node_index = router->current_node_index;
    return 1;
  }

  out->transition_index = selected_index;
  out->source_node_index = router->current_node_index;
  out->priority = selected.priority;

  for (i = 0u; i < selected.effect_count; ++i) {
    uint32_t effect_index;
    if (!Kotonoha_KtrfGetTransitionEffect(router->document, selected_index, i,
                                          &effect_index, error))
      return 0;
    if (!Kotonoha_KtrfRuntimeApplyEffect(&router->runtime, effect_index, error))
      return 0;
  }

  if ((selected.flags & KOTONOHA_KTRF_TRAN_TERMINAL) != 0u) {
    router->terminal = 1;
    out->status = KOTONOHA_KTRF_ROUTE_TERMINAL;
    out->destination_node_index = KOTONOHA_KTRF_NULL_INDEX;
    return 1;
  }

  router->current_node_index = selected.destination_node_index;
  rr_clear_choice_commits(router);
  out->status = KOTONOHA_KTRF_ROUTE_ADVANCED;
  out->destination_node_index = selected.destination_node_index;
  return 1;
}
