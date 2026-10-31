#include "Kotonoha/routing/KtrfRuntime.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rt_fail(Kotonoha_KtrfError *error, Kotonoha_KtrfErrorCode code,
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

static void rt_clear_error(Kotonoha_KtrfError *error) {
  if (error != NULL) {
    error->code = KOTONOHA_KTRF_OK;
    error->message[0] = '\0';
  }
}

static uint32_t rt_count(const Kotonoha_KtrfDocument *doc, const char type[4]) {
  const Kotonoha_KtrfSection *section = Kotonoha_KtrfFindSection(doc, type);
  return section != NULL ? section->item_count : 0u;
}

static int rt_get_string(const Kotonoha_KtrfDocument *doc, uint32_t index,
                         const char **data, size_t *size,
                         Kotonoha_KtrfError *error) {
  return Kotonoha_KtrfGetString(doc, index, data, size, error);
}

static int rt_string_equals(const Kotonoha_KtrfDocument *doc, uint32_t index,
                            const char *text, Kotonoha_KtrfError *error) {
  const char *raw;
  size_t size;
  size_t expected = strlen(text);
  if (!rt_get_string(doc, index, &raw, &size, error))
    return -1;
  return size == expected && memcmp(raw, text, size) == 0 ? 1 : 0;
}

static Kotonoha_KtrfRuntimeValue rt_null(void) {
  Kotonoha_KtrfRuntimeValue value;
  memset(&value, 0, sizeof(value));
  value.kind = KOTONOHA_KTRF_RUNTIME_NULL;
  return value;
}

static int rt_scalar_to_value(const Kotonoha_KtrfDocument *doc,
                              const Kotonoha_KtrfScalar *scalar,
                              Kotonoha_KtrfRuntimeValue *out,
                              Kotonoha_KtrfError *error) {
  *out = rt_null();
  switch (scalar->kind) {
  case KOTONOHA_KTRF_SCALAR_NULL:
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
    if (!rt_get_string(doc, scalar->as.string_index, &data, &size, error))
      return 0;
    out->kind = KOTONOHA_KTRF_RUNTIME_STRING;
    out->as.string_value.data = data;
    out->as.string_value.size = size;
    return 1;
  }
  default:
    return rt_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                   "runtime cannot convert scalar kind %u", scalar->kind);
  }
}

static int rt_default_to_value(const Kotonoha_KtrfDocument *doc,
                               const Kotonoha_KtrfVariableDefault *source,
                               Kotonoha_KtrfRuntimeValue *out,
                               Kotonoha_KtrfError *error) {
  *out = rt_null();
  switch (source->kind) {
  case KOTONOHA_KTRF_VAR_BOOL:
    out->kind = KOTONOHA_KTRF_RUNTIME_BOOL;
    out->as.boolean_value = source->as.boolean_value != 0;
    return 1;
  case KOTONOHA_KTRF_VAR_INT32:
    out->kind = KOTONOHA_KTRF_RUNTIME_INT64;
    out->as.int64_value = source->as.int32_value;
    return 1;
  case KOTONOHA_KTRF_VAR_UINT32:
    out->kind = KOTONOHA_KTRF_RUNTIME_UINT64;
    out->as.uint64_value = source->as.uint32_value;
    return 1;
  case KOTONOHA_KTRF_VAR_FLOAT32:
    out->kind = KOTONOHA_KTRF_RUNTIME_FLOAT64;
    out->as.float64_value = (double)source->as.float32_value;
    return 1;
  case KOTONOHA_KTRF_VAR_FLOAT64:
    out->kind = KOTONOHA_KTRF_RUNTIME_FLOAT64;
    out->as.float64_value = source->as.float64_value;
    return 1;
  case KOTONOHA_KTRF_VAR_STRING: {
    const char *data;
    size_t size;
    if (!rt_get_string(doc, source->as.string_index, &data, &size, error))
      return 0;
    out->kind = KOTONOHA_KTRF_RUNTIME_STRING;
    out->as.string_value.data = data;
    out->as.string_value.size = size;
    return 1;
  }
  case KOTONOHA_KTRF_VAR_BYTES:
    out->kind = KOTONOHA_KTRF_RUNTIME_BYTES;
    out->as.bytes_value.data = source->as.bytes_value.data;
    out->as.bytes_value.size = source->as.bytes_value.size;
    return 1;
  default:
    return rt_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                   "runtime cannot initialize variable default kind %u",
                   source->kind);
  }
}

static int rt_is_numeric(uint8_t kind) {
  return kind == KOTONOHA_KTRF_RUNTIME_BOOL ||
         kind == KOTONOHA_KTRF_RUNTIME_INT64 ||
         kind == KOTONOHA_KTRF_RUNTIME_UINT64 ||
         kind == KOTONOHA_KTRF_RUNTIME_FLOAT64;
}

static long double rt_numeric(const Kotonoha_KtrfRuntimeValue *value) {
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

static int rt_equal(const Kotonoha_KtrfRuntimeValue *a,
                    const Kotonoha_KtrfRuntimeValue *b) {
  if (rt_is_numeric(a->kind) && rt_is_numeric(b->kind))
    return rt_numeric(a) == rt_numeric(b);
  if (a->kind != b->kind)
    return 0;
  switch (a->kind) {
  case KOTONOHA_KTRF_RUNTIME_NULL:
    return 1;
  case KOTONOHA_KTRF_RUNTIME_STRING:
    return a->as.string_value.size == b->as.string_value.size &&
           memcmp(a->as.string_value.data, b->as.string_value.data,
                  a->as.string_value.size) == 0;
  case KOTONOHA_KTRF_RUNTIME_BYTES:
    return a->as.bytes_value.size == b->as.bytes_value.size &&
           memcmp(a->as.bytes_value.data, b->as.bytes_value.data,
                  a->as.bytes_value.size) == 0;
  case KOTONOHA_KTRF_RUNTIME_ENTITY:
    return a->entity_kind == b->entity_kind &&
           a->as.entity_index == b->as.entity_index;
  default:
    return 0;
  }
}

static int rt_require_bool(const Kotonoha_KtrfRuntimeValue *value, int *out,
                           Kotonoha_KtrfError *error) {
  if (value->kind != KOTONOHA_KTRF_RUNTIME_BOOL)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                   "boolean expression operand has runtime kind %u", value->kind);
  *out = value->as.boolean_value != 0;
  return 1;
}

static int rt_coerce_for_variable(const Kotonoha_KtrfVariable *variable,
                                  const Kotonoha_KtrfRuntimeValue *value,
                                  Kotonoha_KtrfRuntimeValue *out,
                                  Kotonoha_KtrfError *error) {
  *out = *value;
  switch (variable->default_value.kind) {
  case KOTONOHA_KTRF_VAR_BOOL:
    if (value->kind != KOTONOHA_KTRF_RUNTIME_BOOL)
      return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "cannot assign non-bool value to bool variable");
    return 1;
  case KOTONOHA_KTRF_VAR_INT32: {
    long double n;
    if (!rt_is_numeric(value->kind))
      return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "cannot assign non-numeric value to int32 variable");
    n = rt_numeric(value);
    if (n < -2147483648.0L || n > 2147483647.0L || (long double)(int64_t)n != n)
      return rt_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                     "assigned value is outside exact int32 range");
    out->kind = KOTONOHA_KTRF_RUNTIME_INT64;
    out->as.int64_value = (int64_t)n;
    return 1;
  }
  case KOTONOHA_KTRF_VAR_UINT32: {
    long double n;
    if (!rt_is_numeric(value->kind))
      return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "cannot assign non-numeric value to uint32 variable");
    n = rt_numeric(value);
    if (n < 0.0L || n > 4294967295.0L || (long double)(uint64_t)n != n)
      return rt_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                     "assigned value is outside exact uint32 range");
    out->kind = KOTONOHA_KTRF_RUNTIME_UINT64;
    out->as.uint64_value = (uint64_t)n;
    return 1;
  }
  case KOTONOHA_KTRF_VAR_FLOAT32: {
    float narrowed;
    if (!rt_is_numeric(value->kind))
      return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "cannot assign non-numeric value to float32 variable");
    narrowed = (float)rt_numeric(value);
    if (!isfinite(narrowed))
      return rt_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                     "assigned float32 value is not finite");
    out->kind = KOTONOHA_KTRF_RUNTIME_FLOAT64;
    out->as.float64_value = (double)narrowed;
    return 1;
  }
  case KOTONOHA_KTRF_VAR_FLOAT64:
    if (!rt_is_numeric(value->kind))
      return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "cannot assign non-numeric value to float64 variable");
    out->kind = KOTONOHA_KTRF_RUNTIME_FLOAT64;
    out->as.float64_value = (double)rt_numeric(value);
    if (!isfinite(out->as.float64_value))
      return rt_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                     "assigned float64 value is not finite");
    return 1;
  case KOTONOHA_KTRF_VAR_STRING:
    if (value->kind != KOTONOHA_KTRF_RUNTIME_STRING)
      return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "cannot assign non-string value to string variable");
    return 1;
  case KOTONOHA_KTRF_VAR_BYTES:
    if (value->kind != KOTONOHA_KTRF_RUNTIME_BYTES)
      return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "cannot assign non-bytes value to bytes variable");
    return 1;
  default:
    return rt_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                   "unsupported variable type/default kind %u",
                   variable->default_value.kind);
  }
}

void Kotonoha_KtrfRuntimeInit(Kotonoha_KtrfRuntime *runtime) {
  if (runtime != NULL) {
    memset(runtime, 0, sizeof(*runtime));
    runtime->last_registered_ending = KOTONOHA_KTRF_NULL_INDEX;
  }
}

void Kotonoha_KtrfRuntimeClean(Kotonoha_KtrfRuntime *runtime) {
  if (runtime == NULL)
    return;
  free(runtime->variables);
  free(runtime->expression_state);
  memset(runtime, 0, sizeof(*runtime));
  runtime->last_registered_ending = KOTONOHA_KTRF_NULL_INDEX;
}

int Kotonoha_KtrfRuntimeBind(Kotonoha_KtrfRuntime *runtime,
                             const Kotonoha_KtrfDocument *document,
                             const Kotonoha_KtrfRuntimeCallbacks *callbacks,
                             Kotonoha_KtrfError *error) {
  uint32_t variables, expressions;
  rt_clear_error(error);
  if (runtime == NULL || document == NULL)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "runtime/document argument is null");
  if (!Kotonoha_KtrfValidateTypedCore(document, error))
    return 0;

  Kotonoha_KtrfRuntimeClean(runtime);
  runtime->document = document;
  variables = rt_count(document, "VARS");
  expressions = rt_count(document, "EXPR");
  if (variables != 0u) {
    runtime->variables = (Kotonoha_KtrfRuntimeValue *)calloc(
        variables, sizeof(Kotonoha_KtrfRuntimeValue));
    if (runtime->variables == NULL) {
      Kotonoha_KtrfRuntimeClean(runtime);
      return rt_fail(error, KOTONOHA_KTRF_ERROR_MEMORY,
                     "out of memory allocating runtime variables");
    }
  }
  if (expressions != 0u) {
    runtime->expression_state = (uint8_t *)calloc(expressions, 1u);
    if (runtime->expression_state == NULL) {
      Kotonoha_KtrfRuntimeClean(runtime);
      return rt_fail(error, KOTONOHA_KTRF_ERROR_MEMORY,
                     "out of memory allocating expression state");
    }
  }
  runtime->variable_count = variables;
  runtime->expression_count = expressions;
  runtime->last_registered_ending = KOTONOHA_KTRF_NULL_INDEX;
  if (callbacks != NULL)
    runtime->callbacks = *callbacks;
  return Kotonoha_KtrfRuntimeResetDefaults(runtime, error);
}

int Kotonoha_KtrfRuntimeResetDefaults(Kotonoha_KtrfRuntime *runtime,
                                      Kotonoha_KtrfError *error) {
  uint32_t i;
  rt_clear_error(error);
  if (runtime == NULL || runtime->document == NULL)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "runtime is not bound to a KTRF document");
  for (i = 0u; i < runtime->variable_count; ++i) {
    Kotonoha_KtrfVariable variable;
    if (!Kotonoha_KtrfGetVariable(runtime->document, i, &variable, error))
      return 0;
    if (!rt_default_to_value(runtime->document, &variable.default_value,
                             &runtime->variables[i], error))
      return 0;
  }
  if (runtime->expression_state != NULL)
    memset(runtime->expression_state, 0, runtime->expression_count);
  runtime->has_registered_ending = 0;
  runtime->last_registered_ending = KOTONOHA_KTRF_NULL_INDEX;
  return 1;
}

int Kotonoha_KtrfRuntimeGetVariable(const Kotonoha_KtrfRuntime *runtime,
                                    uint32_t variable_index,
                                    Kotonoha_KtrfRuntimeValue *out,
                                    Kotonoha_KtrfError *error) {
  rt_clear_error(error);
  if (runtime == NULL || out == NULL || runtime->document == NULL)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "runtime/out argument is null or runtime is unbound");
  if (variable_index >= runtime->variable_count)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                   "runtime variable index %u is out of range", variable_index);
  *out = runtime->variables[variable_index];
  return 1;
}

int Kotonoha_KtrfRuntimeSetVariable(Kotonoha_KtrfRuntime *runtime,
                                    uint32_t variable_index,
                                    const Kotonoha_KtrfRuntimeValue *value,
                                    Kotonoha_KtrfError *error) {
  Kotonoha_KtrfVariable variable;
  Kotonoha_KtrfRuntimeValue coerced;
  rt_clear_error(error);
  if (runtime == NULL || value == NULL || runtime->document == NULL)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "runtime/value argument is null or runtime is unbound");
  if (variable_index >= runtime->variable_count)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                   "runtime variable index %u is out of range", variable_index);
  if (!Kotonoha_KtrfGetVariable(runtime->document, variable_index, &variable,
                                error))
    return 0;
  if (!rt_coerce_for_variable(&variable, value, &coerced, error))
    return 0;
  runtime->variables[variable_index] = coerced;
  return 1;
}

int Kotonoha_KtrfRuntimeEvalValueRef(Kotonoha_KtrfRuntime *runtime,
                                     const Kotonoha_KtrfValueRef *ref,
                                     Kotonoha_KtrfRuntimeValue *out,
                                     Kotonoha_KtrfError *error) {
  rt_clear_error(error);
  if (runtime == NULL || ref == NULL || out == NULL || runtime->document == NULL)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "runtime/value-ref/out argument is null or runtime is unbound");
  switch (ref->kind) {
  case KOTONOHA_KTRF_VALUE_LITERAL:
    return rt_scalar_to_value(runtime->document, &ref->literal, out, error);
  case KOTONOHA_KTRF_VALUE_VARIABLE:
    return Kotonoha_KtrfRuntimeGetVariable(runtime, ref->index, out, error);
  case KOTONOHA_KTRF_VALUE_EXPRESSION:
    return Kotonoha_KtrfRuntimeEvalExpression(runtime, ref->index, out, error);
  case KOTONOHA_KTRF_VALUE_ENTITY:
    *out = rt_null();
    out->kind = KOTONOHA_KTRF_RUNTIME_ENTITY;
    out->entity_kind = ref->entity_kind;
    out->as.entity_index = ref->index;
    return 1;
  default:
    return rt_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                   "unsupported runtime value-ref kind %u", ref->kind);
  }
}

static int rt_eval_arg(Kotonoha_KtrfRuntime *runtime, uint32_t expr_index,
                       uint32_t arg_index, Kotonoha_KtrfRuntimeValue *out,
                       Kotonoha_KtrfError *error) {
  Kotonoha_KtrfValueRef ref;
  if (!Kotonoha_KtrfGetExpressionArg(runtime->document, expr_index, arg_index,
                                     &ref, error))
    return 0;
  return Kotonoha_KtrfRuntimeEvalValueRef(runtime, &ref, out, error);
}

int Kotonoha_KtrfRuntimeEvalExpression(Kotonoha_KtrfRuntime *runtime,
                                       uint32_t expression_index,
                                       Kotonoha_KtrfRuntimeValue *out,
                                       Kotonoha_KtrfError *error) {
  Kotonoha_KtrfExpression expr;
  int is_eq, is_ne, is_gt, is_le, is_and, is_or;
  uint32_t i;
  rt_clear_error(error);
  if (runtime == NULL || out == NULL || runtime->document == NULL)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "runtime/out argument is null or runtime is unbound");
  if (expression_index >= runtime->expression_count)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                   "expression index %u is out of range", expression_index);
  if (runtime->expression_state[expression_index] != 0u)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                   "expression cycle detected at index %u", expression_index);
  if (!Kotonoha_KtrfGetExpression(runtime->document, expression_index, &expr,
                                  error))
    return 0;

  is_eq = rt_string_equals(runtime->document, expr.op_str, "ktrf:eq", error);
  if (is_eq < 0)
    return 0;
  is_ne = rt_string_equals(runtime->document, expr.op_str, "ktrf:ne", error);
  if (is_ne < 0)
    return 0;
  is_gt = rt_string_equals(runtime->document, expr.op_str, "ktrf:gt", error);
  if (is_gt < 0)
    return 0;
  is_le = rt_string_equals(runtime->document, expr.op_str, "ktrf:le", error);
  if (is_le < 0)
    return 0;
  is_and = rt_string_equals(runtime->document, expr.op_str, "ktrf:and", error);
  if (is_and < 0)
    return 0;
  is_or = rt_string_equals(runtime->document, expr.op_str, "ktrf:or", error);
  if (is_or < 0)
    return 0;

  runtime->expression_state[expression_index] = 1u;
  *out = rt_null();
  out->kind = KOTONOHA_KTRF_RUNTIME_BOOL;

  if (is_eq || is_ne || is_gt || is_le) {
    Kotonoha_KtrfRuntimeValue a, b;
    int result;
    if (expr.arg_count != 2u) {
      runtime->expression_state[expression_index] = 0u;
      return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "comparison expression %u must have exactly two args",
                     expression_index);
    }
    if (!rt_eval_arg(runtime, expression_index, 0u, &a, error) ||
        !rt_eval_arg(runtime, expression_index, 1u, &b, error)) {
      runtime->expression_state[expression_index] = 0u;
      return 0;
    }
    if (is_eq || is_ne) {
      result = rt_equal(&a, &b);
      if (is_ne)
        result = !result;
    } else {
      if (!rt_is_numeric(a.kind) || !rt_is_numeric(b.kind)) {
        runtime->expression_state[expression_index] = 0u;
        return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "ordered comparison expression requires numeric args");
      }
      result = is_gt ? (rt_numeric(&a) > rt_numeric(&b))
                     : (rt_numeric(&a) <= rt_numeric(&b));
    }
    out->as.boolean_value = result;
    runtime->expression_state[expression_index] = 0u;
    return 1;
  }

  if (is_and || is_or) {
    int aggregate = is_and ? 1 : 0;
    if (expr.arg_count == 0u) {
      runtime->expression_state[expression_index] = 0u;
      return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "boolean expression %u has no arguments", expression_index);
    }
    for (i = 0u; i < expr.arg_count; ++i) {
      Kotonoha_KtrfRuntimeValue value;
      int boolean_value;
      if (!rt_eval_arg(runtime, expression_index, i, &value, error) ||
          !rt_require_bool(&value, &boolean_value, error)) {
        runtime->expression_state[expression_index] = 0u;
        return 0;
      }
      if (is_and && !boolean_value) {
        aggregate = 0;
        break;
      }
      if (is_or && boolean_value) {
        aggregate = 1;
        break;
      }
    }
    out->as.boolean_value = aggregate;
    runtime->expression_state[expression_index] = 0u;
    return 1;
  }

  runtime->expression_state[expression_index] = 0u;
  return rt_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                 "unsupported KTRF expression operator at index %u",
                 expression_index);
}

static int rt_add_values(const Kotonoha_KtrfRuntimeValue *a,
                         const Kotonoha_KtrfRuntimeValue *b,
                         Kotonoha_KtrfRuntimeValue *out,
                         Kotonoha_KtrfError *error) {
  if (!rt_is_numeric(a->kind) || !rt_is_numeric(b->kind))
    return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                   "ktrf:add requires numeric target and value");
  if (a->kind == KOTONOHA_KTRF_RUNTIME_FLOAT64 ||
      b->kind == KOTONOHA_KTRF_RUNTIME_FLOAT64) {
    long double result = rt_numeric(a) + rt_numeric(b);
    out->kind = KOTONOHA_KTRF_RUNTIME_FLOAT64;
    out->as.float64_value = (double)result;
    if (!isfinite(out->as.float64_value))
      return rt_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                     "ktrf:add produced non-finite floating result");
    return 1;
  }
  if (a->kind == KOTONOHA_KTRF_RUNTIME_UINT64 &&
      b->kind == KOTONOHA_KTRF_RUNTIME_UINT64) {
    if (UINT64_MAX - a->as.uint64_value < b->as.uint64_value)
      return rt_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                     "ktrf:add uint64 overflow");
    out->kind = KOTONOHA_KTRF_RUNTIME_UINT64;
    out->as.uint64_value = a->as.uint64_value + b->as.uint64_value;
    return 1;
  }
  {
    long double result = rt_numeric(a) + rt_numeric(b);
    if (result < (long double)INT64_MIN || result > (long double)INT64_MAX ||
        (long double)(int64_t)result != result)
      return rt_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                     "ktrf:add integer result is outside int64 range");
    out->kind = KOTONOHA_KTRF_RUNTIME_INT64;
    out->as.int64_value = (int64_t)result;
    return 1;
  }
}

int Kotonoha_KtrfRuntimeApplyEffect(Kotonoha_KtrfRuntime *runtime,
                                    uint32_t effect_index,
                                    Kotonoha_KtrfError *error) {
  Kotonoha_KtrfEffect effect;
  rt_clear_error(error);
  if (runtime == NULL || runtime->document == NULL)
    return rt_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                   "runtime is null or unbound");
  if (!Kotonoha_KtrfGetEffect(runtime->document, effect_index, &effect, error))
    return 0;

  switch (effect.shape) {
  case KOTONOHA_KTRF_EFFECT_SET:
  case KOTONOHA_KTRF_EFFECT_ADD: {
    Kotonoha_KtrfValueRef ref;
    Kotonoha_KtrfRuntimeValue value;
    if (effect.value_count != 1u)
      return rt_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "set/add effect must contain exactly one value");
    if (!Kotonoha_KtrfGetEffectValue(runtime->document, effect_index, 0u, &ref,
                                     error) ||
        !Kotonoha_KtrfRuntimeEvalValueRef(runtime, &ref, &value, error))
      return 0;
    if (effect.shape == KOTONOHA_KTRF_EFFECT_ADD) {
      Kotonoha_KtrfRuntimeValue current, sum;
      if (!Kotonoha_KtrfRuntimeGetVariable(runtime, effect.payload_a, &current,
                                           error) ||
          !rt_add_values(&current, &value, &sum, error))
        return 0;
      value = sum;
    }
    return Kotonoha_KtrfRuntimeSetVariable(runtime, effect.payload_a, &value,
                                           error);
  }
  case KOTONOHA_KTRF_EFFECT_COPY: {
    Kotonoha_KtrfRuntimeValue source;
    if (!Kotonoha_KtrfRuntimeGetVariable(runtime, effect.payload_b, &source,
                                         error))
      return 0;
    return Kotonoha_KtrfRuntimeSetVariable(runtime, effect.payload_a, &source,
                                           error);
  }
  case KOTONOHA_KTRF_EFFECT_REGISTER_ENDING:
    runtime->has_registered_ending = 1;
    runtime->last_registered_ending = effect.payload_a;
    if (runtime->callbacks.register_ending != NULL)
      return runtime->callbacks.register_ending(
          runtime, effect.payload_a, runtime->callbacks.userdata, error);
    return 1;
  case KOTONOHA_KTRF_EFFECT_CALL_HOOK: {
    Kotonoha_KtrfRuntimeValue *arguments = NULL;
    uint32_t i;
    int ok;
    if (runtime->callbacks.call_hook == NULL)
      return rt_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                     "ktrf:call-hook has no runtime callback installed");
    if (effect.value_count != 0u) {
      arguments = (Kotonoha_KtrfRuntimeValue *)calloc(
          effect.value_count, sizeof(Kotonoha_KtrfRuntimeValue));
      if (arguments == NULL)
        return rt_fail(error, KOTONOHA_KTRF_ERROR_MEMORY,
                       "out of memory allocating hook arguments");
    }
    for (i = 0u; i < effect.value_count; ++i) {
      Kotonoha_KtrfValueRef ref;
      if (!Kotonoha_KtrfGetEffectValue(runtime->document, effect_index, i, &ref,
                                       error) ||
          !Kotonoha_KtrfRuntimeEvalValueRef(runtime, &ref, &arguments[i], error)) {
        free(arguments);
        return 0;
      }
    }
    ok = runtime->callbacks.call_hook(runtime, effect.payload_a, arguments,
                                      effect.value_count,
                                      runtime->callbacks.userdata, error);
    free(arguments);
    return ok;
  }
  default:
    return rt_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                   "unsupported effect shape %u", effect.shape);
  }
}
