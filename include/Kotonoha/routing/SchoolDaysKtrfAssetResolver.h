#pragma once

#include "Kotonoha/routing/SchoolDaysKtrfAdapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KOTONOHA_SDHQ_ORS_EXTENSION ".ENG.ORS"
#define KOTONOHA_SDHQ_ASSET_PATH_CAPACITY 4096u

typedef struct Kotonoha_SchoolDaysKtrfOrsAsset {
  uint32_t node_index;
  uint32_t resource_index;
  const char *scene_key;
  size_t scene_key_length;
  char path[KOTONOHA_SDHQ_ASSET_PATH_CAPACITY];
  size_t path_length;
} Kotonoha_SchoolDaysKtrfOrsAsset;

typedef struct Kotonoha_SchoolDaysKtrfAssetAdvanceResult {
  Kotonoha_SchoolDaysKtrfAdvanceResult advance;
  Kotonoha_SchoolDaysKtrfOrsAsset asset;
} Kotonoha_SchoolDaysKtrfAssetAdvanceResult;

int Kotonoha_SchoolDaysKtrfBuildOrsPath(
    const char *assets_root, const char *scene_key, size_t scene_key_length,
    char *out_path, size_t out_capacity, size_t *out_length,
    Kotonoha_KtrfError *error);

int Kotonoha_SchoolDaysKtrfResolveNodeOrs(
    const Kotonoha_SchoolDaysKtrfAdapter *adapter, uint32_t node_index,
    const char *assets_root, Kotonoha_SchoolDaysKtrfOrsAsset *out,
    Kotonoha_KtrfError *error);

int Kotonoha_SchoolDaysKtrfCurrentOrs(
    const Kotonoha_SchoolDaysKtrfAdapter *adapter, const char *assets_root,
    Kotonoha_SchoolDaysKtrfOrsAsset *out, Kotonoha_KtrfError *error);

int Kotonoha_SchoolDaysKtrfAdvanceToOrs(
    Kotonoha_SchoolDaysKtrfAdapter *adapter, const char *assets_root,
    const char *trigger, Kotonoha_SchoolDaysKtrfAssetAdvanceResult *out,
    Kotonoha_KtrfError *error);

int Kotonoha_SchoolDaysKtrfProbeOrs(
    const Kotonoha_SchoolDaysKtrfOrsAsset *asset, size_t *file_size,
    Kotonoha_KtrfError *error);

#ifdef __cplusplus
}
#endif
