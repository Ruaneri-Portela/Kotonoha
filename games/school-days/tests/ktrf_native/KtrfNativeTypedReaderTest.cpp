#include "Kotonoha/parsers/Ktrf.h"
#include "Kotonoha/parsers/KtrfTyped.h"

#include <cstdint>
#include <cstdio>

static int fail(const char *label, const Kotonoha_KtrfError &error) {
  std::fprintf(stderr, "FAIL: %s: %s\n", label, error.message);
  return 1;
}

static bool expect_count(const Kotonoha_KtrfDocument &doc, const char type[4],
                         uint32_t expected) {
  const Kotonoha_KtrfSection *section = Kotonoha_KtrfFindSection(&doc, type);
  if (section == nullptr || section->item_count != expected) {
    std::fprintf(stderr, "FAIL: %.4s count expected=%u actual=%u\n", type,
                 expected, section ? section->item_count : 0u);
    return false;
  }
  return true;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr,
                 "usage: KtrfNativeTypedReaderTest <school-days-hq.ktnroute>\n");
    return 1;
  }

  Kotonoha_KtrfDocument doc{};
  Kotonoha_KtrfError error{};
  Kotonoha_KtrfInit(&doc);
  if (!Kotonoha_KtrfLoadFile(argv[1], &doc, &error))
    return fail("load", error);
  if (!Kotonoha_KtrfValidateFullCore(&doc, &error)) {
    Kotonoha_KtrfClean(&doc);
    return fail("full-core envelope", error);
  }
  if (!Kotonoha_KtrfValidateTypedCore(&doc, &error)) {
    Kotonoha_KtrfClean(&doc);
    return fail("typed-core", error);
  }

  const struct Expected {
    const char type[5];
    uint32_t count;
  } expected[] = {{"META", 1},     {"NSPC", 1},    {"FEAT", 8},
                  {"ENTR", 1},     {"VARS", 497},  {"RSRC", 1855},
                  {"HOOK", 3},     {"ENDG", 22},   {"EXPR", 1892},
                  {"EFFT", 11356}, {"CHOI", 287},  {"NODE", 1857},
                  {"TRAN", 2458}};
  for (const auto &item : expected) {
    if (!expect_count(doc, item.type, item.count)) {
      Kotonoha_KtrfClean(&doc);
      return 1;
    }
  }

  Kotonoha_KtrfMeta meta{};
  if (!Kotonoha_KtrfGetMeta(&doc, &meta, &error)) {
    Kotonoha_KtrfClean(&doc);
    return fail("META", error);
  }

  uint64_t expr_args = 0;
  uint64_t effect_values = 0;
  uint64_t choice_options = 0;
  uint64_t choice_option_effects = 0;
  uint64_t choice_timeout_effects = 0;
  uint64_t node_resources = 0;
  uint64_t transition_effects = 0;
  uint64_t transition_triggers = 0;
  uint32_t choices_with_timeout = 0;

  for (uint32_t i = 0; i < 497; ++i) {
    Kotonoha_KtrfVariable row{};
    if (!Kotonoha_KtrfGetVariable(&doc, i, &row, &error)) {
      Kotonoha_KtrfClean(&doc);
      return fail("VARS enumeration", error);
    }
  }
  for (uint32_t i = 0; i < 1855; ++i) {
    Kotonoha_KtrfResource row{};
    if (!Kotonoha_KtrfGetResource(&doc, i, &row, &error)) {
      Kotonoha_KtrfClean(&doc);
      return fail("RSRC enumeration", error);
    }
  }
  for (uint32_t i = 0; i < 3; ++i) {
    Kotonoha_KtrfHook row{};
    if (!Kotonoha_KtrfGetHook(&doc, i, &row, &error)) {
      Kotonoha_KtrfClean(&doc);
      return fail("HOOK enumeration", error);
    }
  }
  for (uint32_t i = 0; i < 22; ++i) {
    Kotonoha_KtrfEnding row{};
    if (!Kotonoha_KtrfGetEnding(&doc, i, &row, &error)) {
      Kotonoha_KtrfClean(&doc);
      return fail("ENDG enumeration", error);
    }
  }

  for (uint32_t i = 0; i < 1892; ++i) {
    Kotonoha_KtrfExpression row{};
    if (!Kotonoha_KtrfGetExpression(&doc, i, &row, &error)) {
      Kotonoha_KtrfClean(&doc);
      return fail("EXPR enumeration", error);
    }
    expr_args += row.arg_count;
    for (uint32_t j = 0; j < row.arg_count; ++j) {
      Kotonoha_KtrfValueRef arg{};
      if (!Kotonoha_KtrfGetExpressionArg(&doc, i, j, &arg, &error)) {
        Kotonoha_KtrfClean(&doc);
        return fail("EXPR arg enumeration", error);
      }
    }
  }

  for (uint32_t i = 0; i < 11356; ++i) {
    Kotonoha_KtrfEffect row{};
    if (!Kotonoha_KtrfGetEffect(&doc, i, &row, &error)) {
      Kotonoha_KtrfClean(&doc);
      return fail("EFFT enumeration", error);
    }
    effect_values += row.value_count;
    for (uint32_t j = 0; j < row.value_count; ++j) {
      Kotonoha_KtrfValueRef value{};
      if (!Kotonoha_KtrfGetEffectValue(&doc, i, j, &value, &error)) {
        Kotonoha_KtrfClean(&doc);
        return fail("EFFT value enumeration", error);
      }
    }
  }

  for (uint32_t i = 0; i < 287; ++i) {
    Kotonoha_KtrfChoice row{};
    if (!Kotonoha_KtrfGetChoice(&doc, i, &row, &error)) {
      Kotonoha_KtrfClean(&doc);
      return fail("CHOI enumeration", error);
    }
    choice_options += row.option_count;
    for (uint32_t j = 0; j < row.option_count; ++j) {
      Kotonoha_KtrfChoiceOption option{};
      if (!Kotonoha_KtrfGetChoiceOption(&doc, i, j, &option, &error)) {
        Kotonoha_KtrfClean(&doc);
        return fail("CHOI option enumeration", error);
      }
      choice_option_effects += option.effect_count;
      for (uint32_t k = 0; k < option.effect_count; ++k) {
        uint32_t effect_index = 0;
        if (!Kotonoha_KtrfGetChoiceOptionEffect(&doc, i, j, k, &effect_index,
                                                &error)) {
          Kotonoha_KtrfClean(&doc);
          return fail("CHOI option effect enumeration", error);
        }
      }
    }

    int has_timeout = 0;
    Kotonoha_KtrfChoiceTimeout timeout{};
    if (!Kotonoha_KtrfGetChoiceTimeout(&doc, i, &has_timeout, &timeout,
                                       &error)) {
      Kotonoha_KtrfClean(&doc);
      return fail("CHOI timeout enumeration", error);
    }
    if (has_timeout) {
      ++choices_with_timeout;
      choice_timeout_effects += timeout.effect_count;
      for (uint32_t k = 0; k < timeout.effect_count; ++k) {
        uint32_t effect_index = 0;
        if (!Kotonoha_KtrfGetChoiceTimeoutEffect(&doc, i, k, &effect_index,
                                                 &error)) {
          Kotonoha_KtrfClean(&doc);
          return fail("CHOI timeout effect enumeration", error);
        }
      }
    }
  }

  for (uint32_t i = 0; i < 1857; ++i) {
    Kotonoha_KtrfNode row{};
    if (!Kotonoha_KtrfGetNode(&doc, i, &row, &error)) {
      Kotonoha_KtrfClean(&doc);
      return fail("NODE enumeration", error);
    }
    node_resources += row.resource_count;
    for (uint32_t j = 0; j < row.resource_count; ++j) {
      uint32_t resource_index = 0;
      if (!Kotonoha_KtrfGetNodeResource(&doc, i, j, &resource_index, &error)) {
        Kotonoha_KtrfClean(&doc);
        return fail("NODE resource enumeration", error);
      }
    }
  }

  for (uint32_t i = 0; i < 2458; ++i) {
    Kotonoha_KtrfTransition row{};
    if (!Kotonoha_KtrfGetTransition(&doc, i, &row, &error)) {
      Kotonoha_KtrfClean(&doc);
      return fail("TRAN enumeration", error);
    }
    transition_effects += row.effect_count;
    transition_triggers += row.trigger_count;
    for (uint32_t j = 0; j < row.effect_count; ++j) {
      uint32_t effect_index = 0;
      if (!Kotonoha_KtrfGetTransitionEffect(&doc, i, j, &effect_index,
                                            &error)) {
        Kotonoha_KtrfClean(&doc);
        return fail("TRAN effect enumeration", error);
      }
    }
    for (uint32_t j = 0; j < row.trigger_count; ++j) {
      uint32_t trigger_string_index = 0;
      if (!Kotonoha_KtrfGetTransitionTrigger(&doc, i, j,
                                             &trigger_string_index, &error)) {
        Kotonoha_KtrfClean(&doc);
        return fail("TRAN trigger enumeration", error);
      }
    }
  }

  Kotonoha_KtrfEntryPoint entry{};
  if (!Kotonoha_KtrfGetEntryPoint(&doc, 0, &entry, &error)) {
    Kotonoha_KtrfClean(&doc);
    return fail("ENTR", error);
  }
  if (entry.node_index >= 1857u) {
    Kotonoha_KtrfClean(&doc);
    std::fprintf(stderr, "FAIL: ENTR node index out of frozen NODE range\n");
    return 1;
  }

  Kotonoha_KtrfVariable out_of_range{};
  if (Kotonoha_KtrfGetVariable(&doc, 497u, &out_of_range, &error) ||
      error.code != KOTONOHA_KTRF_ERROR_RANGE) {
    Kotonoha_KtrfClean(&doc);
    std::fprintf(stderr, "FAIL: typed out-of-range access was not rejected\n");
    return 1;
  }

  std::printf("KTRF NATIVE TYPED READER GATE 2 PASS\n");
  std::printf("fixture=%s\n", argv[1]);
  std::printf("typed_sections=META,NSPC,FEAT,ENTR,VARS,RSRC,HOOK,ENDG,EXPR,EFFT,CHOI,NODE,TRAN\n");
  std::printf("expr_args=%llu\n", static_cast<unsigned long long>(expr_args));
  std::printf("effect_values=%llu\n",
              static_cast<unsigned long long>(effect_values));
  std::printf("choice_options=%llu\n",
              static_cast<unsigned long long>(choice_options));
  std::printf("choice_option_effects=%llu\n",
              static_cast<unsigned long long>(choice_option_effects));
  std::printf("choices_with_timeout=%u\n", choices_with_timeout);
  std::printf("choice_timeout_effects=%llu\n",
              static_cast<unsigned long long>(choice_timeout_effects));
  std::printf("node_resource_refs=%llu\n",
              static_cast<unsigned long long>(node_resources));
  std::printf("transition_effect_refs=%llu\n",
              static_cast<unsigned long long>(transition_effects));
  std::printf("transition_trigger_refs=%llu\n",
              static_cast<unsigned long long>(transition_triggers));
  std::printf("cross_reference_validation=PASS\n");
  std::printf("typed_bounds_validation=PASS\n");

  Kotonoha_KtrfClean(&doc);
  return 0;
}
