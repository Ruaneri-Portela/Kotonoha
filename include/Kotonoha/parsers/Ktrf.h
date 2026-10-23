#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KOTONOHA_KTRF_FORMAT_MAJOR 0u
#define KOTONOHA_KTRF_FORMAT_MINOR 1u
#define KOTONOHA_KTRF_HEADER_SIZE 96u
#define KOTONOHA_KTRF_SECTION_ENTRY_SIZE 48u
#define KOTONOHA_KTRF_NULL_INDEX UINT32_C(0xFFFFFFFF)

#define KOTONOHA_KTRF_SECTION_REQUIRED UINT32_C(0x00000001)
#define KOTONOHA_KTRF_SECTION_COMPRESSED UINT32_C(0x00000002)
#define KOTONOHA_KTRF_SECTION_NON_SEMANTIC UINT32_C(0x00000004)

typedef enum Kotonoha_KtrfErrorCode {
  KOTONOHA_KTRF_OK = 0,
  KOTONOHA_KTRF_ERROR_ARGUMENT,
  KOTONOHA_KTRF_ERROR_IO,
  KOTONOHA_KTRF_ERROR_MEMORY,
  KOTONOHA_KTRF_ERROR_FORMAT,
  KOTONOHA_KTRF_ERROR_INTEGRITY,
  KOTONOHA_KTRF_ERROR_UNSUPPORTED,
  KOTONOHA_KTRF_ERROR_MISSING_SECTION,
  KOTONOHA_KTRF_ERROR_RANGE
} Kotonoha_KtrfErrorCode;

typedef struct Kotonoha_KtrfError {
  Kotonoha_KtrfErrorCode code;
  char message[256];
} Kotonoha_KtrfError;

typedef struct Kotonoha_KtrfSection {
  char type[5];
  uint32_t flags;
  uint64_t offset;
  uint64_t stored_size;
  uint64_t decoded_size;
  uint32_t item_count;
  uint32_t alignment;
  uint32_t crc32;
  const uint8_t *payload;
} Kotonoha_KtrfSection;

typedef struct Kotonoha_KtrfDocument {
  uint8_t *bytes;
  size_t size;
  uint16_t format_major;
  uint16_t format_minor;
  uint32_t flags;
  uint32_t section_count;
  uint8_t content_sha256[32];
  Kotonoha_KtrfSection *sections;

  const uint8_t *strs_payload;
  uint64_t strs_size;
  uint32_t string_count;
  uint32_t string_bytes;
  const uint8_t *string_offsets;
  const uint8_t *string_blob;
} Kotonoha_KtrfDocument;

int Kotonoha_KtrfLoadFile(const char *path, Kotonoha_KtrfDocument *out,
                          Kotonoha_KtrfError *error);
int Kotonoha_KtrfLoadMemoryCopy(const void *data, size_t size,
                                Kotonoha_KtrfDocument *out,
                                Kotonoha_KtrfError *error);
void Kotonoha_KtrfInit(Kotonoha_KtrfDocument *document);
void Kotonoha_KtrfClean(Kotonoha_KtrfDocument *document);

const Kotonoha_KtrfSection *
Kotonoha_KtrfFindSection(const Kotonoha_KtrfDocument *document,
                         const char type[4]);

int Kotonoha_KtrfGetString(const Kotonoha_KtrfDocument *document,
                           uint32_t index, const char **data, size_t *length,
                           Kotonoha_KtrfError *error);

int Kotonoha_KtrfValidateFullCore(const Kotonoha_KtrfDocument *document,
                                  Kotonoha_KtrfError *error);

#ifdef __cplusplus
}
#endif
