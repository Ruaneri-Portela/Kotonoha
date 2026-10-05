#include "Kotonoha/parsers/KtrfTyped.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KTRF_FEATURE_FLAGS (KOTONOHA_KTRF_FEATURE_REQUIRED | KOTONOHA_KTRF_FEATURE_OPTIONAL)
#define KTRF_RSRC_FLAGS (KOTONOHA_KTRF_RSRC_REQUIRED | KOTONOHA_KTRF_RSRC_HAS_REQUIRED)
#define KTRF_TRAN_FLAGS (KOTONOHA_KTRF_TRAN_TERMINAL | KOTONOHA_KTRF_TRAN_HAS_TRIGGERS)

static void typed_clear(Kotonoha_KtrfError *error) {
  if (error != NULL) {
    error->code = KOTONOHA_KTRF_OK;
    error->message[0] = '\0';
  }
}

static int typed_fail(Kotonoha_KtrfError *error, Kotonoha_KtrfErrorCode code,
                      const char *format, ...) {
  if (error != NULL) {
    va_list args;
    error->code = code;
    va_start(args, format);
#if defined(_MSC_VER)
    _vsnprintf_s(error->message, sizeof(error->message), _TRUNCATE, format, args);
#else
    vsnprintf(error->message, sizeof(error->message), format, args);
#endif
    va_end(args);
    error->message[sizeof(error->message) - 1u] = '\0';
  }
  return 0;
}

static uint16_t typed_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t typed_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint64_t typed_u64(const uint8_t *p) {
  return (uint64_t)typed_u32(p) | ((uint64_t)typed_u32(p + 4u) << 32);
}

static int typed_range_u32(uint32_t start, uint32_t count, uint32_t total) {
  return start <= total && count <= total - start;
}

static const Kotonoha_KtrfSection *
typed_section(const Kotonoha_KtrfDocument *document, const char type[4],
              Kotonoha_KtrfError *error) {
  const Kotonoha_KtrfSection *section;
  if (document == NULL) {
    typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT, "KTRF document is null");
    return NULL;
  }
  section = Kotonoha_KtrfFindSection(document, type);
  if (section == NULL) {
    typed_fail(error, KOTONOHA_KTRF_ERROR_MISSING_SECTION,
               "missing KTRF typed-core section %.4s", type);
    return NULL;
  }
  return section;
}

static int typed_string(const Kotonoha_KtrfDocument *document, uint32_t index,
                        const char **data, size_t *length,
                        Kotonoha_KtrfError *error) {
  if (!Kotonoha_KtrfGetString(document, index, data, length, error))
    return 0;
  return 1;
}

static int typed_string_index(const Kotonoha_KtrfDocument *document,
                              uint32_t index, const char *label,
                              Kotonoha_KtrfError *error) {
  const char *data;
  size_t length;
  if (index == KOTONOHA_KTRF_NULL_INDEX)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "%s uses NULL_INDEX where a string is required", label);
  if (!typed_string(document, index, &data, &length, error))
    return 0;
  (void)data;
  (void)length;
  return 1;
}

static int typed_optional_string_index(const Kotonoha_KtrfDocument *document,
                                       uint32_t index, const char *label,
                                       Kotonoha_KtrfError *error) {
  if (index == KOTONOHA_KTRF_NULL_INDEX)
    return 1;
  return typed_string_index(document, index, label, error);
}

static int typed_string_less(const Kotonoha_KtrfDocument *document,
                             uint32_t left, uint32_t right,
                             Kotonoha_KtrfError *error) {
  const char *a, *b;
  size_t an, bn, common;
  int cmp;
  if (!typed_string(document, left, &a, &an, error) ||
      !typed_string(document, right, &b, &bn, error))
    return 0;
  common = an < bn ? an : bn;
  cmp = common != 0u ? memcmp(a, b, common) : 0;
  if (cmp < 0)
    return 1;
  if (cmp > 0)
    return 0;
  return an < bn;
}

static int typed_string_equals_literal(const Kotonoha_KtrfDocument *document,
                                       uint32_t index, const char *literal,
                                       Kotonoha_KtrfError *error) {
  const char *data;
  size_t length, expected;
  if (!typed_string(document, index, &data, &length, error))
    return 0;
  expected = strlen(literal);
  return length == expected && memcmp(data, literal, length) == 0;
}

static int typed_fixed_record(const Kotonoha_KtrfDocument *document,
                              const char type[4], uint32_t record_size,
                              uint32_t index, const uint8_t **record,
                              uint32_t *count, Kotonoha_KtrfError *error) {
  const Kotonoha_KtrfSection *section = typed_section(document, type, error);
  uint64_t expected;
  if (section == NULL)
    return 0;
  expected = (uint64_t)section->item_count * record_size;
  if (section->stored_size != expected)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "%.4s payload size/item_count mismatch", type);
  if (index >= section->item_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "%.4s record index %u is out of range", type, index);
  *record = section->payload + (uint64_t)index * record_size;
  if (count != NULL)
    *count = section->item_count;
  return 1;
}

static int typed_decode_scalar(const Kotonoha_KtrfDocument *document,
                               uint8_t kind, uint32_t payload_a,
                               uint64_t payload_b, int allow_absent,
                               Kotonoha_KtrfScalar *out,
                               Kotonoha_KtrfError *error) {
  uint64_t bits;
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "KTRF scalar output is null");
  memset(out, 0, sizeof(*out));
  out->kind = kind;
  switch (kind) {
  case KOTONOHA_KTRF_SCALAR_ABSENT:
    if (!allow_absent || payload_a != 0u || payload_b != 0u)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid absent KTRF scalar payload");
    return 1;
  case KOTONOHA_KTRF_SCALAR_NULL:
    if (payload_a != 0u || payload_b != 0u)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid null KTRF scalar payload");
    return 1;
  case KOTONOHA_KTRF_SCALAR_BOOL:
    if (payload_a != 0u || payload_b > 1u)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid bool KTRF scalar payload");
    out->as.boolean_value = payload_b != 0u;
    return 1;
  case KOTONOHA_KTRF_SCALAR_INT64:
    if (payload_a != 0u)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid int64 KTRF scalar payload");
    memcpy(&out->as.int64_value, &payload_b, sizeof(payload_b));
    return 1;
  case KOTONOHA_KTRF_SCALAR_FLOAT64:
    if (payload_a != 0u)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid float64 KTRF scalar payload");
    bits = payload_b;
    memcpy(&out->as.float64_value, &bits, sizeof(bits));
    if (!isfinite(out->as.float64_value))
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "non-finite float64 KTRF scalar");
    return 1;
  case KOTONOHA_KTRF_SCALAR_STRING:
    if (payload_b != 0u)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid string KTRF scalar payload");
    if (!typed_string_index(document, payload_a, "scalar string", error))
      return 0;
    out->as.string_index = payload_a;
    return 1;
  default:
    return typed_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                      "unknown KTRF scalar kind %u", (unsigned)kind);
  }
}

typedef struct TypedVarsView {
  const Kotonoha_KtrfSection *section;
  uint32_t count;
  uint32_t blob_offset;
  uint32_t blob_size;
} TypedVarsView;

static int typed_vars_view(const Kotonoha_KtrfDocument *document,
                           TypedVarsView *view, Kotonoha_KtrfError *error) {
  const Kotonoha_KtrfSection *s = typed_section(document, "VARS", error);
  uint32_t count, record_size, blob_offset, blob_size;
  uint64_t expected_offset;
  if (s == NULL)
    return 0;
  if (s->stored_size < 16u)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "VARS payload is too small");
  count = typed_u32(s->payload + 0u);
  record_size = typed_u32(s->payload + 4u);
  blob_offset = typed_u32(s->payload + 8u);
  blob_size = typed_u32(s->payload + 12u);
  expected_offset = 16u + (uint64_t)count * 32u;
  if (count != s->item_count || record_size != 32u ||
      blob_offset != expected_offset ||
      (uint64_t)blob_offset + blob_size != s->stored_size)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "VARS header/layout mismatch");
  view->section = s;
  view->count = count;
  view->blob_offset = blob_offset;
  view->blob_size = blob_size;
  return 1;
}

typedef struct TypedPool6View {
  const Kotonoha_KtrfSection *section;
  uint32_t count;
  uint32_t record_size;
  uint32_t pool_count;
  uint32_t pool_record_size;
  uint32_t records_offset;
  uint32_t pool_offset;
} TypedPool6View;

static int typed_pool6_view(const Kotonoha_KtrfDocument *document,
                            const char type[4], uint32_t expected_record,
                            uint32_t expected_pool_record,
                            TypedPool6View *view,
                            Kotonoha_KtrfError *error) {
  const Kotonoha_KtrfSection *s = typed_section(document, type, error);
  uint32_t count, record_size, pool_count, pool_record_size, records_offset,
      pool_offset;
  uint64_t expected_pool, expected_size;
  if (s == NULL)
    return 0;
  if (s->stored_size < 24u)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "%.4s payload is too small", type);
  count = typed_u32(s->payload + 0u);
  record_size = typed_u32(s->payload + 4u);
  pool_count = typed_u32(s->payload + 8u);
  pool_record_size = typed_u32(s->payload + 12u);
  records_offset = typed_u32(s->payload + 16u);
  pool_offset = typed_u32(s->payload + 20u);
  expected_pool = 24u + (uint64_t)count * expected_record;
  expected_size = expected_pool + (uint64_t)pool_count * expected_pool_record;
  if (count != s->item_count || record_size != expected_record ||
      pool_record_size != expected_pool_record || records_offset != 24u ||
      pool_offset != expected_pool || s->stored_size != expected_size)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "%.4s header/layout mismatch", type);
  view->section = s;
  view->count = count;
  view->record_size = record_size;
  view->pool_count = pool_count;
  view->pool_record_size = pool_record_size;
  view->records_offset = records_offset;
  view->pool_offset = pool_offset;
  return 1;
}

typedef struct TypedChoiceView {
  const Kotonoha_KtrfSection *section;
  uint32_t choice_count, option_count, timeout_count, effect_count;
  uint32_t choices_offset, options_offset, timeouts_offset, effects_offset;
} TypedChoiceView;

static int typed_choice_view(const Kotonoha_KtrfDocument *document,
                             TypedChoiceView *view,
                             Kotonoha_KtrfError *error) {
  const Kotonoha_KtrfSection *s = typed_section(document, "CHOI", error);
  uint32_t cc, cr, oc, or_, tc, tr, ec, er, co, oo, to, eo;
  uint64_t eoo, eto, eeo, esize;
  if (s == NULL)
    return 0;
  if (s->stored_size < 48u)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "CHOI payload is too small");
  cc = typed_u32(s->payload + 0u);
  cr = typed_u32(s->payload + 4u);
  oc = typed_u32(s->payload + 8u);
  or_ = typed_u32(s->payload + 12u);
  tc = typed_u32(s->payload + 16u);
  tr = typed_u32(s->payload + 20u);
  ec = typed_u32(s->payload + 24u);
  er = typed_u32(s->payload + 28u);
  co = typed_u32(s->payload + 32u);
  oo = typed_u32(s->payload + 36u);
  to = typed_u32(s->payload + 40u);
  eo = typed_u32(s->payload + 44u);
  eoo = 48u + (uint64_t)cc * 32u;
  eto = eoo + (uint64_t)oc * 36u;
  eeo = eto + (uint64_t)tc * 28u;
  esize = eeo + (uint64_t)ec * 4u;
  if (cc != s->item_count || cr != 32u || or_ != 36u || tr != 28u ||
      er != 4u || co != 48u || oo != eoo || to != eto || eo != eeo ||
      s->stored_size != esize)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "CHOI header/layout mismatch");
  view->section = s;
  view->choice_count = cc;
  view->option_count = oc;
  view->timeout_count = tc;
  view->effect_count = ec;
  view->choices_offset = co;
  view->options_offset = oo;
  view->timeouts_offset = to;
  view->effects_offset = eo;
  return 1;
}

typedef struct TypedTranView {
  const Kotonoha_KtrfSection *section;
  uint32_t count, effect_count, trigger_count, effects_offset, triggers_offset;
} TypedTranView;

static int typed_tran_view(const Kotonoha_KtrfDocument *document,
                           TypedTranView *view, Kotonoha_KtrfError *error) {
  const Kotonoha_KtrfSection *s = typed_section(document, "TRAN", error);
  uint32_t count, recsz, ec, ersz, tc, trsz, eo, to;
  uint64_t expected_eo, expected_to, expected_size;
  if (s == NULL)
    return 0;
  if (s->stored_size < 32u)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "TRAN payload is too small");
  count = typed_u32(s->payload + 0u);
  recsz = typed_u32(s->payload + 4u);
  ec = typed_u32(s->payload + 8u);
  ersz = typed_u32(s->payload + 12u);
  tc = typed_u32(s->payload + 16u);
  trsz = typed_u32(s->payload + 20u);
  eo = typed_u32(s->payload + 24u);
  to = typed_u32(s->payload + 28u);
  expected_eo = 32u + (uint64_t)count * 48u;
  expected_to = expected_eo + (uint64_t)ec * 4u;
  expected_size = expected_to + (uint64_t)tc * 4u;
  if (count != s->item_count || recsz != 48u || ersz != 4u || trsz != 4u ||
      eo != expected_eo || to != expected_to || s->stored_size != expected_size)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "TRAN header/layout mismatch");
  view->section = s;
  view->count = count;
  view->effect_count = ec;
  view->trigger_count = tc;
  view->effects_offset = eo;
  view->triggers_offset = to;
  return 1;
}

int Kotonoha_KtrfGetMeta(const Kotonoha_KtrfDocument *document,
                         Kotonoha_KtrfMeta *out, Kotonoha_KtrfError *error) {
  const Kotonoha_KtrfSection *s;
  const uint8_t *p;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "META output is null");
  s = typed_section(document, "META", error);
  if (s == NULL)
    return 0;
  if (s->item_count != 1u || s->stored_size != 32u)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "META must contain one 32-byte record");
  p = s->payload;
  if (typed_u32(p + 20u) || typed_u32(p + 24u) || typed_u32(p + 28u))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "META reserved fields must be zero");
  out->format_str = typed_u32(p + 0u);
  out->ir_version_str = typed_u32(p + 4u);
  out->document_id_str = typed_u32(p + 8u);
  out->profile_id_str = typed_u32(p + 12u);
  out->profile_version_str = typed_u32(p + 16u);
  if (!typed_string_index(document, out->format_str, "META.format", error) ||
      !typed_string_index(document, out->ir_version_str, "META.ir_version", error) ||
      !typed_string_index(document, out->document_id_str, "META.document_id", error) ||
      !typed_string_index(document, out->profile_id_str, "META.profile.id", error) ||
      !typed_string_index(document, out->profile_version_str,
                          "META.profile.version", error))
    return 0;
  return 1;
}

int Kotonoha_KtrfGetNamespace(const Kotonoha_KtrfDocument *document,
                              uint32_t index, Kotonoha_KtrfNamespace *out,
                              Kotonoha_KtrfError *error) {
  const uint8_t *p;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "NSPC output is null");
  if (!typed_fixed_record(document, "NSPC", 8u, index, &p, NULL, error))
    return 0;
  out->prefix_str = typed_u32(p);
  out->uri_str = typed_u32(p + 4u);
  return typed_string_index(document, out->prefix_str, "NSPC.prefix", error) &&
         typed_string_index(document, out->uri_str, "NSPC.uri", error);
}

int Kotonoha_KtrfGetFeature(const Kotonoha_KtrfDocument *document,
                            uint32_t index, Kotonoha_KtrfFeature *out,
                            Kotonoha_KtrfError *error) {
  const uint8_t *p;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "FEAT output is null");
  if (!typed_fixed_record(document, "FEAT", 12u, index, &p, NULL, error))
    return 0;
  out->id_str = typed_u32(p);
  out->version_str = typed_u32(p + 4u);
  out->flags = typed_u32(p + 8u);
  if (out->flags != KOTONOHA_KTRF_FEATURE_REQUIRED &&
      out->flags != KOTONOHA_KTRF_FEATURE_OPTIONAL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "FEAT record has invalid flags 0x%08X", out->flags);
  return typed_string_index(document, out->id_str, "FEAT.id", error) &&
         typed_string_index(document, out->version_str, "FEAT.version", error);
}

int Kotonoha_KtrfGetEntryPoint(const Kotonoha_KtrfDocument *document,
                               uint32_t index, Kotonoha_KtrfEntryPoint *out,
                               Kotonoha_KtrfError *error) {
  const uint8_t *p;
  const Kotonoha_KtrfSection *nodes;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "ENTR output is null");
  if (!typed_fixed_record(document, "ENTR", 12u, index, &p, NULL, error))
    return 0;
  nodes = typed_section(document, "NODE", error);
  if (nodes == NULL)
    return 0;
  out->id_str = typed_u32(p);
  out->node_index = typed_u32(p + 4u);
  out->trigger_str = typed_u32(p + 8u);
  if (out->node_index >= nodes->item_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "ENTR node index out of range");
  return typed_string_index(document, out->id_str, "ENTR.id", error) &&
         typed_string_index(document, out->trigger_str, "ENTR.trigger", error);
}

int Kotonoha_KtrfGetVariable(const Kotonoha_KtrfDocument *document,
                             uint32_t index, Kotonoha_KtrfVariable *out,
                             Kotonoha_KtrfError *error) {
  TypedVarsView view;
  const uint8_t *p, *blob;
  uint8_t kind, flags;
  uint16_t reserved;
  uint32_t a, b;
  uint64_t c;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "VARS output is null");
  if (!typed_vars_view(document, &view, error))
    return 0;
  if (index >= view.count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "VARS record index %u is out of range", index);
  p = view.section->payload + 16u + (uint64_t)index * 32u;
  blob = view.section->payload + view.blob_offset;
  out->id_str = typed_u32(p + 0u);
  out->type_str = typed_u32(p + 4u);
  out->scope_str = typed_u32(p + 8u);
  kind = p[12u];
  flags = p[13u];
  reserved = typed_u16(p + 14u);
  a = typed_u32(p + 16u);
  b = typed_u32(p + 20u);
  c = typed_u64(p + 24u);
  if (flags != 0u || reserved != 0u)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "VARS flags/reserved fields must be zero");
  if (!typed_string_index(document, out->id_str, "VARS.id", error) ||
      !typed_string_index(document, out->type_str, "VARS.type", error) ||
      !typed_string_index(document, out->scope_str, "VARS.scope", error))
    return 0;
  {
    uint8_t expected_kind = 0u;
    if (typed_string_equals_literal(document, out->type_str, "ktrf:bool", error))
      expected_kind = KOTONOHA_KTRF_VAR_BOOL;
    else if (typed_string_equals_literal(document, out->type_str, "ktrf:int32", error))
      expected_kind = KOTONOHA_KTRF_VAR_INT32;
    else if (typed_string_equals_literal(document, out->type_str, "ktrf:uint32", error))
      expected_kind = KOTONOHA_KTRF_VAR_UINT32;
    else if (typed_string_equals_literal(document, out->type_str, "ktrf:float32", error))
      expected_kind = KOTONOHA_KTRF_VAR_FLOAT32;
    else if (typed_string_equals_literal(document, out->type_str, "ktrf:float64", error))
      expected_kind = KOTONOHA_KTRF_VAR_FLOAT64;
    else if (typed_string_equals_literal(document, out->type_str, "ktrf:string", error))
      expected_kind = KOTONOHA_KTRF_VAR_STRING;
    else if (typed_string_equals_literal(document, out->type_str, "ktrf:bytes", error))
      expected_kind = KOTONOHA_KTRF_VAR_BYTES;
    if (expected_kind == 0u)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                        "unsupported VARS type/default encoding");
    if (kind != expected_kind)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "VARS type/default-kind mismatch");
  }
  memset(&out->default_value, 0, sizeof(out->default_value));
  out->default_value.kind = kind;
  switch (kind) {
  case KOTONOHA_KTRF_VAR_BOOL:
    if (a || b || c > 1u)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid VARS bool default payload");
    out->default_value.as.boolean_value = c != 0u;
    break;
  case KOTONOHA_KTRF_VAR_INT32:
    if (a || b || c > UINT32_MAX)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid VARS int32 default payload");
    out->default_value.as.int32_value = (int32_t)(uint32_t)c;
    break;
  case KOTONOHA_KTRF_VAR_UINT32:
    if (a || b || c > UINT32_MAX)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid VARS uint32 default payload");
    out->default_value.as.uint32_value = (uint32_t)c;
    break;
  case KOTONOHA_KTRF_VAR_FLOAT32: {
    uint32_t bits;
    if (a || b || c > UINT32_MAX)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid VARS float32 default payload");
    bits = (uint32_t)c;
    memcpy(&out->default_value.as.float32_value, &bits, sizeof(bits));
    break;
  }
  case KOTONOHA_KTRF_VAR_FLOAT64: {
    uint64_t bits;
    if (a || b)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid VARS float64 default payload");
    bits = c;
    memcpy(&out->default_value.as.float64_value, &bits, sizeof(bits));
    break;
  }
  case KOTONOHA_KTRF_VAR_STRING:
    if (b || c)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid VARS string default payload");
    if (!typed_string_index(document, a, "VARS.default-string", error))
      return 0;
    out->default_value.as.string_index = a;
    break;
  case KOTONOHA_KTRF_VAR_BYTES:
    if (c || !typed_range_u32(a, b, view.blob_size))
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid VARS bytes default payload");
    out->default_value.as.bytes_value.data = blob + a;
    out->default_value.as.bytes_value.size = b;
    break;
  default:
    return typed_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                      "unknown VARS default kind %u", (unsigned)kind);
  }
  return 1;
}

int Kotonoha_KtrfGetResource(const Kotonoha_KtrfDocument *document,
                             uint32_t index, Kotonoha_KtrfResource *out,
                             Kotonoha_KtrfError *error) {
  const uint8_t *p;
  uint32_t reserved;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "RSRC output is null");
  if (!typed_fixed_record(document, "RSRC", 24u, index, &p, NULL, error))
    return 0;
  out->id_str = typed_u32(p + 0u);
  out->scheme_str = typed_u32(p + 4u);
  out->value_str = typed_u32(p + 8u);
  out->media_type_str = typed_u32(p + 12u);
  out->flags = typed_u32(p + 16u);
  reserved = typed_u32(p + 20u);
  if (reserved != 0u || (out->flags & ~KTRF_RSRC_FLAGS) != 0u ||
      ((out->flags & KOTONOHA_KTRF_RSRC_REQUIRED) != 0u &&
       (out->flags & KOTONOHA_KTRF_RSRC_HAS_REQUIRED) == 0u))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "invalid RSRC flags/reserved fields");
  return typed_string_index(document, out->id_str, "RSRC.id", error) &&
         typed_string_index(document, out->scheme_str, "RSRC.scheme", error) &&
         typed_string_index(document, out->value_str, "RSRC.value", error) &&
         typed_optional_string_index(document, out->media_type_str,
                                     "RSRC.media_type", error);
}

int Kotonoha_KtrfGetHook(const Kotonoha_KtrfDocument *document,
                         uint32_t index, Kotonoha_KtrfHook *out,
                         Kotonoha_KtrfError *error) {
  const uint8_t *p;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "HOOK output is null");
  if (!typed_fixed_record(document, "HOOK", 16u, index, &p, NULL, error))
    return 0;
  out->id_str = typed_u32(p + 0u);
  out->symbol_str = typed_u32(p + 4u);
  out->contract_str = typed_u32(p + 8u);
  if (typed_u32(p + 12u) != 0u)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "HOOK reserved field must be zero");
  return typed_string_index(document, out->id_str, "HOOK.id", error) &&
         typed_string_index(document, out->symbol_str, "HOOK.symbol", error) &&
         typed_string_index(document, out->contract_str, "HOOK.contract", error);
}

int Kotonoha_KtrfGetEnding(const Kotonoha_KtrfDocument *document,
                           uint32_t index, Kotonoha_KtrfEnding *out,
                           Kotonoha_KtrfError *error) {
  const uint8_t *p;
  uint8_t kind, flags;
  uint16_t reserved;
  uint32_t a;
  uint64_t b;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "ENDG output is null");
  if (!typed_fixed_record(document, "ENDG", 24u, index, &p, NULL, error))
    return 0;
  out->id_str = typed_u32(p + 0u);
  out->label_str = typed_u32(p + 4u);
  kind = p[8u];
  flags = p[9u];
  reserved = typed_u16(p + 10u);
  a = typed_u32(p + 12u);
  b = typed_u64(p + 16u);
  if (flags || reserved)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "ENDG flags/reserved fields must be zero");
  if (!typed_string_index(document, out->id_str, "ENDG.id", error) ||
      !typed_optional_string_index(document, out->label_str, "ENDG.label", error))
    return 0;
  out->has_code = kind != KOTONOHA_KTRF_SCALAR_ABSENT;
  return typed_decode_scalar(document, kind, a, b, 1, &out->code, error);
}

static int typed_value_ref_at(const Kotonoha_KtrfDocument *document,
                              const uint8_t *p, uint32_t variable_count,
                              uint32_t expression_count,
                              uint32_t resource_count, uint32_t hook_count,
                              uint32_t ending_count,
                              Kotonoha_KtrfValueRef *out,
                              Kotonoha_KtrfError *error) {
  uint8_t kind, literal_kind, entity_kind, flags;
  uint32_t a, aux, reserved;
  uint64_t b;
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "valueRef output is null");
  kind = p[0u];
  literal_kind = p[1u];
  entity_kind = p[2u];
  flags = p[3u];
  a = typed_u32(p + 4u);
  aux = typed_u32(p + 8u);
  b = typed_u64(p + 12u);
  reserved = typed_u32(p + 20u);
  if (flags || aux || reserved)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "valueRef flags/reserved fields must be zero");
  memset(out, 0, sizeof(*out));
  out->kind = kind;
  out->entity_kind = entity_kind;
  if (kind == KOTONOHA_KTRF_VALUE_LITERAL) {
    if (entity_kind != KOTONOHA_KTRF_ENTITY_NONE || literal_kind == 0u)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "invalid literal valueRef kind fields");
    return typed_decode_scalar(document, literal_kind, a, b, 0, &out->literal,
                               error);
  }
  if (literal_kind != 0u || b != 0u)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "non-literal valueRef has literal payload");
  out->index = a;
  switch (kind) {
  case KOTONOHA_KTRF_VALUE_VARIABLE:
    if (entity_kind != KOTONOHA_KTRF_ENTITY_NONE || a >= variable_count)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                        "valueRef variable index out of range");
    return 1;
  case KOTONOHA_KTRF_VALUE_EXPRESSION:
    if (entity_kind != KOTONOHA_KTRF_ENTITY_NONE || a >= expression_count)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                        "valueRef expression index out of range");
    return 1;
  case KOTONOHA_KTRF_VALUE_ENTITY:
    if (entity_kind == KOTONOHA_KTRF_ENTITY_RESOURCE) {
      if (a >= resource_count)
        return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                          "valueRef resource index out of range");
    } else if (entity_kind == KOTONOHA_KTRF_ENTITY_HOOK) {
      if (a >= hook_count)
        return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                          "valueRef hook index out of range");
    } else if (entity_kind == KOTONOHA_KTRF_ENTITY_ENDING) {
      if (a >= ending_count)
        return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                          "valueRef ending index out of range");
    } else {
      return typed_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                        "unknown valueRef entity kind %u",
                        (unsigned)entity_kind);
    }
    return 1;
  default:
    return typed_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                      "unknown valueRef kind %u", (unsigned)kind);
  }
}

int Kotonoha_KtrfGetExpression(const Kotonoha_KtrfDocument *document,
                               uint32_t index, Kotonoha_KtrfExpression *out,
                               Kotonoha_KtrfError *error) {
  TypedPool6View view;
  const uint8_t *p;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "EXPR output is null");
  if (!typed_pool6_view(document, "EXPR", 32u, 24u, &view, error))
    return 0;
  if (index >= view.count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "EXPR record index %u is out of range", index);
  p = view.section->payload + view.records_offset + (uint64_t)index * 32u;
  out->id_str = typed_u32(p + 0u);
  out->op_str = typed_u32(p + 4u);
  out->result_type_str = typed_u32(p + 8u);
  out->arg_start = typed_u32(p + 12u);
  out->arg_count = typed_u32(p + 16u);
  if (typed_u32(p + 20u) || typed_u32(p + 24u) || typed_u32(p + 28u))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "EXPR flags/reserved fields must be zero");
  if (!typed_range_u32(out->arg_start, out->arg_count, view.pool_count))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "EXPR argument slice is out of range");
  return typed_string_index(document, out->id_str, "EXPR.id", error) &&
         typed_string_index(document, out->op_str, "EXPR.op", error) &&
         typed_optional_string_index(document, out->result_type_str,
                                     "EXPR.result_type", error);
}

int Kotonoha_KtrfGetExpressionArg(const Kotonoha_KtrfDocument *document,
                                  uint32_t expression_index,
                                  uint32_t local_arg_index,
                                  Kotonoha_KtrfValueRef *out,
                                  Kotonoha_KtrfError *error) {
  TypedPool6View view;
  Kotonoha_KtrfExpression expr;
  const Kotonoha_KtrfSection *vars, *rsrc, *hook, *endg;
  const uint8_t *p;
  typed_clear(error);
  if (!typed_pool6_view(document, "EXPR", 32u, 24u, &view, error) ||
      !Kotonoha_KtrfGetExpression(document, expression_index, &expr, error))
    return 0;
  if (local_arg_index >= expr.arg_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "EXPR local argument index out of range");
  vars = typed_section(document, "VARS", error);
  rsrc = typed_section(document, "RSRC", error);
  hook = typed_section(document, "HOOK", error);
  endg = typed_section(document, "ENDG", error);
  if (!vars || !rsrc || !hook || !endg)
    return 0;
  p = view.section->payload + view.pool_offset +
      (uint64_t)(expr.arg_start + local_arg_index) * 24u;
  return typed_value_ref_at(document, p, vars->item_count, view.count,
                            rsrc->item_count, hook->item_count,
                            endg->item_count, out, error);
}

int Kotonoha_KtrfGetEffect(const Kotonoha_KtrfDocument *document,
                           uint32_t index, Kotonoha_KtrfEffect *out,
                           Kotonoha_KtrfError *error) {
  TypedPool6View view;
  const uint8_t *p;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "EFFT output is null");
  if (!typed_pool6_view(document, "EFFT", 32u, 24u, &view, error))
    return 0;
  if (index >= view.count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "EFFT record index %u is out of range", index);
  p = view.section->payload + view.records_offset + (uint64_t)index * 32u;
  out->id_str = typed_u32(p + 0u);
  out->op_str = typed_u32(p + 4u);
  out->shape = p[8u];
  if (p[9u] || typed_u16(p + 10u) || typed_u32(p + 28u))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "EFFT flags/reserved fields must be zero");
  out->payload_a = typed_u32(p + 12u);
  out->payload_b = typed_u32(p + 16u);
  out->value_start = typed_u32(p + 20u);
  out->value_count = typed_u32(p + 24u);
  if (!typed_range_u32(out->value_start, out->value_count, view.pool_count))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "EFFT value slice is out of range");
  return typed_string_index(document, out->id_str, "EFFT.id", error) &&
         typed_string_index(document, out->op_str, "EFFT.op", error);
}

int Kotonoha_KtrfGetEffectValue(const Kotonoha_KtrfDocument *document,
                                uint32_t effect_index,
                                uint32_t local_value_index,
                                Kotonoha_KtrfValueRef *out,
                                Kotonoha_KtrfError *error) {
  TypedPool6View view;
  Kotonoha_KtrfEffect effect;
  const Kotonoha_KtrfSection *vars, *expr, *rsrc, *hook, *endg;
  const uint8_t *p;
  typed_clear(error);
  if (!typed_pool6_view(document, "EFFT", 32u, 24u, &view, error) ||
      !Kotonoha_KtrfGetEffect(document, effect_index, &effect, error))
    return 0;
  if (local_value_index >= effect.value_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "EFFT local value index out of range");
  vars = typed_section(document, "VARS", error);
  expr = typed_section(document, "EXPR", error);
  rsrc = typed_section(document, "RSRC", error);
  hook = typed_section(document, "HOOK", error);
  endg = typed_section(document, "ENDG", error);
  if (!vars || !expr || !rsrc || !hook || !endg)
    return 0;
  p = view.section->payload + view.pool_offset +
      (uint64_t)(effect.value_start + local_value_index) * 24u;
  return typed_value_ref_at(document, p, vars->item_count, expr->item_count,
                            rsrc->item_count, hook->item_count,
                            endg->item_count, out, error);
}

int Kotonoha_KtrfGetChoice(const Kotonoha_KtrfDocument *document,
                           uint32_t index, Kotonoha_KtrfChoice *out,
                           Kotonoha_KtrfError *error) {
  TypedChoiceView view;
  const Kotonoha_KtrfSection *vars, *nodes;
  const uint8_t *p;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "CHOI output is null");
  if (!typed_choice_view(document, &view, error))
    return 0;
  if (index >= view.choice_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI record index %u is out of range", index);
  vars = typed_section(document, "VARS", error);
  nodes = typed_section(document, "NODE", error);
  if (!vars || !nodes)
    return 0;
  p = view.section->payload + view.choices_offset + (uint64_t)index * 32u;
  out->id_str = typed_u32(p + 0u);
  out->node_index = typed_u32(p + 4u);
  out->result_variable_index = typed_u32(p + 8u);
  out->routing_policy_str = typed_u32(p + 12u);
  out->option_start = typed_u32(p + 16u);
  out->option_count = typed_u32(p + 20u);
  out->timeout_index = typed_u32(p + 24u);
  if (typed_u32(p + 28u) != 0u)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "CHOI reserved field must be zero");
  if (out->node_index >= nodes->item_count ||
      out->result_variable_index >= vars->item_count ||
      !typed_range_u32(out->option_start, out->option_count, view.option_count) ||
      (out->timeout_index != KOTONOHA_KTRF_NULL_INDEX &&
       out->timeout_index >= view.timeout_count))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI record has out-of-range reference");
  if (out->option_count == 0u)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "CHOI must contain at least one option");
  return typed_string_index(document, out->id_str, "CHOI.id", error) &&
         typed_string_index(document, out->routing_policy_str,
                            "CHOI.routing_policy", error);
}

static int typed_choice_option_global(const Kotonoha_KtrfDocument *document,
                                      const TypedChoiceView *view,
                                      uint32_t option_index,
                                      Kotonoha_KtrfChoiceOption *out,
                                      Kotonoha_KtrfError *error) {
  const uint8_t *p;
  uint8_t value_kind, flags;
  uint16_t reserved;
  uint32_t a;
  uint64_t b;
  if (option_index >= view->option_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI option index out of range");
  p = view->section->payload + view->options_offset +
      (uint64_t)option_index * 36u;
  out->id_str = typed_u32(p + 0u);
  out->label_str = typed_u32(p + 4u);
  value_kind = p[8u];
  flags = p[9u];
  reserved = typed_u16(p + 10u);
  a = typed_u32(p + 12u);
  b = typed_u64(p + 16u);
  out->effect_start = typed_u32(p + 24u);
  out->effect_count = typed_u32(p + 28u);
  if (flags || reserved || typed_u32(p + 32u))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "CHOI option flags/reserved fields must be zero");
  if (!typed_range_u32(out->effect_start, out->effect_count, view->effect_count))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI option effect slice is out of range");
  if (!typed_string_index(document, out->id_str, "CHOI option id", error) ||
      !typed_optional_string_index(document, out->label_str,
                                   "CHOI option label", error))
    return 0;
  return typed_decode_scalar(document, value_kind, a, b, 0, &out->value, error);
}

int Kotonoha_KtrfGetChoiceOption(const Kotonoha_KtrfDocument *document,
                                 uint32_t choice_index,
                                 uint32_t local_option_index,
                                 Kotonoha_KtrfChoiceOption *out,
                                 Kotonoha_KtrfError *error) {
  TypedChoiceView view;
  Kotonoha_KtrfChoice choice;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "CHOI option output is null");
  if (!typed_choice_view(document, &view, error) ||
      !Kotonoha_KtrfGetChoice(document, choice_index, &choice, error))
    return 0;
  if (local_option_index >= choice.option_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI local option index out of range");
  return typed_choice_option_global(document, &view,
                                    choice.option_start + local_option_index,
                                    out, error);
}

static int typed_choice_timeout_global(const Kotonoha_KtrfDocument *document,
                                       const TypedChoiceView *view,
                                       uint32_t timeout_index,
                                       Kotonoha_KtrfChoiceTimeout *out,
                                       Kotonoha_KtrfError *error) {
  const uint8_t *p;
  uint8_t value_kind, flags;
  uint16_t reserved;
  uint32_t a;
  uint64_t b;
  if (timeout_index >= view->timeout_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI timeout index out of range");
  p = view->section->payload + view->timeouts_offset +
      (uint64_t)timeout_index * 28u;
  value_kind = p[0u];
  flags = p[1u];
  reserved = typed_u16(p + 2u);
  a = typed_u32(p + 4u);
  b = typed_u64(p + 8u);
  out->effect_start = typed_u32(p + 16u);
  out->effect_count = typed_u32(p + 20u);
  if (flags || reserved || typed_u32(p + 24u))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "CHOI timeout flags/reserved fields must be zero");
  if (!typed_range_u32(out->effect_start, out->effect_count, view->effect_count))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI timeout effect slice is out of range");
  return typed_decode_scalar(document, value_kind, a, b, 0, &out->value, error);
}

int Kotonoha_KtrfGetChoiceTimeout(const Kotonoha_KtrfDocument *document,
                                  uint32_t choice_index, int *has_timeout,
                                  Kotonoha_KtrfChoiceTimeout *out,
                                  Kotonoha_KtrfError *error) {
  TypedChoiceView view;
  Kotonoha_KtrfChoice choice;
  typed_clear(error);
  if (has_timeout == NULL || out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "CHOI timeout output is null");
  if (!typed_choice_view(document, &view, error) ||
      !Kotonoha_KtrfGetChoice(document, choice_index, &choice, error))
    return 0;
  if (choice.timeout_index == KOTONOHA_KTRF_NULL_INDEX) {
    *has_timeout = 0;
    memset(out, 0, sizeof(*out));
    return 1;
  }
  *has_timeout = 1;
  return typed_choice_timeout_global(document, &view, choice.timeout_index, out,
                                     error);
}

int Kotonoha_KtrfGetChoiceOptionEffect(const Kotonoha_KtrfDocument *document,
                                       uint32_t choice_index,
                                       uint32_t local_option_index,
                                       uint32_t local_effect_index,
                                       uint32_t *effect_index,
                                       Kotonoha_KtrfError *error) {
  TypedChoiceView view;
  Kotonoha_KtrfChoiceOption option;
  const Kotonoha_KtrfSection *effects;
  typed_clear(error);
  if (effect_index == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "CHOI effect output is null");
  if (!typed_choice_view(document, &view, error) ||
      !Kotonoha_KtrfGetChoiceOption(document, choice_index, local_option_index,
                                    &option, error))
    return 0;
  if (local_effect_index >= option.effect_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI local option effect index out of range");
  *effect_index = typed_u32(view.section->payload + view.effects_offset +
                            (uint64_t)(option.effect_start + local_effect_index) *
                                4u);
  effects = typed_section(document, "EFFT", error);
  if (effects == NULL || *effect_index >= effects->item_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI effect index out of range");
  return 1;
}

int Kotonoha_KtrfGetChoiceTimeoutEffect(const Kotonoha_KtrfDocument *document,
                                        uint32_t choice_index,
                                        uint32_t local_effect_index,
                                        uint32_t *effect_index,
                                        Kotonoha_KtrfError *error) {
  TypedChoiceView view;
  Kotonoha_KtrfChoiceTimeout timeout;
  const Kotonoha_KtrfSection *effects;
  int has_timeout;
  typed_clear(error);
  if (effect_index == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "CHOI timeout effect output is null");
  if (!typed_choice_view(document, &view, error) ||
      !Kotonoha_KtrfGetChoiceTimeout(document, choice_index, &has_timeout,
                                     &timeout, error))
    return 0;
  if (!has_timeout)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI has no timeout");
  if (local_effect_index >= timeout.effect_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI local timeout effect index out of range");
  *effect_index = typed_u32(view.section->payload + view.effects_offset +
                            (uint64_t)(timeout.effect_start + local_effect_index) *
                                4u);
  effects = typed_section(document, "EFFT", error);
  if (effects == NULL || *effect_index >= effects->item_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "CHOI timeout effect index out of range");
  return 1;
}

int Kotonoha_KtrfGetNode(const Kotonoha_KtrfDocument *document,
                         uint32_t index, Kotonoha_KtrfNode *out,
                         Kotonoha_KtrfError *error) {
  TypedPool6View view;
  const uint8_t *p;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "NODE output is null");
  if (!typed_pool6_view(document, "NODE", 32u, 4u, &view, error))
    return 0;
  if (index >= view.count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "NODE record index %u is out of range", index);
  p = view.section->payload + view.records_offset + (uint64_t)index * 32u;
  out->id_str = typed_u32(p + 0u);
  out->kind_str = typed_u32(p + 4u);
  out->label_str = typed_u32(p + 8u);
  out->resource_start = typed_u32(p + 12u);
  out->resource_count = typed_u32(p + 16u);
  if (typed_u32(p + 20u) || typed_u32(p + 24u) || typed_u32(p + 28u))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "NODE flags/reserved fields must be zero");
  if (!typed_range_u32(out->resource_start, out->resource_count,
                       view.pool_count))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "NODE resource slice is out of range");
  return typed_string_index(document, out->id_str, "NODE.id", error) &&
         typed_string_index(document, out->kind_str, "NODE.kind", error) &&
         typed_optional_string_index(document, out->label_str, "NODE.label",
                                     error);
}

int Kotonoha_KtrfGetNodeResource(const Kotonoha_KtrfDocument *document,
                                 uint32_t node_index,
                                 uint32_t local_resource_index,
                                 uint32_t *resource_index,
                                 Kotonoha_KtrfError *error) {
  TypedPool6View view;
  Kotonoha_KtrfNode node;
  const Kotonoha_KtrfSection *resources;
  typed_clear(error);
  if (resource_index == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "NODE resource output is null");
  if (!typed_pool6_view(document, "NODE", 32u, 4u, &view, error) ||
      !Kotonoha_KtrfGetNode(document, node_index, &node, error))
    return 0;
  if (local_resource_index >= node.resource_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "NODE local resource index out of range");
  *resource_index = typed_u32(view.section->payload + view.pool_offset +
                              (uint64_t)(node.resource_start +
                                         local_resource_index) *
                                  4u);
  resources = typed_section(document, "RSRC", error);
  if (resources == NULL || *resource_index >= resources->item_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "NODE resource index out of range");
  return 1;
}

int Kotonoha_KtrfGetTransition(const Kotonoha_KtrfDocument *document,
                               uint32_t index, Kotonoha_KtrfTransition *out,
                               Kotonoha_KtrfError *error) {
  TypedTranView view;
  const Kotonoha_KtrfSection *nodes, *exprs;
  const uint8_t *p;
  typed_clear(error);
  if (out == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "TRAN output is null");
  if (!typed_tran_view(document, &view, error))
    return 0;
  if (index >= view.count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "TRAN record index %u is out of range", index);
  nodes = typed_section(document, "NODE", error);
  exprs = typed_section(document, "EXPR", error);
  if (!nodes || !exprs)
    return 0;
  p = view.section->payload + 32u + (uint64_t)index * 48u;
  out->id_str = typed_u32(p + 0u);
  out->source_node_index = typed_u32(p + 4u);
  out->destination_node_index = typed_u32(p + 8u);
  out->priority = typed_u32(p + 12u);
  out->predicate_expression_index = typed_u32(p + 16u);
  out->effect_start = typed_u32(p + 20u);
  out->effect_count = typed_u32(p + 24u);
  out->trigger_start = typed_u32(p + 28u);
  out->trigger_count = typed_u32(p + 32u);
  out->flags = typed_u32(p + 36u);
  if (typed_u32(p + 40u) || typed_u32(p + 44u) ||
      (out->flags & ~KTRF_TRAN_FLAGS) != 0u)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "TRAN flags/reserved fields are invalid");
  if (out->source_node_index >= nodes->item_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "TRAN source node index out of range");
  if ((out->flags & KOTONOHA_KTRF_TRAN_TERMINAL) != 0u) {
    if (out->destination_node_index != KOTONOHA_KTRF_NULL_INDEX)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "terminal TRAN must not have destination");
  } else if (out->destination_node_index == KOTONOHA_KTRF_NULL_INDEX ||
             out->destination_node_index >= nodes->item_count) {
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "nonterminal TRAN destination is invalid");
  }
  if (out->predicate_expression_index != KOTONOHA_KTRF_NULL_INDEX &&
      out->predicate_expression_index >= exprs->item_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "TRAN predicate expression index out of range");
  if (!typed_range_u32(out->effect_start, out->effect_count,
                       view.effect_count) ||
      !typed_range_u32(out->trigger_start, out->trigger_count,
                       view.trigger_count))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "TRAN effect/trigger slice is out of range");
  if (((out->flags & KOTONOHA_KTRF_TRAN_HAS_TRIGGERS) != 0u) !=
      (out->trigger_count != 0u))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "TRAN HAS_TRIGGERS flag/count mismatch");
  return typed_string_index(document, out->id_str, "TRAN.id", error);
}

int Kotonoha_KtrfGetTransitionEffect(const Kotonoha_KtrfDocument *document,
                                     uint32_t transition_index,
                                     uint32_t local_effect_index,
                                     uint32_t *effect_index,
                                     Kotonoha_KtrfError *error) {
  TypedTranView view;
  Kotonoha_KtrfTransition transition;
  const Kotonoha_KtrfSection *effects;
  typed_clear(error);
  if (effect_index == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "TRAN effect output is null");
  if (!typed_tran_view(document, &view, error) ||
      !Kotonoha_KtrfGetTransition(document, transition_index, &transition,
                                  error))
    return 0;
  if (local_effect_index >= transition.effect_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "TRAN local effect index out of range");
  *effect_index = typed_u32(view.section->payload + view.effects_offset +
                            (uint64_t)(transition.effect_start +
                                       local_effect_index) *
                                4u);
  effects = typed_section(document, "EFFT", error);
  if (effects == NULL || *effect_index >= effects->item_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "TRAN effect index out of range");
  return 1;
}

int Kotonoha_KtrfGetTransitionTrigger(const Kotonoha_KtrfDocument *document,
                                      uint32_t transition_index,
                                      uint32_t local_trigger_index,
                                      uint32_t *trigger_string_index,
                                      Kotonoha_KtrfError *error) {
  TypedTranView view;
  Kotonoha_KtrfTransition transition;
  typed_clear(error);
  if (trigger_string_index == NULL)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "TRAN trigger output is null");
  if (!typed_tran_view(document, &view, error) ||
      !Kotonoha_KtrfGetTransition(document, transition_index, &transition,
                                  error))
    return 0;
  if (local_trigger_index >= transition.trigger_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "TRAN local trigger index out of range");
  *trigger_string_index =
      typed_u32(view.section->payload + view.triggers_offset +
                (uint64_t)(transition.trigger_start + local_trigger_index) * 4u);
  return typed_string_index(document, *trigger_string_index, "TRAN.trigger",
                            error);
}

static int typed_validate_sorted_id(uint32_t previous, uint32_t current,
                                    int have_previous,
                                    const Kotonoha_KtrfDocument *document,
                                    const char *label,
                                    Kotonoha_KtrfError *error) {
  if (have_previous && !typed_string_less(document, previous, current, error))
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "%s records are not in canonical id order", label);
  return 1;
}

static int typed_validate_expression_cycles(const Kotonoha_KtrfDocument *document,
                                            uint32_t count,
                                            Kotonoha_KtrfError *error) {
  uint32_t *indegree = NULL, *queue = NULL;
  uint32_t i, head = 0u, tail = 0u, visited = 0u;
  if (count == 0u)
    return 1;
  indegree = (uint32_t *)calloc(count, sizeof(uint32_t));
  queue = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
  if (!indegree || !queue) {
    free(indegree);
    free(queue);
    return typed_fail(error, KOTONOHA_KTRF_ERROR_MEMORY,
                      "out of memory validating EXPR acyclicity");
  }
  for (i = 0u; i < count; ++i) {
    Kotonoha_KtrfExpression expr;
    uint32_t j;
    if (!Kotonoha_KtrfGetExpression(document, i, &expr, error))
      goto fail;
    for (j = 0u; j < expr.arg_count; ++j) {
      Kotonoha_KtrfValueRef ref;
      if (!Kotonoha_KtrfGetExpressionArg(document, i, j, &ref, error))
        goto fail;
      if (ref.kind == KOTONOHA_KTRF_VALUE_EXPRESSION) {
        if (indegree[ref.index] == UINT32_MAX) {
          typed_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                     "EXPR indegree overflow");
          goto fail;
        }
        indegree[ref.index]++;
      }
    }
  }
  for (i = 0u; i < count; ++i)
    if (indegree[i] == 0u)
      queue[tail++] = i;
  while (head < tail) {
    uint32_t node = queue[head++];
    Kotonoha_KtrfExpression expr;
    uint32_t j;
    visited++;
    if (!Kotonoha_KtrfGetExpression(document, node, &expr, error))
      goto fail;
    for (j = 0u; j < expr.arg_count; ++j) {
      Kotonoha_KtrfValueRef ref;
      if (!Kotonoha_KtrfGetExpressionArg(document, node, j, &ref, error))
        goto fail;
      if (ref.kind == KOTONOHA_KTRF_VALUE_EXPRESSION) {
        if (indegree[ref.index] == 0u) {
          typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "EXPR acyclicity bookkeeping underflow");
          goto fail;
        }
        indegree[ref.index]--;
        if (indegree[ref.index] == 0u)
          queue[tail++] = ref.index;
      }
    }
  }
  free(indegree);
  free(queue);
  if (visited != count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "Expression reference cycle is not allowed");
  return 1;
fail:
  free(indegree);
  free(queue);
  return 0;
}

int Kotonoha_KtrfValidateTypedCore(const Kotonoha_KtrfDocument *document,
                                   Kotonoha_KtrfError *error) {
  const Kotonoha_KtrfSection *nspc, *feat, *entr, *vars, *rsrc, *hook, *endg,
      *expr, *efft, *choi, *node, *tran;
  Kotonoha_KtrfMeta meta;
  uint32_t i, previous, expected_start;
  int have_previous;
  TypedPool6View expr_view, efft_view, node_view;
  TypedChoiceView choi_view;
  TypedTranView tran_view;

  typed_clear(error);
  if (!Kotonoha_KtrfValidateFullCore(document, error) ||
      !Kotonoha_KtrfGetMeta(document, &meta, error))
    return 0;
  (void)meta;

  nspc = typed_section(document, "NSPC", error);
  feat = typed_section(document, "FEAT", error);
  entr = typed_section(document, "ENTR", error);
  vars = typed_section(document, "VARS", error);
  rsrc = typed_section(document, "RSRC", error);
  hook = typed_section(document, "HOOK", error);
  endg = typed_section(document, "ENDG", error);
  expr = typed_section(document, "EXPR", error);
  efft = typed_section(document, "EFFT", error);
  choi = typed_section(document, "CHOI", error);
  node = typed_section(document, "NODE", error);
  tran = typed_section(document, "TRAN", error);
  if (!nspc || !feat || !entr || !vars || !rsrc || !hook || !endg || !expr ||
      !efft || !choi || !node || !tran)
    return 0;

  previous = 0u;
  have_previous = 0;
  for (i = 0u; i < nspc->item_count; ++i) {
    Kotonoha_KtrfNamespace rec;
    if (!Kotonoha_KtrfGetNamespace(document, i, &rec, error))
      return 0;
    if (have_previous &&
        !typed_string_less(document, previous, rec.prefix_str, error))
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "NSPC records are not in canonical prefix order");
    previous = rec.prefix_str;
    have_previous = 1;
  }

  {
    uint32_t prev_id = 0u, prev_rank = 0u;
    int have = 0;
    for (i = 0u; i < feat->item_count; ++i) {
      Kotonoha_KtrfFeature rec;
      uint32_t rank;
      if (!Kotonoha_KtrfGetFeature(document, i, &rec, error))
        return 0;
      rank = rec.flags == KOTONOHA_KTRF_FEATURE_REQUIRED ? 0u : 1u;
      if (have) {
        int ordered = 0;
        if (rank > prev_rank)
          ordered = 1;
        else if (rank == prev_rank) {
          if (prev_id == rec.id_str)
            return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                              "duplicate FEAT id within feature class");
          if (typed_string_less(document, prev_id, rec.id_str, error))
            ordered = 1;
        }
        if (!ordered)
          return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "FEAT records are not in canonical order");
      }
      prev_id = rec.id_str;
      prev_rank = rank;
      have = 1;
    }
  }

  previous = 0u;
  have_previous = 0;
  for (i = 0u; i < entr->item_count; ++i) {
    Kotonoha_KtrfEntryPoint rec;
    if (!Kotonoha_KtrfGetEntryPoint(document, i, &rec, error) ||
        !typed_validate_sorted_id(previous, rec.id_str, have_previous, document,
                                  "ENTR", error))
      return 0;
    previous = rec.id_str;
    have_previous = 1;
  }

#define VALIDATE_SIMPLE_ID(SECTION_PTR, TYPE_NAME, GETTER, LABEL)                 \
  do {                                                                            \
    previous = 0u;                                                                \
    have_previous = 0;                                                            \
    for (i = 0u; i < (SECTION_PTR)->item_count; ++i) {                           \
      TYPE_NAME rec;                                                              \
      if (!GETTER(document, i, &rec, error) ||                                    \
          !typed_validate_sorted_id(previous, rec.id_str, have_previous, document,\
                                    LABEL, error))                                 \
        return 0;                                                                 \
      previous = rec.id_str;                                                      \
      have_previous = 1;                                                          \
    }                                                                             \
  } while (0)

  VALIDATE_SIMPLE_ID(vars, Kotonoha_KtrfVariable, Kotonoha_KtrfGetVariable,
                     "VARS");
  VALIDATE_SIMPLE_ID(rsrc, Kotonoha_KtrfResource, Kotonoha_KtrfGetResource,
                     "RSRC");
  VALIDATE_SIMPLE_ID(hook, Kotonoha_KtrfHook, Kotonoha_KtrfGetHook, "HOOK");
  VALIDATE_SIMPLE_ID(endg, Kotonoha_KtrfEnding, Kotonoha_KtrfGetEnding, "ENDG");
  VALIDATE_SIMPLE_ID(expr, Kotonoha_KtrfExpression,
                     Kotonoha_KtrfGetExpression, "EXPR");
  VALIDATE_SIMPLE_ID(efft, Kotonoha_KtrfEffect, Kotonoha_KtrfGetEffect, "EFFT");
  VALIDATE_SIMPLE_ID(choi, Kotonoha_KtrfChoice, Kotonoha_KtrfGetChoice, "CHOI");
  VALIDATE_SIMPLE_ID(node, Kotonoha_KtrfNode, Kotonoha_KtrfGetNode, "NODE");
  VALIDATE_SIMPLE_ID(tran, Kotonoha_KtrfTransition,
                     Kotonoha_KtrfGetTransition, "TRAN");
#undef VALIDATE_SIMPLE_ID

  if (!typed_pool6_view(document, "EXPR", 32u, 24u, &expr_view, error))
    return 0;
  expected_start = 0u;
  for (i = 0u; i < expr_view.count; ++i) {
    Kotonoha_KtrfExpression rec;
    uint32_t j;
    if (!Kotonoha_KtrfGetExpression(document, i, &rec, error))
      return 0;
    if (rec.arg_start != expected_start)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "EXPR argument slices are not canonical/contiguous");
    expected_start += rec.arg_count;
    for (j = 0u; j < rec.arg_count; ++j) {
      Kotonoha_KtrfValueRef value;
      if (!Kotonoha_KtrfGetExpressionArg(document, i, j, &value, error))
        return 0;
    }
  }
  if (expected_start != expr_view.pool_count ||
      !typed_validate_expression_cycles(document, expr_view.count, error))
    return 0;

  if (!typed_pool6_view(document, "EFFT", 32u, 24u, &efft_view, error))
    return 0;
  expected_start = 0u;
  for (i = 0u; i < efft_view.count; ++i) {
    Kotonoha_KtrfEffect rec;
    uint32_t j;
    const char *expected_op = NULL;
    if (!Kotonoha_KtrfGetEffect(document, i, &rec, error))
      return 0;
    if (rec.value_start != expected_start)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "EFFT value slices are not canonical/contiguous");
    expected_start += rec.value_count;
    switch (rec.shape) {
    case KOTONOHA_KTRF_EFFECT_SET:
      expected_op = "ktrf:set";
      if (rec.payload_a >= vars->item_count || rec.payload_b != 0u ||
          rec.value_count != 1u)
        return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                          "invalid set EFFT payload");
      break;
    case KOTONOHA_KTRF_EFFECT_ADD:
      expected_op = "ktrf:add";
      if (rec.payload_a >= vars->item_count || rec.payload_b != 0u ||
          rec.value_count != 1u)
        return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                          "invalid add EFFT payload");
      break;
    case KOTONOHA_KTRF_EFFECT_COPY:
      expected_op = "ktrf:copy";
      if (rec.payload_a >= vars->item_count ||
          rec.payload_b >= vars->item_count || rec.value_count != 0u)
        return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                          "invalid copy EFFT payload");
      break;
    case KOTONOHA_KTRF_EFFECT_REGISTER_ENDING:
      expected_op = "ktrf:register-ending";
      if (rec.payload_a >= endg->item_count || rec.payload_b != 0u ||
          rec.value_count != 0u)
        return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                          "invalid register-ending EFFT payload");
      break;
    case KOTONOHA_KTRF_EFFECT_CALL_HOOK:
      expected_op = "ktrf:call-hook";
      if (rec.payload_a >= hook->item_count || rec.payload_b != 0u)
        return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                          "invalid call-hook EFFT payload");
      break;
    default:
      return typed_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                        "unknown EFFT shape %u", (unsigned)rec.shape);
    }
    if (!typed_string_equals_literal(document, rec.op_str, expected_op, error))
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "EFFT shape/op mismatch");
    for (j = 0u; j < rec.value_count; ++j) {
      Kotonoha_KtrfValueRef value;
      if (!Kotonoha_KtrfGetEffectValue(document, i, j, &value, error))
        return 0;
    }
  }
  if (expected_start != efft_view.pool_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "EFFT values are not fully owned by effects");

  if (!typed_choice_view(document, &choi_view, error))
    return 0;
  {
    uint32_t expected_option = 0u, expected_timeout = 0u, expected_effect = 0u;
    for (i = 0u; i < choi_view.choice_count; ++i) {
      Kotonoha_KtrfChoice c;
      uint32_t j, k;
      int has_timeout;
      Kotonoha_KtrfChoiceTimeout timeout;
      if (!Kotonoha_KtrfGetChoice(document, i, &c, error))
        return 0;
      if (c.option_start != expected_option)
        return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                          "CHOI option slices are not canonical/contiguous");
      expected_option += c.option_count;
      for (j = 0u; j < c.option_count; ++j) {
        Kotonoha_KtrfChoiceOption option;
        uint32_t m;
        if (!Kotonoha_KtrfGetChoiceOption(document, i, j, &option, error))
          return 0;
        if (option.effect_start != expected_effect)
          return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "CHOI effect slices are not canonical/contiguous");
        expected_effect += option.effect_count;
        for (m = 0u; m < j; ++m) {
          Kotonoha_KtrfChoiceOption earlier;
          if (!Kotonoha_KtrfGetChoiceOption(document, i, m, &earlier, error))
            return 0;
          if (earlier.id_str == option.id_str)
            return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                              "duplicate CHOI option id");
        }
        for (k = 0u; k < option.effect_count; ++k) {
          uint32_t effect_index;
          if (!Kotonoha_KtrfGetChoiceOptionEffect(document, i, j, k,
                                                  &effect_index, error))
            return 0;
        }
      }
      if (!Kotonoha_KtrfGetChoiceTimeout(document, i, &has_timeout, &timeout,
                                         error))
        return 0;
      if (has_timeout) {
        if (c.timeout_index != expected_timeout ||
            timeout.effect_start != expected_effect)
          return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "CHOI timeout/effect pools are not canonical");
        expected_timeout++;
        expected_effect += timeout.effect_count;
        for (k = 0u; k < timeout.effect_count; ++k) {
          uint32_t effect_index;
          if (!Kotonoha_KtrfGetChoiceTimeoutEffect(document, i, k,
                                                   &effect_index, error))
            return 0;
        }
      }
    }
    if (expected_option != choi_view.option_count ||
        expected_timeout != choi_view.timeout_count ||
        expected_effect != choi_view.effect_count)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "CHOI pools are not fully owned by choices");
  }

  if (!typed_pool6_view(document, "NODE", 32u, 4u, &node_view, error))
    return 0;
  expected_start = 0u;
  for (i = 0u; i < node_view.count; ++i) {
    Kotonoha_KtrfNode rec;
    uint32_t j;
    if (!Kotonoha_KtrfGetNode(document, i, &rec, error))
      return 0;
    if (rec.resource_start != expected_start)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "NODE resource slices are not canonical/contiguous");
    expected_start += rec.resource_count;
    for (j = 0u; j < rec.resource_count; ++j) {
      uint32_t resource_index;
      if (!Kotonoha_KtrfGetNodeResource(document, i, j, &resource_index, error))
        return 0;
    }
  }
  if (expected_start != node_view.pool_count)
    return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "NODE resource refs are not fully owned by nodes");

  if (!typed_tran_view(document, &tran_view, error))
    return 0;
  {
    uint32_t expected_effect = 0u, expected_trigger = 0u;
    for (i = 0u; i < tran_view.count; ++i) {
      Kotonoha_KtrfTransition rec;
      uint32_t j, k;
      if (!Kotonoha_KtrfGetTransition(document, i, &rec, error))
        return 0;
      if (rec.effect_start != expected_effect ||
          rec.trigger_start != expected_trigger)
        return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                          "TRAN pool slices are not canonical/contiguous");
      expected_effect += rec.effect_count;
      expected_trigger += rec.trigger_count;
      for (j = 0u; j < rec.effect_count; ++j) {
        uint32_t effect_index;
        if (!Kotonoha_KtrfGetTransitionEffect(document, i, j, &effect_index,
                                              error))
          return 0;
      }
      for (j = 0u; j < rec.trigger_count; ++j) {
        uint32_t trigger_index;
        if (!Kotonoha_KtrfGetTransitionTrigger(document, i, j, &trigger_index,
                                               error))
          return 0;
        for (k = 0u; k < j; ++k) {
          uint32_t earlier;
          if (!Kotonoha_KtrfGetTransitionTrigger(document, i, k, &earlier,
                                                 error))
            return 0;
          if (earlier == trigger_index)
            return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                              "TRAN contains duplicate trigger");
        }
      }
    }
    if (expected_effect != tran_view.effect_count ||
        expected_trigger != tran_view.trigger_count)
      return typed_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "TRAN pools are not fully owned by transitions");
  }

  return 1;
}
