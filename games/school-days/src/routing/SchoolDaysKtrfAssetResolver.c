#include "Kotonoha/routing/SchoolDaysKtrfAssetResolver.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void asset_clear_error(Kotonoha_KtrfError *error) {
  if (error != NULL) {
    error->code = KOTONOHA_KTRF_OK;
    error->message[0] = '\0';
  }
}

static int asset_fail(Kotonoha_KtrfError *error,
                      Kotonoha_KtrfErrorCode code,
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

static int asset_is_digit(char value) {
  return value >= '0' && value <= '9';
}

static int asset_is_name_char(char value) {
  return (value >= '0' && value <= '9') ||
         (value >= 'A' && value <= 'Z') ||
         (value >= 'a' && value <= 'z') || value == '-';
}

static int asset_validate_scene_key(const char *scene_key, size_t length,
                                    Kotonoha_KtrfError *error) {
  size_t i;
  if (scene_key == NULL || length < 7u)
    return asset_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "School Days scene-key is empty or too short");

  if (!asset_is_digit(scene_key[0]) || !asset_is_digit(scene_key[1]) ||
      scene_key[2] != '/' || scene_key[3] != scene_key[0] ||
      scene_key[4] != scene_key[1] || scene_key[5] != '-')
    return asset_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "invalid School Days scene-key layout");

  for (i = 3u; i < length; ++i) {
    if (!asset_is_name_char(scene_key[i]))
      return asset_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days scene-key contains invalid character");
  }
  return 1;
}

static char asset_separator(void) {
#if defined(_WIN32)
  return '\\';
#else
  return '/';
#endif
}

int Kotonoha_SchoolDaysKtrfBuildOrsPath(
    const char *assets_root, const char *scene_key, size_t scene_key_length,
    char *out_path, size_t out_capacity, size_t *out_length,
    Kotonoha_KtrfError *error) {
  const size_t extension_length = sizeof(KOTONOHA_SDHQ_ORS_EXTENSION) - 1u;
  size_t root_length;
  size_t cursor = 0u;
  size_t i;
  int needs_separator;
  char separator;

  asset_clear_error(error);
  if (assets_root == NULL || assets_root[0] == '\0' || out_path == NULL ||
      out_capacity == 0u)
    return asset_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "asset root/output path argument is empty");
  if (!asset_validate_scene_key(scene_key, scene_key_length, error))
    return 0;

  root_length = strlen(assets_root);
  needs_separator =
      assets_root[root_length - 1u] != '/' &&
      assets_root[root_length - 1u] != '\\';
  if (root_length + (needs_separator ? 1u : 0u) + scene_key_length +
          extension_length + 1u >
      out_capacity)
    return asset_fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                      "resolved School Days ORS path exceeds output capacity");

  memcpy(out_path + cursor, assets_root, root_length);
  cursor += root_length;
  separator = asset_separator();
  if (needs_separator)
    out_path[cursor++] = separator;

  for (i = 0u; i < scene_key_length; ++i) {
    char value = scene_key[i];
    out_path[cursor++] = value == '/' ? separator : value;
  }

  memcpy(out_path + cursor, KOTONOHA_SDHQ_ORS_EXTENSION, extension_length);
  cursor += extension_length;
  out_path[cursor] = '\0';
  if (out_length != NULL)
    *out_length = cursor;
  return 1;
}

int Kotonoha_SchoolDaysKtrfResolveNodeOrs(
    const Kotonoha_SchoolDaysKtrfAdapter *adapter, uint32_t node_index,
    const char *assets_root, Kotonoha_SchoolDaysKtrfOrsAsset *out,
    Kotonoha_KtrfError *error) {
  Kotonoha_SchoolDaysKtrfNodeView view;

  asset_clear_error(error);
  if (adapter == NULL || out == NULL)
    return asset_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "School Days asset resolver argument is null");

  memset(out, 0, sizeof(*out));
  out->node_index = KOTONOHA_KTRF_NULL_INDEX;
  out->resource_index = KOTONOHA_KTRF_NULL_INDEX;

  if (!Kotonoha_SchoolDaysKtrfGetNodeView(adapter, node_index, &view, error))
    return 0;
  if (view.kind != KOTONOHA_SDHQ_NODE_SCENE)
    return asset_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "routing-only School Days node has no physical ORS asset");
  if (view.scene_key == NULL || view.scene_key_length == 0u ||
      view.resource_index == KOTONOHA_KTRF_NULL_INDEX)
    return asset_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "School Days scene node lacks a resolvable resource");

  if (!Kotonoha_SchoolDaysKtrfBuildOrsPath(
          assets_root, view.scene_key, view.scene_key_length, out->path,
          sizeof(out->path), &out->path_length, error))
    return 0;

  out->node_index = view.node_index;
  out->resource_index = view.resource_index;
  out->scene_key = view.scene_key;
  out->scene_key_length = view.scene_key_length;
  return 1;
}

int Kotonoha_SchoolDaysKtrfCurrentOrs(
    const Kotonoha_SchoolDaysKtrfAdapter *adapter, const char *assets_root,
    Kotonoha_SchoolDaysKtrfOrsAsset *out, Kotonoha_KtrfError *error) {
  Kotonoha_SchoolDaysKtrfNodeView view;

  asset_clear_error(error);
  if (adapter == NULL || out == NULL)
    return asset_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "School Days current-asset argument is null");
  if (!Kotonoha_SchoolDaysKtrfCurrentNodeView(adapter, &view, error))
    return 0;
  return Kotonoha_SchoolDaysKtrfResolveNodeOrs(
      adapter, view.node_index, assets_root, out, error);
}

int Kotonoha_SchoolDaysKtrfAdvanceToOrs(
    Kotonoha_SchoolDaysKtrfAdapter *adapter, const char *assets_root,
    const char *trigger, Kotonoha_SchoolDaysKtrfAssetAdvanceResult *out,
    Kotonoha_KtrfError *error) {
  asset_clear_error(error);
  if (adapter == NULL || out == NULL)
    return asset_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "School Days asset-advance argument is null");

  memset(out, 0, sizeof(*out));
  out->asset.node_index = KOTONOHA_KTRF_NULL_INDEX;
  out->asset.resource_index = KOTONOHA_KTRF_NULL_INDEX;

  if (!Kotonoha_SchoolDaysKtrfAdvanceToScene(
          adapter, trigger, &out->advance, error))
    return 0;

  if (out->advance.status == KOTONOHA_SDHQ_ADVANCE_ADVANCED) {
    if (out->advance.scene.kind != KOTONOHA_SDHQ_NODE_SCENE)
      return asset_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "AdvanceToScene returned a non-scene destination");
    if (!Kotonoha_SchoolDaysKtrfResolveNodeOrs(
            adapter, out->advance.scene.node_index, assets_root, &out->asset,
            error))
      return 0;
  }
  return 1;
}

int Kotonoha_SchoolDaysKtrfProbeOrs(
    const Kotonoha_SchoolDaysKtrfOrsAsset *asset, size_t *file_size,
    Kotonoha_KtrfError *error) {
  FILE *stream;
  long size;

  asset_clear_error(error);
  if (asset == NULL || asset->path[0] == '\0')
    return asset_fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                      "School Days ORS asset path is empty");

  stream = fopen(asset->path, "rb");
  if (stream == NULL)
    return asset_fail(error, KOTONOHA_KTRF_ERROR_IO,
                      "cannot open School Days ORS asset: %s", asset->path);

  if (fseek(stream, 0L, SEEK_END) != 0) {
    fclose(stream);
    return asset_fail(error, KOTONOHA_KTRF_ERROR_IO,
                      "cannot seek School Days ORS asset: %s", asset->path);
  }
  size = ftell(stream);
  fclose(stream);

  if (size <= 0L)
    return asset_fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                      "School Days ORS asset is empty: %s", asset->path);
  if (file_size != NULL)
    *file_size = (size_t)size;
  return 1;
}
