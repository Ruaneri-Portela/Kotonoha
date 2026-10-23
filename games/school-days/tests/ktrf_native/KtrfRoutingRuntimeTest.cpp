#include "Kotonoha/parsers/Ktrf.h"
#include "Kotonoha/parsers/KtrfTyped.h"
#include "Kotonoha/routing/KtrfRuntime.h"

#include <cstdint>
#include <cstdio>

struct CallbackStats {
  uint32_t endings = 0;
  uint32_t hooks = 0;
  uint32_t hook_args = 0;
};

static int on_ending(Kotonoha_KtrfRuntime *, uint32_t, void *userdata,
                     Kotonoha_KtrfError *) {
  auto *stats = static_cast<CallbackStats *>(userdata);
  ++stats->endings;
  return 1;
}

static int on_hook(Kotonoha_KtrfRuntime *, uint32_t,
                   const Kotonoha_KtrfRuntimeValue *, uint32_t argument_count,
                   void *userdata, Kotonoha_KtrfError *) {
  auto *stats = static_cast<CallbackStats *>(userdata);
  ++stats->hooks;
  stats->hook_args += argument_count;
  return 1;
}

static int fail(const char *stage, const Kotonoha_KtrfError &error) {
  std::fprintf(stderr, "FAIL: %s: %s\n", stage, error.message);
  return 1;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr,
                 "usage: KtrfRoutingRuntimeTest <school-days-hq.ktnroute>\n");
    return 1;
  }

  Kotonoha_KtrfDocument doc{};
  Kotonoha_KtrfError error{};
  Kotonoha_KtrfInit(&doc);
  if (!Kotonoha_KtrfLoadFile(argv[1], &doc, &error))
    return fail("load", error);
  if (!Kotonoha_KtrfValidateTypedCore(&doc, &error)) {
    Kotonoha_KtrfClean(&doc);
    return fail("typed-core", error);
  }

  CallbackStats stats{};
  Kotonoha_KtrfRuntimeCallbacks callbacks{};
  callbacks.register_ending = on_ending;
  callbacks.call_hook = on_hook;
  callbacks.userdata = &stats;

  Kotonoha_KtrfRuntime runtime{};
  Kotonoha_KtrfRuntimeInit(&runtime);
  if (!Kotonoha_KtrfRuntimeBind(&runtime, &doc, &callbacks, &error)) {
    Kotonoha_KtrfClean(&doc);
    return fail("runtime-bind", error);
  }

  const auto *expr_section = Kotonoha_KtrfFindSection(&doc, "EXPR");
  const auto *effect_section = Kotonoha_KtrfFindSection(&doc, "EFFT");
  if (expr_section == nullptr || effect_section == nullptr) {
    Kotonoha_KtrfRuntimeClean(&runtime);
    Kotonoha_KtrfClean(&doc);
    std::fprintf(stderr, "FAIL: missing EXPR/EFFT section\n");
    return 1;
  }

  uint32_t expression_passes = 0;
  for (uint32_t i = 0; i < expr_section->item_count; ++i) {
    Kotonoha_KtrfRuntimeValue value{};
    if (!Kotonoha_KtrfRuntimeEvalExpression(&runtime, i, &value, &error)) {
      std::fprintf(stderr, "FAIL: expression[%u]: %s\n", i, error.message);
      Kotonoha_KtrfRuntimeClean(&runtime);
      Kotonoha_KtrfClean(&doc);
      return 1;
    }
    if (value.kind != KOTONOHA_KTRF_RUNTIME_BOOL) {
      std::fprintf(stderr, "FAIL: expression[%u] did not produce bool\n", i);
      Kotonoha_KtrfRuntimeClean(&runtime);
      Kotonoha_KtrfClean(&doc);
      return 1;
    }
    ++expression_passes;
  }

  uint32_t shape_counts[6]{};
  for (uint32_t i = 0; i < effect_section->item_count; ++i) {
    Kotonoha_KtrfEffect effect{};
    if (!Kotonoha_KtrfRuntimeResetDefaults(&runtime, &error)) {
      Kotonoha_KtrfRuntimeClean(&runtime);
      Kotonoha_KtrfClean(&doc);
      return fail("reset-defaults", error);
    }
    if (!Kotonoha_KtrfGetEffect(&doc, i, &effect, &error)) {
      Kotonoha_KtrfRuntimeClean(&runtime);
      Kotonoha_KtrfClean(&doc);
      return fail("get-effect", error);
    }
    if (effect.shape < 1u || effect.shape > 5u) {
      std::fprintf(stderr, "FAIL: effect[%u] invalid shape %u\n", i,
                   effect.shape);
      Kotonoha_KtrfRuntimeClean(&runtime);
      Kotonoha_KtrfClean(&doc);
      return 1;
    }
    ++shape_counts[effect.shape];
    if (!Kotonoha_KtrfRuntimeApplyEffect(&runtime, i, &error)) {
      std::fprintf(stderr, "FAIL: effect[%u] shape=%u: %s\n", i,
                   effect.shape, error.message);
      Kotonoha_KtrfRuntimeClean(&runtime);
      Kotonoha_KtrfClean(&doc);
      return 1;
    }
  }

  for (uint32_t shape = 1; shape <= 5; ++shape) {
    if (shape_counts[shape] == 0u) {
      std::fprintf(stderr, "FAIL: effect shape %u is absent from fixture\n", shape);
      Kotonoha_KtrfRuntimeClean(&runtime);
      Kotonoha_KtrfClean(&doc);
      return 1;
    }
  }
  if (stats.hooks == 0u || stats.endings == 0u) {
    std::fprintf(stderr, "FAIL: external effect callbacks were not exercised\n");
    Kotonoha_KtrfRuntimeClean(&runtime);
    Kotonoha_KtrfClean(&doc);
    return 1;
  }

  std::printf("KTRF ROUTING RUNTIME GATE 1 PASS\n");
  std::printf("expressions=%u\n", expression_passes);
  std::printf("effects=%u\n", effect_section->item_count);
  std::printf("effect_shapes=set:%u add:%u copy:%u ending:%u hook:%u\n",
              shape_counts[KOTONOHA_KTRF_EFFECT_SET],
              shape_counts[KOTONOHA_KTRF_EFFECT_ADD],
              shape_counts[KOTONOHA_KTRF_EFFECT_COPY],
              shape_counts[KOTONOHA_KTRF_EFFECT_REGISTER_ENDING],
              shape_counts[KOTONOHA_KTRF_EFFECT_CALL_HOOK]);
  std::printf("hook_callbacks=%u hook_args=%u ending_callbacks=%u\n", stats.hooks,
              stats.hook_args, stats.endings);
  std::printf("expression_evaluation=PASS\n");
  std::printf("effect_execution=PASS\n");

  Kotonoha_KtrfRuntimeClean(&runtime);
  Kotonoha_KtrfClean(&doc);
  return 0;
}
