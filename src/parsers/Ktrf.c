#include "Kotonoha/parsers/Ktrf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KTRF_MAX_ALIGNMENT 4096u
#define KTRF_SECTION_FLAGS_KNOWN                                             \
  (KOTONOHA_KTRF_SECTION_REQUIRED | KOTONOHA_KTRF_SECTION_COMPRESSED |      \
   KOTONOHA_KTRF_SECTION_NON_SEMANTIC)

static const char *const KTRF_SECTION_ORDER[] = {
    "META", "STRS", "NSPC", "FEAT", "ENTR", "VARS", "RSRC", "HOOK",
    "ENDG", "EXPR", "EFFT", "CHOI", "NODE", "TRAN", "EXTN", "DBUG"};
static const size_t KTRF_SECTION_ORDER_COUNT =
    sizeof(KTRF_SECTION_ORDER) / sizeof(KTRF_SECTION_ORDER[0]);
static const size_t KTRF_FULL_CORE_COUNT = 14u;

static void ktrf_clear_error(Kotonoha_KtrfError *error) {
  if (error != NULL) {
    error->code = KOTONOHA_KTRF_OK;
    error->message[0] = '\0';
  }
}

static int ktrf_fail(Kotonoha_KtrfError *error, Kotonoha_KtrfErrorCode code,
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

static uint16_t ktrf_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t ktrf_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint64_t ktrf_u64(const uint8_t *p) {
  return (uint64_t)ktrf_u32(p) | ((uint64_t)ktrf_u32(p + 4) << 32);
}

static int ktrf_all_zero(const uint8_t *p, size_t size) {
  size_t i;
  for (i = 0; i < size; ++i) {
    if (p[i] != 0u)
      return 0;
  }
  return 1;
}

static int ktrf_is_power_of_two(uint32_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static uint32_t ktrf_crc32(const uint8_t *data, size_t size) {
  uint32_t crc = UINT32_C(0xFFFFFFFF);
  size_t i;
  for (i = 0; i < size; ++i) {
    uint32_t x = (crc ^ data[i]) & 0xFFu;
    unsigned bit;
    for (bit = 0; bit < 8u; ++bit)
      x = (x >> 1) ^ (UINT32_C(0xEDB88320) & (0u - (x & 1u)));
    crc = (crc >> 8) ^ x;
  }
  return crc ^ UINT32_C(0xFFFFFFFF);
}

typedef struct KtrfSha256 {
  uint32_t state[8];
  uint64_t bit_count;
  uint8_t buffer[64];
  size_t buffer_size;
} KtrfSha256;

static uint32_t ktrf_rotr32(uint32_t x, unsigned n) {
  return (x >> n) | (x << (32u - n));
}

static uint32_t ktrf_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void ktrf_sha256_transform(KtrfSha256 *ctx, const uint8_t block[64]) {
  static const uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
      0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
      0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
      0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
      0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
      0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
      0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
      0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
      0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
      0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
  uint32_t w[64];
  uint32_t a, b, c, d, e, f, g, h;
  unsigned i;

  for (i = 0; i < 16u; ++i)
    w[i] = ktrf_be32(block + i * 4u);
  for (i = 16u; i < 64u; ++i) {
    uint32_t s0 = ktrf_rotr32(w[i - 15u], 7) ^
                  ktrf_rotr32(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
    uint32_t s1 = ktrf_rotr32(w[i - 2u], 17) ^
                  ktrf_rotr32(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
    w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
  }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];

  for (i = 0; i < 64u; ++i) {
    uint32_t s1 = ktrf_rotr32(e, 6) ^ ktrf_rotr32(e, 11) ^ ktrf_rotr32(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t t1 = h + s1 + ch + k[i] + w[i];
    uint32_t s0 = ktrf_rotr32(a, 2) ^ ktrf_rotr32(a, 13) ^ ktrf_rotr32(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void ktrf_sha256_init(KtrfSha256 *ctx) {
  static const uint32_t initial[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                      0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                      0x1f83d9abu, 0x5be0cd19u};
  memcpy(ctx->state, initial, sizeof(initial));
  ctx->bit_count = 0u;
  ctx->buffer_size = 0u;
}

static void ktrf_sha256_update(KtrfSha256 *ctx, const uint8_t *data,
                               size_t size) {
  while (size != 0u) {
    size_t space = sizeof(ctx->buffer) - ctx->buffer_size;
    size_t take = size < space ? size : space;
    memcpy(ctx->buffer + ctx->buffer_size, data, take);
    ctx->buffer_size += take;
    data += take;
    size -= take;
    if (ctx->buffer_size == sizeof(ctx->buffer)) {
      ktrf_sha256_transform(ctx, ctx->buffer);
      ctx->bit_count += 512u;
      ctx->buffer_size = 0u;
    }
  }
}

static void ktrf_sha256_final(KtrfSha256 *ctx, uint8_t digest[32]) {
  uint64_t total_bits = ctx->bit_count + (uint64_t)ctx->buffer_size * 8u;
  size_t i;

  ctx->buffer[ctx->buffer_size++] = 0x80u;
  if (ctx->buffer_size > 56u) {
    while (ctx->buffer_size < 64u)
      ctx->buffer[ctx->buffer_size++] = 0u;
    ktrf_sha256_transform(ctx, ctx->buffer);
    ctx->buffer_size = 0u;
  }
  while (ctx->buffer_size < 56u)
    ctx->buffer[ctx->buffer_size++] = 0u;
  for (i = 0; i < 8u; ++i)
    ctx->buffer[56u + i] = (uint8_t)(total_bits >> (56u - (unsigned)i * 8u));
  ktrf_sha256_transform(ctx, ctx->buffer);

  for (i = 0; i < 8u; ++i) {
    digest[i * 4u + 0u] = (uint8_t)(ctx->state[i] >> 24);
    digest[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16);
    digest[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8);
    digest[i * 4u + 3u] = (uint8_t)ctx->state[i];
  }
}

static void ktrf_sha256(const uint8_t *data, size_t size, uint8_t digest[32]) {
  KtrfSha256 ctx;
  ktrf_sha256_init(&ctx);
  ktrf_sha256_update(&ctx, data, size);
  ktrf_sha256_final(&ctx, digest);
}

static int ktrf_fourcc_printable(const uint8_t type[4]) {
  unsigned i;
  for (i = 0; i < 4u; ++i) {
    if (type[i] < 0x20u || type[i] > 0x7Eu)
      return 0;
  }
  return 1;
}

static int ktrf_type_rank(const uint8_t type[4]) {
  size_t i;
  for (i = 0; i < KTRF_SECTION_ORDER_COUNT; ++i) {
    if (memcmp(type, KTRF_SECTION_ORDER[i], 4u) == 0)
      return (int)i;
  }
  return (int)KTRF_SECTION_ORDER_COUNT;
}

static int ktrf_type_known(const uint8_t type[4]) {
  return ktrf_type_rank(type) < (int)KTRF_SECTION_ORDER_COUNT;
}

static int ktrf_type_compare_canonical(const uint8_t a[4],
                                       const uint8_t b[4]) {
  int ra = ktrf_type_rank(a);
  int rb = ktrf_type_rank(b);
  if (ra != rb)
    return ra < rb ? -1 : 1;
  return memcmp(a, b, 4u);
}

static int ktrf_range_fits(uint64_t offset, uint64_t length, size_t total) {
  uint64_t total64 = (uint64_t)total;
  return offset <= total64 && length <= total64 - offset;
}

static int ktrf_validate_utf8(const uint8_t *data, size_t size) {
  size_t i = 0u;
  while (i < size) {
    uint8_t c = data[i++];
    uint32_t cp;
    unsigned need;
    if (c <= 0x7Fu) {
      if (c == 0u)
        return 0;
      continue;
    }
    if (c >= 0xC2u && c <= 0xDFu) {
      cp = c & 0x1Fu;
      need = 1u;
    } else if (c >= 0xE0u && c <= 0xEFu) {
      cp = c & 0x0Fu;
      need = 2u;
    } else if (c >= 0xF0u && c <= 0xF4u) {
      cp = c & 0x07u;
      need = 3u;
    } else {
      return 0;
    }
    if (i + need > size)
      return 0;
    while (need-- != 0u) {
      uint8_t t = data[i++];
      if ((t & 0xC0u) != 0x80u)
        return 0;
      cp = (cp << 6) | (uint32_t)(t & 0x3Fu);
    }
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
      return 0;
    if ((cp <= 0x7FFu && c >= 0xE0u) || (cp <= 0xFFFFu && c >= 0xF0u))
      return 0;
  }
  return 1;
}

static int ktrf_slice_compare(const uint8_t *a, size_t a_size, const uint8_t *b,
                              size_t b_size) {
  size_t common = a_size < b_size ? a_size : b_size;
  int cmp = common != 0u ? memcmp(a, b, common) : 0;
  if (cmp != 0)
    return cmp;
  if (a_size == b_size)
    return 0;
  return a_size < b_size ? -1 : 1;
}

static int ktrf_validate_strs(Kotonoha_KtrfDocument *doc,
                              Kotonoha_KtrfError *error) {
  const Kotonoha_KtrfSection *section =
      Kotonoha_KtrfFindSection(doc, "STRS");
  const uint8_t *payload;
  uint32_t count, bytes;
  uint64_t table_bytes, data_offset;
  uint32_t i;
  const uint8_t *previous = NULL;
  size_t previous_size = 0u;

  if (section == NULL)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_MISSING_SECTION,
                     "missing required STRS section");
  payload = section->payload;
  if (section->stored_size < 12u)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "STRS payload is too small");
  count = ktrf_u32(payload + 0u);
  bytes = ktrf_u32(payload + 4u);
  table_bytes = ((uint64_t)count + 1u) * 4u;
  data_offset = 8u + table_bytes;
  if (data_offset > section->stored_size ||
      (uint64_t)bytes != section->stored_size - data_offset)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "STRS size fields do not match payload length");
  if (count == 0u)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "STRS must contain index 0 empty string");
  if (ktrf_u32(payload + 8u) != 0u ||
      ktrf_u32(payload + 8u + (uint64_t)count * 4u) != bytes)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "STRS offset endpoints are invalid");

  for (i = 0u; i < count; ++i) {
    uint32_t start = ktrf_u32(payload + 8u + (uint64_t)i * 4u);
    uint32_t end = ktrf_u32(payload + 8u + ((uint64_t)i + 1u) * 4u);
    const uint8_t *raw;
    size_t raw_size;
    if (start > end || end > bytes)
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "STRS offsets are not monotonic/in range");
    raw = payload + data_offset + start;
    raw_size = (size_t)(end - start);
    if (i == 0u) {
      if (raw_size != 0u)
        return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                         "STRS index 0 must be empty");
    } else {
      if (raw_size == 0u || !ktrf_validate_utf8(raw, raw_size))
        return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                         "STRS contains empty/invalid UTF-8 string at index %u",
                         i);
      if (previous != NULL &&
          ktrf_slice_compare(previous, previous_size, raw, raw_size) >= 0)
        return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                         "STRS is not in strict canonical UTF-8 byte order");
    }
    previous = raw;
    previous_size = raw_size;
  }

  if (section->item_count != count)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "STRS item_count does not match string_count");

  doc->strs_payload = payload;
  doc->strs_size = section->stored_size;
  doc->string_count = count;
  doc->string_bytes = bytes;
  doc->string_offsets = payload + 8u;
  doc->string_blob = payload + data_offset;
  return 1;
}

static int ktrf_section_ptr_compare_offset(const void *a, const void *b) {
  const Kotonoha_KtrfSection *const sa =
      *(const Kotonoha_KtrfSection *const *)a;
  const Kotonoha_KtrfSection *const sb =
      *(const Kotonoha_KtrfSection *const *)b;
  if (sa->offset < sb->offset)
    return -1;
  if (sa->offset > sb->offset)
    return 1;
  return 0;
}

static int ktrf_parse_owned(Kotonoha_KtrfDocument *doc,
                            Kotonoha_KtrfError *error) {
  const uint8_t *data = doc->bytes;
  size_t size = doc->size;
  uint16_t major, minor, header_size, section_entry_size;
  uint32_t flags, section_count, reserved0, header_crc, expected_crc;
  uint64_t directory_offset, file_size, directory_end;
  uint8_t byte_order, hash_algorithm;
  uint16_t reserved1;
  uint8_t digest[32];
  uint8_t header_copy[KOTONOHA_KTRF_HEADER_SIZE];
  uint32_t i;
  uint64_t cursor;

  if (size < KOTONOHA_KTRF_HEADER_SIZE)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "file is smaller than 96-byte KTRF header");
  if (memcmp(data, "KTRF", 4u) != 0)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT, "bad KTRF magic");

  major = ktrf_u16(data + 4u);
  minor = ktrf_u16(data + 6u);
  header_size = ktrf_u16(data + 8u);
  section_entry_size = ktrf_u16(data + 10u);
  flags = ktrf_u32(data + 12u);
  section_count = ktrf_u32(data + 16u);
  reserved0 = ktrf_u32(data + 20u);
  directory_offset = ktrf_u64(data + 24u);
  file_size = ktrf_u64(data + 32u);
  byte_order = data[40u];
  hash_algorithm = data[41u];
  reserved1 = ktrf_u16(data + 42u);
  header_crc = ktrf_u32(data + 44u);

  if (major != KOTONOHA_KTRF_FORMAT_MAJOR ||
      minor != KOTONOHA_KTRF_FORMAT_MINOR)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                     "unsupported KTRF binary version %u.%u", major, minor);
  if (header_size != KOTONOHA_KTRF_HEADER_SIZE ||
      section_entry_size != KOTONOHA_KTRF_SECTION_ENTRY_SIZE)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                     "unexpected KTRF header/directory record size");
  if (flags != 0u || reserved0 != 0u || reserved1 != 0u ||
      !ktrf_all_zero(data + 80u, 16u))
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "KTRF header flags/reserved fields are not canonical");
  if (directory_offset != KOTONOHA_KTRF_HEADER_SIZE)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "KTRF v0.1 directory must start at byte 96");
  if (file_size != (uint64_t)size)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "KTRF file_size does not match actual size");
  if (byte_order != 1u || hash_algorithm != 1u)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                     "unsupported KTRF byte order/hash algorithm");

  memcpy(header_copy, data, sizeof(header_copy));
  memset(header_copy + 44u, 0, 4u);
  expected_crc = ktrf_crc32(header_copy, sizeof(header_copy));
  if (header_crc != expected_crc)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_INTEGRITY,
                     "KTRF header CRC32 mismatch");

  ktrf_sha256(data + KOTONOHA_KTRF_HEADER_SIZE,
              size - KOTONOHA_KTRF_HEADER_SIZE, digest);
  if (memcmp(digest, data + 48u, 32u) != 0)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_INTEGRITY,
                     "KTRF content SHA-256 mismatch");

  if ((uint64_t)section_count >
      (UINT64_MAX - directory_offset) / KOTONOHA_KTRF_SECTION_ENTRY_SIZE)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                     "KTRF directory size overflow");
  directory_end = directory_offset +
                  (uint64_t)section_count * KOTONOHA_KTRF_SECTION_ENTRY_SIZE;
  if (directory_end > file_size)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "KTRF section directory extends beyond file");

  if (section_count != 0u) {
    doc->sections = (Kotonoha_KtrfSection *)calloc(
        section_count, sizeof(Kotonoha_KtrfSection));
    if (doc->sections == NULL)
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_MEMORY,
                       "out of memory allocating KTRF section directory");
  }

  for (i = 0u; i < section_count; ++i) {
    const uint8_t *entry = data + directory_offset +
                           (uint64_t)i * KOTONOHA_KTRF_SECTION_ENTRY_SIZE;
    Kotonoha_KtrfSection *dst = &doc->sections[i];
    uint32_t section_flags = ktrf_u32(entry + 4u);
    uint64_t payload_offset = ktrf_u64(entry + 8u);
    uint64_t stored_size = ktrf_u64(entry + 16u);
    uint64_t decoded_size = ktrf_u64(entry + 24u);
    uint32_t item_count = ktrf_u32(entry + 32u);
    uint32_t alignment = ktrf_u32(entry + 36u);
    uint32_t crc = ktrf_u32(entry + 40u);
    uint32_t reserved = ktrf_u32(entry + 44u);
    uint32_t j;

    if (!ktrf_fourcc_printable(entry))
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "KTRF section %u has non-printable FourCC", i);
    for (j = 0u; j < i; ++j) {
      if (memcmp(doc->sections[j].type, entry, 4u) == 0)
        return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                         "duplicate KTRF section %.4s", (const char *)entry);
    }
    if (i != 0u &&
        ktrf_type_compare_canonical((const uint8_t *)doc->sections[i - 1u].type,
                                    entry) >= 0)
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "KTRF section directory is not canonical");
    if ((section_flags & ~KTRF_SECTION_FLAGS_KNOWN) != 0u)
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "KTRF section %.4s has unknown flags", (const char *)entry);
    if ((section_flags & KOTONOHA_KTRF_SECTION_COMPRESSED) != 0u)
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                       "compressed KTRF sections are unsupported in v0.1");
    if ((section_flags & KOTONOHA_KTRF_SECTION_REQUIRED) != 0u &&
        !ktrf_type_known(entry))
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                       "unknown required KTRF section %.4s", (const char *)entry);
    if (reserved != 0u || !ktrf_is_power_of_two(alignment) ||
        alignment > KTRF_MAX_ALIGNMENT || payload_offset % alignment != 0u)
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "KTRF section %.4s has invalid reserved/alignment fields",
                       (const char *)entry);
    if (decoded_size != stored_size)
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_UNSUPPORTED,
                       "KTRF section %.4s decoded_size differs without compression",
                       (const char *)entry);
    if (payload_offset < directory_end ||
        !ktrf_range_fits(payload_offset, stored_size, size))
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "KTRF section %.4s payload is out of bounds", (const char *)entry);

    memcpy(dst->type, entry, 4u);
    dst->type[4] = '\0';
    dst->flags = section_flags;
    dst->offset = payload_offset;
    dst->stored_size = stored_size;
    dst->decoded_size = decoded_size;
    dst->item_count = item_count;
    dst->alignment = alignment;
    dst->crc32 = crc;
    dst->payload = data + payload_offset;
  }

  cursor = directory_end;
  if (section_count != 0u) {
    Kotonoha_KtrfSection **by_offset = (Kotonoha_KtrfSection **)malloc(
        (size_t)section_count * sizeof(*by_offset));
    if (by_offset == NULL)
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_MEMORY,
                       "out of memory sorting KTRF section payloads");
    for (i = 0u; i < section_count; ++i)
      by_offset[i] = &doc->sections[i];
    qsort(by_offset, section_count, sizeof(*by_offset),
          ktrf_section_ptr_compare_offset);

    for (i = 0u; i < section_count; ++i) {
      const Kotonoha_KtrfSection *section = by_offset[i];
      uint64_t pos;
      uint32_t actual_crc;
      if (section->offset < cursor) {
        free(by_offset);
        return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                         "KTRF section %.4s overlaps previous data",
                         section->type);
      }
      for (pos = cursor; pos < section->offset; ++pos) {
        if (data[pos] != 0u) {
          free(by_offset);
          return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                           "non-zero KTRF section padding");
        }
      }
      actual_crc = ktrf_crc32(section->payload, (size_t)section->stored_size);
      if (actual_crc != section->crc32) {
        free(by_offset);
        return ktrf_fail(error, KOTONOHA_KTRF_ERROR_INTEGRITY,
                         "KTRF section %.4s CRC32 mismatch", section->type);
      }
      cursor = section->offset + section->stored_size;
    }
    free(by_offset);
  }
  if (cursor != file_size)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "canonical KTRF v0.1 must not contain trailing padding");

  doc->format_major = major;
  doc->format_minor = minor;
  doc->flags = flags;
  doc->section_count = section_count;
  memcpy(doc->content_sha256, data + 48u, 32u);

  if (!ktrf_validate_strs(doc, error))
    return 0;
  return 1;
}

void Kotonoha_KtrfInit(Kotonoha_KtrfDocument *document) {
  if (document != NULL)
    memset(document, 0, sizeof(*document));
}

void Kotonoha_KtrfClean(Kotonoha_KtrfDocument *document) {
  if (document == NULL)
    return;
  free(document->sections);
  free(document->bytes);
  memset(document, 0, sizeof(*document));
}

int Kotonoha_KtrfLoadMemoryCopy(const void *data, size_t size,
                                Kotonoha_KtrfDocument *out,
                                Kotonoha_KtrfError *error) {
  Kotonoha_KtrfDocument temp;
  ktrf_clear_error(error);
  if (data == NULL || out == NULL)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "KTRF data/out argument is null");
  memset(&temp, 0, sizeof(temp));
  if (size != 0u) {
    temp.bytes = (uint8_t *)malloc(size);
    if (temp.bytes == NULL)
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_MEMORY,
                       "out of memory copying KTRF input");
    memcpy(temp.bytes, data, size);
  }
  temp.size = size;
  if (!ktrf_parse_owned(&temp, error)) {
    Kotonoha_KtrfClean(&temp);
    return 0;
  }
  *out = temp;
  return 1;
}

int Kotonoha_KtrfLoadFile(const char *path, Kotonoha_KtrfDocument *out,
                          Kotonoha_KtrfError *error) {
  FILE *file;
  long length;
  uint8_t *bytes;
  size_t read_size;
  int ok;
  ktrf_clear_error(error);
  if (path == NULL || out == NULL)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "KTRF path/out argument is null");
  file = fopen(path, "rb");
  if (file == NULL)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_IO,
                     "failed to open KTRF file: %s", path);
  if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_IO,
                     "failed to determine KTRF file size");
  }
  bytes = length != 0 ? (uint8_t *)malloc((size_t)length) : NULL;
  if (length != 0 && bytes == NULL) {
    fclose(file);
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_MEMORY,
                     "out of memory reading KTRF file");
  }
  read_size = length != 0 ? fread(bytes, 1u, (size_t)length, file) : 0u;
  fclose(file);
  if (read_size != (size_t)length) {
    free(bytes);
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_IO,
                     "short read while loading KTRF file");
  }
  ok = Kotonoha_KtrfLoadMemoryCopy(bytes, (size_t)length, out, error);
  free(bytes);
  return ok;
}

const Kotonoha_KtrfSection *
Kotonoha_KtrfFindSection(const Kotonoha_KtrfDocument *document,
                         const char type[4]) {
  uint32_t i;
  if (document == NULL || type == NULL)
    return NULL;
  for (i = 0u; i < document->section_count; ++i) {
    if (memcmp(document->sections[i].type, type, 4u) == 0)
      return &document->sections[i];
  }
  return NULL;
}

int Kotonoha_KtrfGetString(const Kotonoha_KtrfDocument *document,
                           uint32_t index, const char **data, size_t *length,
                           Kotonoha_KtrfError *error) {
  uint32_t start, end;
  ktrf_clear_error(error);
  if (document == NULL || data == NULL || length == NULL)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "KTRF string arguments are null");
  if (index >= document->string_count)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                     "KTRF string index %u is out of range", index);
  start = ktrf_u32(document->string_offsets + (uint64_t)index * 4u);
  end = ktrf_u32(document->string_offsets + ((uint64_t)index + 1u) * 4u);
  *data = (const char *)(document->string_blob + start);
  *length = (size_t)(end - start);
  return 1;
}

int Kotonoha_KtrfValidateFullCore(const Kotonoha_KtrfDocument *document,
                                  Kotonoha_KtrfError *error) {
  size_t i;
  ktrf_clear_error(error);
  if (document == NULL)
    return ktrf_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                     "KTRF document is null");
  for (i = 0u; i < KTRF_FULL_CORE_COUNT; ++i) {
    const Kotonoha_KtrfSection *section =
        Kotonoha_KtrfFindSection(document, KTRF_SECTION_ORDER[i]);
    if (section == NULL)
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_MISSING_SECTION,
                       "missing KTRF full-core section %s",
                       KTRF_SECTION_ORDER[i]);
    if ((section->flags & KOTONOHA_KTRF_SECTION_REQUIRED) == 0u)
      return ktrf_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                       "KTRF full-core section %s is not marked required",
                       KTRF_SECTION_ORDER[i]);
  }
  return 1;
}
