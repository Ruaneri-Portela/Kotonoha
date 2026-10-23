#include "Kotonoha/routing/KtrfRouter.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

static int fail(const char *message) {
  std::fprintf(stderr, "FAIL: %s\n", message);
  return 1;
}

static uint32_t count_section(const Kotonoha_KtrfDocument &doc,
                              const char type[4]) {
  const Kotonoha_KtrfSection *section = Kotonoha_KtrfFindSection(&doc, type);
  return section != nullptr ? section->item_count : 0u;
}

static bool string_equals(const Kotonoha_KtrfDocument &doc, uint32_t index,
                          const char *expected) {
  const char *data = nullptr;
  size_t size = 0;
  Kotonoha_KtrfError error{};
  if (!Kotonoha_KtrfGetString(&doc, index, &data, &size, &error))
    return false;
  const size_t expected_size = std::strlen(expected);
  return size == expected_size &&
         (size == 0u || std::memcmp(data, expected, size) == 0);
}

static bool truthy(const Kotonoha_KtrfRuntimeValue &value) {
  switch (value.kind) {
  case KOTONOHA_KTRF_RUNTIME_NULL:
    return false;
  case KOTONOHA_KTRF_RUNTIME_BOOL:
    return value.as.boolean_value != 0;
  case KOTONOHA_KTRF_RUNTIME_INT64:
    return value.as.int64_value != 0;
  case KOTONOHA_KTRF_RUNTIME_UINT64:
    return value.as.uint64_value != 0u;
  case KOTONOHA_KTRF_RUNTIME_FLOAT64:
    return value.as.float64_value != 0.0;
  case KOTONOHA_KTRF_RUNTIME_STRING:
    return value.as.string_value.size != 0u;
  case KOTONOHA_KTRF_RUNTIME_BYTES:
    return value.as.bytes_value.size != 0u;
  case KOTONOHA_KTRF_RUNTIME_ENTITY:
    return true;
  default:
    return false;
  }
}

static bool transition_has_next(const Kotonoha_KtrfDocument &doc,
                                uint32_t transition_index,
                                const Kotonoha_KtrfTransition &transition) {
  if ((transition.flags & KOTONOHA_KTRF_TRAN_HAS_TRIGGERS) == 0u)
    return true;

  Kotonoha_KtrfError error{};
  for (uint32_t i = 0; i < transition.trigger_count; ++i) {
    uint32_t string_index = 0;
    if (!Kotonoha_KtrfGetTransitionTrigger(
            &doc, transition_index, i, &string_index, &error))
      return false;
    if (string_equals(doc, string_index, "ktrf:next"))
      return true;
  }
  return false;
}

struct CallbackStats {
  uint64_t endings = 0;
  uint64_t hooks = 0;
};

static int on_ending(Kotonoha_KtrfRuntime *, uint32_t, void *userdata,
                     Kotonoha_KtrfError *) {
  auto *stats = static_cast<CallbackStats *>(userdata);
  ++stats->endings;
  return 1;
}

static int on_hook(Kotonoha_KtrfRuntime *, uint32_t,
                   const Kotonoha_KtrfRuntimeValue *, uint32_t, void *userdata,
                   Kotonoha_KtrfError *) {
  auto *stats = static_cast<CallbackStats *>(userdata);
  ++stats->hooks;
  return 1;
}

int main(int argc, char **argv) {
  if (argc != 2)
    return fail("usage: KtrfRoutingRuntimeGate2Test <school-days-hq.ktnroute>");

  Kotonoha_KtrfDocument doc{};
  Kotonoha_KtrfError error{};
  Kotonoha_KtrfInit(&doc);
  if (!Kotonoha_KtrfLoadFile(argv[1], &doc, &error)) {
    std::fprintf(stderr, "FAIL: load: %s\n", error.message);
    return 1;
  }
  if (!Kotonoha_KtrfValidateTypedCore(&doc, &error)) {
    std::fprintf(stderr, "FAIL: typed core: %s\n", error.message);
    Kotonoha_KtrfClean(&doc);
    return 1;
  }

  if (count_section(doc, "CHOI") != 287u ||
      count_section(doc, "NODE") != 1857u ||
      count_section(doc, "TRAN") != 2458u) {
    Kotonoha_KtrfClean(&doc);
    return fail("frozen School Days routing counts changed");
  }

  CallbackStats stats{};
  Kotonoha_KtrfRuntimeCallbacks callbacks{};
  callbacks.register_ending = on_ending;
  callbacks.call_hook = on_hook;
  callbacks.userdata = &stats;

  Kotonoha_KtrfRouter router{};
  Kotonoha_KtrfRouterInit(&router);
  if (!Kotonoha_KtrfRouterBind(&router, &doc, &callbacks, &error)) {
    std::fprintf(stderr, "FAIL: router bind: %s\n", error.message);
    Kotonoha_KtrfClean(&doc);
    return 1;
  }
  if (!Kotonoha_KtrfRouterResetEntry(&router, 0u, &error)) {
    std::fprintf(stderr, "FAIL: reset entry: %s\n", error.message);
    Kotonoha_KtrfRouterClean(&router);
    Kotonoha_KtrfClean(&doc);
    return 1;
  }

  Kotonoha_KtrfEntryPoint entry{};
  if (!Kotonoha_KtrfGetEntryPoint(&doc, 0u, &entry, &error) ||
      router.current_node_index != entry.node_index) {
    Kotonoha_KtrfRouterClean(&router);
    Kotonoha_KtrfClean(&doc);
    return fail("router did not activate entry-point node");
  }

  uint64_t option_commits = 0;
  uint64_t timeout_commits = 0;
  const uint32_t choice_count = count_section(doc, "CHOI");

  for (uint32_t choice_index = 0; choice_index < choice_count; ++choice_index) {
    Kotonoha_KtrfChoice choice{};
    if (!Kotonoha_KtrfGetChoice(&doc, choice_index, &choice, &error)) {
      std::fprintf(stderr, "FAIL: choice decode: %s\n", error.message);
      return 1;
    }
    if (!string_equals(doc, choice.routing_policy_str, "ktrf:deferred"))
      return fail("School Days choice is not ktrf:deferred");

    if (!Kotonoha_KtrfRuntimeResetDefaults(&router.runtime, &error) ||
        !Kotonoha_KtrfRouterActivateNode(&router, choice.node_index, &error)) {
      std::fprintf(stderr, "FAIL: choice setup: %s\n", error.message);
      return 1;
    }

    Kotonoha_KtrfRouteResult blocked{};
    if (!Kotonoha_KtrfRouterTrigger(&router, "ktrf:next", &blocked, &error)) {
      std::fprintf(stderr, "FAIL: blocked-choice trigger: %s\n", error.message);
      return 1;
    }
    if (blocked.status != KOTONOHA_KTRF_ROUTE_BLOCKED_CHOICE)
      return fail("uncommitted deferred choice did not block routing");

    for (uint32_t local = 0; local < choice.option_count; ++local) {
      int accepted = 0;
      if (!Kotonoha_KtrfRuntimeResetDefaults(&router.runtime, &error) ||
          !Kotonoha_KtrfRouterActivateNode(&router, choice.node_index, &error) ||
          !Kotonoha_KtrfRouterCommitChoiceOption(
              &router, choice_index, local, &accepted, &error)) {
        std::fprintf(stderr, "FAIL: option commit: %s\n", error.message);
        return 1;
      }
      if (!accepted)
        return fail("valid choice option was rejected");

      const uint32_t before = router.current_node_index;
      int accepted_again = 0;
      if (!Kotonoha_KtrfRouterCommitChoiceOption(
              &router, choice_index, local, &accepted_again, &error) ||
          !accepted_again)
        return fail("same committed choice result is not idempotent");
      if (router.current_node_index != before)
        return fail("deferred choice commit routed immediately");
      ++option_commits;
    }

    Kotonoha_KtrfChoiceTimeout timeout{};
    int has_timeout = 0;
    if (!Kotonoha_KtrfGetChoiceTimeout(&doc, choice_index, &has_timeout,
                                       &timeout, &error))
      return fail("failed to decode choice timeout");
    if (has_timeout) {
      int accepted = 0;
      if (!Kotonoha_KtrfRuntimeResetDefaults(&router.runtime, &error) ||
          !Kotonoha_KtrfRouterActivateNode(&router, choice.node_index, &error) ||
          !Kotonoha_KtrfRouterCommitChoiceTimeout(
              &router, choice_index, &accepted, &error)) {
        std::fprintf(stderr, "FAIL: timeout commit: %s\n", error.message);
        return 1;
      }
      if (!accepted)
        return fail("valid choice timeout was rejected");
      ++timeout_commits;
    }
  }

  const uint32_t node_count = count_section(doc, "NODE");
  const uint32_t transition_count = count_section(doc, "TRAN");
  uint64_t selected_transitions = 0;
  uint64_t no_match_nodes = 0;

  for (uint32_t node_index = 0; node_index < node_count; ++node_index) {
    if (!Kotonoha_KtrfRuntimeResetDefaults(&router.runtime, &error) ||
        !Kotonoha_KtrfRouterActivateNode(&router, node_index, &error)) {
      std::fprintf(stderr, "FAIL: node activation: %s\n", error.message);
      return 1;
    }

    uint32_t current_choice_count = 0;
    if (!Kotonoha_KtrfRouterCurrentChoiceCount(
            &router, &current_choice_count, &error))
      return fail("failed to count current-node choices");

    for (uint32_t ordinal = 0; ordinal < current_choice_count; ++ordinal) {
      uint32_t choice_index = 0;
      int accepted = 0;
      if (!Kotonoha_KtrfRouterCurrentChoiceAt(
              &router, ordinal, &choice_index, &error) ||
          !Kotonoha_KtrfRouterCommitChoiceOption(
              &router, choice_index, 0u, &accepted, &error)) {
        std::fprintf(stderr, "FAIL: node choice commit: %s\n", error.message);
        return 1;
      }
      if (!accepted)
        return fail("first option at current node was rejected");
    }

    uint32_t expected_index = KOTONOHA_KTRF_NULL_INDEX;
    uint32_t expected_priority = UINT32_MAX;
    bool expected_terminal = false;
    uint32_t expected_destination = KOTONOHA_KTRF_NULL_INDEX;

    for (uint32_t transition_index = 0; transition_index < transition_count;
         ++transition_index) {
      Kotonoha_KtrfTransition transition{};
      if (!Kotonoha_KtrfGetTransition(
              &doc, transition_index, &transition, &error))
        return fail("failed to decode transition during selection audit");
      if (transition.source_node_index != node_index ||
          !transition_has_next(doc, transition_index, transition))
        continue;

      bool matches = true;
      if (transition.predicate_expression_index != KOTONOHA_KTRF_NULL_INDEX) {
        Kotonoha_KtrfRuntimeValue predicate{};
        if (!Kotonoha_KtrfRuntimeEvalExpression(
                &router.runtime, transition.predicate_expression_index,
                &predicate, &error)) {
          std::fprintf(stderr, "FAIL: predicate evaluation: %s\n",
                       error.message);
          return 1;
        }
        matches = truthy(predicate);
      }
      if (!matches)
        continue;

      if (expected_index == KOTONOHA_KTRF_NULL_INDEX ||
          transition.priority < expected_priority ||
          (transition.priority == expected_priority &&
           transition_index < expected_index)) {
        expected_index = transition_index;
        expected_priority = transition.priority;
        expected_terminal =
            (transition.flags & KOTONOHA_KTRF_TRAN_TERMINAL) != 0u;
        expected_destination = transition.destination_node_index;
      }
    }

    Kotonoha_KtrfRouteResult result{};
    if (!Kotonoha_KtrfRouterTrigger(&router, "ktrf:next", &result, &error)) {
      std::fprintf(stderr, "FAIL: router trigger: %s\n", error.message);
      return 1;
    }

    if (expected_index == KOTONOHA_KTRF_NULL_INDEX) {
      if (result.status != KOTONOHA_KTRF_ROUTE_NONE)
        return fail("router selected a transition where none matched");
      ++no_match_nodes;
      continue;
    }

    if (result.transition_index != expected_index ||
        result.priority != expected_priority)
      return fail("router did not choose first matching explicit priority");

    if (expected_terminal) {
      if (result.status != KOTONOHA_KTRF_ROUTE_TERMINAL ||
          !router.terminal ||
          result.destination_node_index != KOTONOHA_KTRF_NULL_INDEX)
        return fail("terminal transition result/state mismatch");
    } else {
      if (result.status != KOTONOHA_KTRF_ROUTE_ADVANCED ||
          router.terminal ||
          result.destination_node_index != expected_destination ||
          router.current_node_index != expected_destination)
        return fail("nonterminal transition result/state mismatch");
    }
    ++selected_transitions;
  }

  std::printf("KTRF ROUTING RUNTIME GATE 2 PASS\n");
  std::printf("choices=%u\n", choice_count);
  std::printf("option_commits=%llu\n",
              static_cast<unsigned long long>(option_commits));
  std::printf("timeout_commits=%llu\n",
              static_cast<unsigned long long>(timeout_commits));
  std::printf("nodes=%u\n", node_count);
  std::printf("transitions=%u\n", transition_count);
  std::printf("default_state_selected=%llu\n",
              static_cast<unsigned long long>(selected_transitions));
  std::printf("default_state_no_match=%llu\n",
              static_cast<unsigned long long>(no_match_nodes));
  std::printf("choice_deferred_semantics=PASS\n");
  std::printf("transition_priority_selection=PASS\n");
  std::printf("ordered_transition_effects=PASS\n");

  Kotonoha_KtrfRouterClean(&router);
  Kotonoha_KtrfClean(&doc);
  return 0;
}
