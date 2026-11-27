#pragma once

#include "Kotonoha/routing/KtrfAdapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCHOOL_DAYS_KTRF_ASSET_PATH_CAPACITY 4096u

typedef struct SchoolDaysKtrfResolvedAsset {
  uint32_t node_index;
  uint32_t resource_index;
  const char *scene_key;
  size_t scene_key_length;
  char path[SCHOOL_DAYS_KTRF_ASSET_PATH_CAPACITY];
  size_t path_length;
} SchoolDaysKtrfResolvedAsset;

typedef struct SchoolDaysKtrfAssetAdvanceResult {
  Kotonoha_KtrfAdvanceResult advance;
  SchoolDaysKtrfResolvedAsset asset;
} SchoolDaysKtrfAssetAdvanceResult;

int SchoolDaysKtrfBuildAssetPath(
    const char *assets_root, const char *scene_key, size_t scene_key_length,
    char *out_path, size_t out_capacity, size_t *out_length,
    Kotonoha_KtrfError *error);

int SchoolDaysKtrfResolveNodeAsset(
    const Kotonoha_KtrfAdapter *adapter, uint32_t node_index,
    const char *assets_root, SchoolDaysKtrfResolvedAsset *out,
    Kotonoha_KtrfError *error);

int SchoolDaysKtrfCurrentAsset(
    const Kotonoha_KtrfAdapter *adapter, const char *assets_root,
    SchoolDaysKtrfResolvedAsset *out, Kotonoha_KtrfError *error);

int SchoolDaysKtrfAdvanceToAsset(
    Kotonoha_KtrfAdapter *adapter, const char *assets_root,
    const char *trigger, SchoolDaysKtrfAssetAdvanceResult *out,
    Kotonoha_KtrfError *error);

int SchoolDaysKtrfProbeAsset(
    const SchoolDaysKtrfResolvedAsset *asset, size_t *file_size,
    Kotonoha_KtrfError *error);

#ifdef __cplusplus
}
#endif
