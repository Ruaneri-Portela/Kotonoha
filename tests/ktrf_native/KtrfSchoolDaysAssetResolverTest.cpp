#include <Kotonoha/parsers/Ktrf.h>
#include <Kotonoha/routing/SchoolDaysKtrfAdapter.h>
#include <Kotonoha/routing/SchoolDaysKtrfAssetResolver.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "KtrfSchoolDaysAssetResolverTest: " << message << '\n';
    std::exit(2);
}

void Check(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

int IgnoreEnding(int64_t, void*, Kotonoha_KtrfError*) {
    return 1;
}

int IgnoreHook(const char*, size_t, void*, Kotonoha_KtrfError*) {
    return 1;
}

uint32_t Count(const Kotonoha_KtrfDocument& doc, const char type[4]) {
    const auto* section = Kotonoha_KtrfFindSection(&doc, type);
    return section ? section->item_count : 0u;
}

std::string Slice(const char* data, size_t size) {
    return data ? std::string(data, size) : std::string();
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: KtrfSchoolDaysAssetResolverTest "
                     "<school-days-hq.ktnroute> <assets-root>\n";
        return 2;
    }

    const char* artifact = argv[1];
    const char* assets_root = argv[2];

    Kotonoha_KtrfDocument document{};
    Kotonoha_KtrfError error{};
    Kotonoha_KtrfInit(&document);
    if (!Kotonoha_KtrfLoadFile(artifact, &document, &error))
        Fail(std::string("load failed: ") + error.message);

    Kotonoha_SchoolDaysKtrfCallbacks callbacks{};
    callbacks.register_ending = IgnoreEnding;
    callbacks.call_hook = IgnoreHook;

    Kotonoha_SchoolDaysKtrfAdapter adapter{};
    Kotonoha_SchoolDaysKtrfAdapterInit(&adapter);
    if (!Kotonoha_SchoolDaysKtrfAdapterBind(
            &adapter, &document, &callbacks, &error))
        Fail(std::string("bind failed: ") + error.message);

    uint32_t scene_assets = 0u;
    uint32_t dispatcher_rejections = 0u;
    size_t total_bytes = 0u;

    for (uint32_t i = 0; i < Count(document, "NODE"); ++i) {
        Kotonoha_SchoolDaysKtrfNodeView view{};
        if (!Kotonoha_SchoolDaysKtrfGetNodeView(&adapter, i, &view, &error))
            Fail(std::string("node view failed: ") + error.message);

        Kotonoha_SchoolDaysKtrfOrsAsset resolved{};
        if (view.kind == KOTONOHA_SDHQ_NODE_DISPATCHER) {
            if (Kotonoha_SchoolDaysKtrfResolveNodeOrs(
                    &adapter, i, assets_root, &resolved, &error))
                Fail("dispatcher unexpectedly resolved to an ORS asset");
            Check(error.code == KOTONOHA_KTRF_ERROR_FORMAT,
                  "dispatcher rejection used unexpected error code");
            ++dispatcher_rejections;
            continue;
        }

        if (!Kotonoha_SchoolDaysKtrfResolveNodeOrs(
                &adapter, i, assets_root, &resolved, &error))
            Fail(std::string("asset resolve failed for ") +
                 Slice(view.scene_key, view.scene_key_length) + ": " +
                 error.message);

        Check(resolved.node_index == i, "resolved node index mismatch");
        Check(resolved.resource_index == view.resource_index,
              "resolved resource index mismatch");
        Check(Slice(resolved.scene_key, resolved.scene_key_length) ==
                  Slice(view.scene_key, view.scene_key_length),
              "resolved scene-key mismatch");
        Check(EndsWith(resolved.path, KOTONOHA_SDHQ_ORS_EXTENSION),
              "resolved path lacks .ENG.ORS extension");

        size_t file_size = 0u;
        if (!Kotonoha_SchoolDaysKtrfProbeOrs(&resolved, &file_size, &error))
            Fail(std::string("physical ORS probe failed for ") +
                 Slice(view.scene_key, view.scene_key_length) + ": " +
                 error.message);
        Check(file_size != 0u, "physical ORS reported zero size");
        total_bytes += file_size;
        ++scene_assets;
    }

    Check(scene_assets == 1855u, "physical scene asset count mismatch");
    Check(dispatcher_rejections == 2u, "dispatcher rejection count mismatch");

    Kotonoha_SchoolDaysKtrfNodeView initial{};
    if (!Kotonoha_SchoolDaysKtrfResetNewGame(&adapter, &initial, &error))
        Fail(std::string("new-game reset failed: ") + error.message);

    Kotonoha_SchoolDaysKtrfOrsAsset initial_asset{};
    if (!Kotonoha_SchoolDaysKtrfCurrentOrs(
            &adapter, assets_root, &initial_asset, &error))
        Fail(std::string("current ORS resolve failed: ") + error.message);

    const std::string initial_key =
        Slice(initial_asset.scene_key, initial_asset.scene_key_length);
    Check(initial_key == "00/00-00-A00", "new-game ORS scene-key mismatch");
    Check(EndsWith(initial_asset.path, "00-00-A00.ENG.ORS"),
          "new-game physical ORS filename mismatch");

    size_t initial_size = 0u;
    if (!Kotonoha_SchoolDaysKtrfProbeOrs(
            &initial_asset, &initial_size, &error))
        Fail(std::string("new-game ORS probe failed: ") + error.message);

    Kotonoha_SchoolDaysKtrfAssetAdvanceResult next{};
    if (!Kotonoha_SchoolDaysKtrfAdvanceToOrs(
            &adapter, assets_root, "ktrf:next", &next, &error))
        Fail(std::string("advance-to-ORS failed: ") + error.message);
    Check(next.advance.status == KOTONOHA_SDHQ_ADVANCE_ADVANCED,
          "first routing step did not resolve to a physical scene");
    Check(next.asset.path[0] != '\0', "first routed ORS path is empty");

    size_t next_size = 0u;
    if (!Kotonoha_SchoolDaysKtrfProbeOrs(&next.asset, &next_size, &error))
        Fail(std::string("first routed ORS probe failed: ") + error.message);

    std::cout << "SCHOOL DAYS KTRF ADAPTER GATE 2 PASS\n";
    std::cout << "scene_assets=" << scene_assets << "/1855\n";
    std::cout << "dispatcher_rejections=" << dispatcher_rejections << "/2\n";
    std::cout << "asset_bytes=" << total_bytes << '\n';
    std::cout << "new_game_scene=" << initial_key << '\n';
    std::cout << "new_game_asset=" << initial_asset.path << '\n';
    std::cout << "new_game_asset_bytes=" << initial_size << '\n';
    std::cout << "first_routed_scene="
              << Slice(next.asset.scene_key, next.asset.scene_key_length) << '\n';
    std::cout << "first_routed_asset=" << next.asset.path << '\n';
    std::cout << "first_routed_asset_bytes=" << next_size << '\n';
    std::cout << "scene_key_inverse_mapping=PASS\n";
    std::cout << "physical_ors_open=PASS\n";

    Kotonoha_SchoolDaysKtrfAdapterClean(&adapter);
    Kotonoha_KtrfClean(&document);
    return 0;
}
