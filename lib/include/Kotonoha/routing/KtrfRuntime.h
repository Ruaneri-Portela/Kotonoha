#pragma once

#include "Kotonoha/parsers/KtrfTyped.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Kotonoha_KtrfRuntimeValueKind {
  KOTONOHA_KTRF_RUNTIME_NULL = 0,
  KOTONOHA_KTRF_RUNTIME_BOOL = 1,
  KOTONOHA_KTRF_RUNTIME_INT64 = 2,
  KOTONOHA_KTRF_RUNTIME_UINT64 = 3,
  KOTONOHA_KTRF_RUNTIME_FLOAT64 = 4,
  KOTONOHA_KTRF_RUNTIME_STRING = 5,
  KOTONOHA_KTRF_RUNTIME_BYTES = 6,
  KOTONOHA_KTRF_RUNTIME_ENTITY = 7
} Kotonoha_KtrfRuntimeValueKind;

typedef struct Kotonoha_KtrfRuntimeValue {
  uint8_t kind;
  uint8_t entity_kind;
  uint16_t reserved;
  union {
    int boolean_value;
    int64_t int64_value;
    uint64_t uint64_value;
    double float64_value;
    struct {
      const char *data;
      size_t size;
    } string_value;
    struct {
      const uint8_t *data;
      size_t size;
    } bytes_value;
    uint32_t entity_index;
  } as;
} Kotonoha_KtrfRuntimeValue;

struct Kotonoha_KtrfRuntime;
typedef struct Kotonoha_KtrfRuntime Kotonoha_KtrfRuntime;

typedef int (*Kotonoha_KtrfRegisterEndingFn)(
    Kotonoha_KtrfRuntime *runtime, uint32_t ending_index, void *userdata,
    Kotonoha_KtrfError *error);

typedef int (*Kotonoha_KtrfCallHookFn)(
    Kotonoha_KtrfRuntime *runtime, uint32_t hook_index,
    const Kotonoha_KtrfRuntimeValue *arguments, uint32_t argument_count,
    void *userdata, Kotonoha_KtrfError *error);

typedef struct Kotonoha_KtrfRuntimeCallbacks {
  Kotonoha_KtrfRegisterEndingFn register_ending;
  Kotonoha_KtrfCallHookFn call_hook;
  void *userdata;
} Kotonoha_KtrfRuntimeCallbacks;

struct Kotonoha_KtrfRuntime {
  const Kotonoha_KtrfDocument *document;
  Kotonoha_KtrfRuntimeValue *variables;
  uint32_t variable_count;
  uint8_t *expression_state;
  uint32_t expression_count;
  uint32_t last_registered_ending;
  int has_registered_ending;
  Kotonoha_KtrfRuntimeCallbacks callbacks;
};

void Kotonoha_KtrfRuntimeInit(Kotonoha_KtrfRuntime *runtime);
void Kotonoha_KtrfRuntimeClean(Kotonoha_KtrfRuntime *runtime);

int Kotonoha_KtrfRuntimeBind(Kotonoha_KtrfRuntime *runtime,
                             const Kotonoha_KtrfDocument *document,
                             const Kotonoha_KtrfRuntimeCallbacks *callbacks,
                             Kotonoha_KtrfError *error);
int Kotonoha_KtrfRuntimeResetDefaults(Kotonoha_KtrfRuntime *runtime,
                                      Kotonoha_KtrfError *error);

int Kotonoha_KtrfRuntimeGetVariable(const Kotonoha_KtrfRuntime *runtime,
                                    uint32_t variable_index,
                                    Kotonoha_KtrfRuntimeValue *out,
                                    Kotonoha_KtrfError *error);
int Kotonoha_KtrfRuntimeSetVariable(Kotonoha_KtrfRuntime *runtime,
                                    uint32_t variable_index,
                                    const Kotonoha_KtrfRuntimeValue *value,
                                    Kotonoha_KtrfError *error);

int Kotonoha_KtrfRuntimeEvalValueRef(Kotonoha_KtrfRuntime *runtime,
                                     const Kotonoha_KtrfValueRef *ref,
                                     Kotonoha_KtrfRuntimeValue *out,
                                     Kotonoha_KtrfError *error);
int Kotonoha_KtrfRuntimeEvalExpression(Kotonoha_KtrfRuntime *runtime,
                                       uint32_t expression_index,
                                       Kotonoha_KtrfRuntimeValue *out,
                                       Kotonoha_KtrfError *error);

int Kotonoha_KtrfRuntimeApplyEffect(Kotonoha_KtrfRuntime *runtime,
                                    uint32_t effect_index,
                                    Kotonoha_KtrfError *error);

#ifdef __cplusplus
}
#endif
