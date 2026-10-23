#pragma once

#include "Kotonoha/routing/KtrfAdapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KOTONOHA_SDHQ_ORS_EXTENSION ".ENG.ORS"
#define KOTONOHA_SDHQ_ASSET_PATH_CAPACITY 4096u

typedef struct Kotonoha_KtrfOrsAsset {
  uint32_t node_index;
  uint32_t resource_index;
  const char *scene_key;
  size_t scene_key_length;
  char path[KOTONOHA_SDHQ_ASSET_PATH_CAPACITY];
  size_t path_length;
} Kotonoha_KtrfOrsAsset;

typedef struct Kotonoha_KtrfAssetAdvanceResult {
  Kotonoha_KtrfAdvanceResult advance;
  Kotonoha_KtrfOrsAsset asset;
} Kotonoha_KtrfAssetAdvanceResult;

int Kotonoha_KtrfBuildOrsPath(
    const char *assets_root, const char *scene_key, size_t scene_key_length,
    char *out_path, size_t out_capacity, size_t *out_length,
    Kotonoha_KtrfError *error);

int Kotonoha_KtrfResolveNodeOrs(
    const Kotonoha_KtrfAdapter *adapter, uint32_t node_index,
    const char *assets_root, Kotonoha_KtrfOrsAsset *out,
    Kotonoha_KtrfError *error);

int Kotonoha_KtrfCurrentOrs(
    const Kotonoha_KtrfAdapter *adapter, const char *assets_root,
    Kotonoha_KtrfOrsAsset *out, Kotonoha_KtrfError *error);

int Kotonoha_KtrfAdvanceToOrs(
    Kotonoha_KtrfAdapter *adapter, const char *assets_root,
    const char *trigger, Kotonoha_KtrfAssetAdvanceResult *out,
    Kotonoha_KtrfError *error);

int Kotonoha_KtrfProbeOrs(
    const Kotonoha_KtrfOrsAsset *asset, size_t *file_size,
    Kotonoha_KtrfError *error);

#ifdef __cplusplus
}
#endif
