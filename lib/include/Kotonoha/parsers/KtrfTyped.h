#pragma once

#include "Kotonoha/parsers/Ktrf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KOTONOHA_KTRF_FEATURE_REQUIRED UINT32_C(0x00000001)
#define KOTONOHA_KTRF_FEATURE_OPTIONAL UINT32_C(0x00000002)

#define KOTONOHA_KTRF_RSRC_REQUIRED UINT32_C(0x00000001)
#define KOTONOHA_KTRF_RSRC_HAS_REQUIRED UINT32_C(0x00000002)

#define KOTONOHA_KTRF_TRAN_TERMINAL UINT32_C(0x00000001)
#define KOTONOHA_KTRF_TRAN_HAS_TRIGGERS UINT32_C(0x00000002)

typedef enum Kotonoha_KtrfVariableDefaultKind {
  KOTONOHA_KTRF_VAR_BOOL = 1,
  KOTONOHA_KTRF_VAR_INT32 = 2,
  KOTONOHA_KTRF_VAR_UINT32 = 3,
  KOTONOHA_KTRF_VAR_FLOAT32 = 4,
  KOTONOHA_KTRF_VAR_FLOAT64 = 5,
  KOTONOHA_KTRF_VAR_STRING = 6,
  KOTONOHA_KTRF_VAR_BYTES = 7
} Kotonoha_KtrfVariableDefaultKind;

typedef enum Kotonoha_KtrfScalarKind {
  KOTONOHA_KTRF_SCALAR_ABSENT = 0,
  KOTONOHA_KTRF_SCALAR_NULL = 1,
  KOTONOHA_KTRF_SCALAR_BOOL = 2,
  KOTONOHA_KTRF_SCALAR_INT64 = 3,
  KOTONOHA_KTRF_SCALAR_FLOAT64 = 4,
  KOTONOHA_KTRF_SCALAR_STRING = 5
} Kotonoha_KtrfScalarKind;

typedef enum Kotonoha_KtrfValueRefKind {
  KOTONOHA_KTRF_VALUE_LITERAL = 1,
  KOTONOHA_KTRF_VALUE_VARIABLE = 2,
  KOTONOHA_KTRF_VALUE_EXPRESSION = 3,
  KOTONOHA_KTRF_VALUE_ENTITY = 4
} Kotonoha_KtrfValueRefKind;

typedef enum Kotonoha_KtrfEntityKind {
  KOTONOHA_KTRF_ENTITY_NONE = 0,
  KOTONOHA_KTRF_ENTITY_RESOURCE = 1,
  KOTONOHA_KTRF_ENTITY_HOOK = 2,
  KOTONOHA_KTRF_ENTITY_ENDING = 3
} Kotonoha_KtrfEntityKind;

typedef enum Kotonoha_KtrfEffectShape {
  KOTONOHA_KTRF_EFFECT_SET = 1,
  KOTONOHA_KTRF_EFFECT_ADD = 2,
  KOTONOHA_KTRF_EFFECT_COPY = 3,
  KOTONOHA_KTRF_EFFECT_REGISTER_ENDING = 4,
  KOTONOHA_KTRF_EFFECT_CALL_HOOK = 5
} Kotonoha_KtrfEffectShape;

typedef struct Kotonoha_KtrfMeta {
  uint32_t format_str;
  uint32_t ir_version_str;
  uint32_t document_id_str;
  uint32_t profile_id_str;
  uint32_t profile_version_str;
} Kotonoha_KtrfMeta;

typedef struct Kotonoha_KtrfNamespace {
  uint32_t prefix_str;
  uint32_t uri_str;
} Kotonoha_KtrfNamespace;

typedef struct Kotonoha_KtrfFeature {
  uint32_t id_str;
  uint32_t version_str;
  uint32_t flags;
} Kotonoha_KtrfFeature;

typedef struct Kotonoha_KtrfEntryPoint {
  uint32_t id_str;
  uint32_t node_index;
  uint32_t trigger_str;
} Kotonoha_KtrfEntryPoint;

typedef struct Kotonoha_KtrfVariableDefault {
  uint8_t kind;
  union {
    int boolean_value;
    int32_t int32_value;
    uint32_t uint32_value;
    float float32_value;
    double float64_value;
    uint32_t string_index;
    struct {
      const uint8_t *data;
      size_t size;
    } bytes_value;
  } as;
} Kotonoha_KtrfVariableDefault;

typedef struct Kotonoha_KtrfVariable {
  uint32_t id_str;
  uint32_t type_str;
  uint32_t scope_str;
  Kotonoha_KtrfVariableDefault default_value;
} Kotonoha_KtrfVariable;

typedef struct Kotonoha_KtrfResource {
  uint32_t id_str;
  uint32_t scheme_str;
  uint32_t value_str;
  uint32_t media_type_str;
  uint32_t flags;
} Kotonoha_KtrfResource;

typedef struct Kotonoha_KtrfHook {
  uint32_t id_str;
  uint32_t symbol_str;
  uint32_t contract_str;
} Kotonoha_KtrfHook;

typedef struct Kotonoha_KtrfScalar {
  uint8_t kind;
  union {
    int boolean_value;
    int64_t int64_value;
    double float64_value;
    uint32_t string_index;
  } as;
} Kotonoha_KtrfScalar;

typedef struct Kotonoha_KtrfEnding {
  uint32_t id_str;
  uint32_t label_str;
  int has_code;
  Kotonoha_KtrfScalar code;
} Kotonoha_KtrfEnding;

typedef struct Kotonoha_KtrfValueRef {
  uint8_t kind;
  uint8_t entity_kind;
  uint32_t index;
  Kotonoha_KtrfScalar literal;
} Kotonoha_KtrfValueRef;

typedef struct Kotonoha_KtrfExpression {
  uint32_t id_str;
  uint32_t op_str;
  uint32_t result_type_str;
  uint32_t arg_start;
  uint32_t arg_count;
} Kotonoha_KtrfExpression;

typedef struct Kotonoha_KtrfEffect {
  uint32_t id_str;
  uint32_t op_str;
  uint8_t shape;
  uint32_t payload_a;
  uint32_t payload_b;
  uint32_t value_start;
  uint32_t value_count;
} Kotonoha_KtrfEffect;

typedef struct Kotonoha_KtrfChoice {
  uint32_t id_str;
  uint32_t node_index;
  uint32_t result_variable_index;
  uint32_t routing_policy_str;
  uint32_t option_start;
  uint32_t option_count;
  uint32_t timeout_index;
} Kotonoha_KtrfChoice;

typedef struct Kotonoha_KtrfChoiceOption {
  uint32_t id_str;
  uint32_t label_str;
  Kotonoha_KtrfScalar value;
  uint32_t effect_start;
  uint32_t effect_count;
} Kotonoha_KtrfChoiceOption;

typedef struct Kotonoha_KtrfChoiceTimeout {
  Kotonoha_KtrfScalar value;
  uint32_t effect_start;
  uint32_t effect_count;
} Kotonoha_KtrfChoiceTimeout;

typedef struct Kotonoha_KtrfNode {
  uint32_t id_str;
  uint32_t kind_str;
  uint32_t label_str;
  uint32_t resource_start;
  uint32_t resource_count;
} Kotonoha_KtrfNode;

typedef struct Kotonoha_KtrfTransition {
  uint32_t id_str;
  uint32_t source_node_index;
  uint32_t destination_node_index;
  uint32_t priority;
  uint32_t predicate_expression_index;
  uint32_t effect_start;
  uint32_t effect_count;
  uint32_t trigger_start;
  uint32_t trigger_count;
  uint32_t flags;
} Kotonoha_KtrfTransition;

int Kotonoha_KtrfGetMeta(const Kotonoha_KtrfDocument *document,
                         Kotonoha_KtrfMeta *out, Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetNamespace(const Kotonoha_KtrfDocument *document,
                              uint32_t index, Kotonoha_KtrfNamespace *out,
                              Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetFeature(const Kotonoha_KtrfDocument *document,
                            uint32_t index, Kotonoha_KtrfFeature *out,
                            Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetEntryPoint(const Kotonoha_KtrfDocument *document,
                               uint32_t index, Kotonoha_KtrfEntryPoint *out,
                               Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetVariable(const Kotonoha_KtrfDocument *document,
                             uint32_t index, Kotonoha_KtrfVariable *out,
                             Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetResource(const Kotonoha_KtrfDocument *document,
                             uint32_t index, Kotonoha_KtrfResource *out,
                             Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetHook(const Kotonoha_KtrfDocument *document,
                         uint32_t index, Kotonoha_KtrfHook *out,
                         Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetEnding(const Kotonoha_KtrfDocument *document,
                           uint32_t index, Kotonoha_KtrfEnding *out,
                           Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetExpression(const Kotonoha_KtrfDocument *document,
                               uint32_t index, Kotonoha_KtrfExpression *out,
                               Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetExpressionArg(const Kotonoha_KtrfDocument *document,
                                  uint32_t expression_index,
                                  uint32_t local_arg_index,
                                  Kotonoha_KtrfValueRef *out,
                                  Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetEffect(const Kotonoha_KtrfDocument *document,
                           uint32_t index, Kotonoha_KtrfEffect *out,
                           Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetEffectValue(const Kotonoha_KtrfDocument *document,
                                uint32_t effect_index,
                                uint32_t local_value_index,
                                Kotonoha_KtrfValueRef *out,
                                Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetChoice(const Kotonoha_KtrfDocument *document,
                           uint32_t index, Kotonoha_KtrfChoice *out,
                           Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetChoiceOption(const Kotonoha_KtrfDocument *document,
                                 uint32_t choice_index,
                                 uint32_t local_option_index,
                                 Kotonoha_KtrfChoiceOption *out,
                                 Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetChoiceTimeout(const Kotonoha_KtrfDocument *document,
                                  uint32_t choice_index, int *has_timeout,
                                  Kotonoha_KtrfChoiceTimeout *out,
                                  Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetChoiceOptionEffect(const Kotonoha_KtrfDocument *document,
                                       uint32_t choice_index,
                                       uint32_t local_option_index,
                                       uint32_t local_effect_index,
                                       uint32_t *effect_index,
                                       Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetChoiceTimeoutEffect(const Kotonoha_KtrfDocument *document,
                                        uint32_t choice_index,
                                        uint32_t local_effect_index,
                                        uint32_t *effect_index,
                                        Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetNode(const Kotonoha_KtrfDocument *document,
                         uint32_t index, Kotonoha_KtrfNode *out,
                         Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetNodeResource(const Kotonoha_KtrfDocument *document,
                                 uint32_t node_index,
                                 uint32_t local_resource_index,
                                 uint32_t *resource_index,
                                 Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetTransition(const Kotonoha_KtrfDocument *document,
                               uint32_t index, Kotonoha_KtrfTransition *out,
                               Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetTransitionEffect(const Kotonoha_KtrfDocument *document,
                                     uint32_t transition_index,
                                     uint32_t local_effect_index,
                                     uint32_t *effect_index,
                                     Kotonoha_KtrfError *error);
int Kotonoha_KtrfGetTransitionTrigger(const Kotonoha_KtrfDocument *document,
                                      uint32_t transition_index,
                                      uint32_t local_trigger_index,
                                      uint32_t *trigger_string_index,
                                      Kotonoha_KtrfError *error);

int Kotonoha_KtrfValidateTypedCore(const Kotonoha_KtrfDocument *document,
                                   Kotonoha_KtrfError *error);

#ifdef __cplusplus
}
#endif
