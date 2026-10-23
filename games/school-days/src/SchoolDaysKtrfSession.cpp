#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/routing/SchoolDaysBackRules.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace Kotonoha {
namespace {

bool RouteMapFail(Kotonoha_KtrfError* error, Kotonoha_KtrfErrorCode code,
                  const char* message) {
    if (error != nullptr) {
        error->code = code;
        std::snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}

bool RouteMapAscii(const std::u16string& value, std::string* out,
                   Kotonoha_KtrfError* error) {
    if (out == nullptr || value.empty()) {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days RouteMap checkpoint SceneKey is empty");
    }
    out->clear();
    out->reserve(value.size());
    for (char16_t unit : value) {
        if (unit == 0 || unit > 0x7fu) {
            out->clear();
            return RouteMapFail(
                error, KOTONOHA_KTRF_ERROR_FORMAT,
                "School Days RouteMap checkpoint SceneKey is not ASCII");
        }
        out->push_back(static_cast<char>(unit));
    }
    return true;
}

bool RouteMapCoordinateValue(const SchoolDaysFlgH& flags,
                             const char16_t* key, int* value,
                             Kotonoha_KtrfError* error) {
    if (key == nullptr || value == nullptr) {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                            "School Days RouteMap checkpoint coordinate argument is invalid");
    }
    const SchoolDaysFlgHEntry* found = nullptr;
    for (const auto& entry : flags.entries) {
        if (entry.key != key) continue;
        if (found != nullptr) {
            return RouteMapFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                                "School Days RouteMap checkpoint has duplicate coordinate keys");
        }
        found = &entry;
    }
    if (found == nullptr || found->type != 3) {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days RouteMap checkpoint lacks integer ROUTE/SCENE");
    }
    if (found->integer < std::numeric_limits<int>::min() ||
        found->integer > std::numeric_limits<int>::max()) {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_RANGE,
                            "School Days RouteMap checkpoint coordinate exceeds runtime range");
    }
    *value = static_cast<int>(found->integer);
    return true;
}

bool ProbeResolvedOrs(const Kotonoha_SchoolDaysKtrfOrsAsset& asset,
                      size_t* bytes, Kotonoha_KtrfError* error) {
    if (bytes != nullptr) *bytes = 0u;
    if (asset.path[0] == '\0') {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                            "School Days ORS asset reference is empty");
    }
    const std::string reference(asset.path, asset.path_length);
    if (!SchoolDaysAssetReferenceIsVirtual(reference))
        return Kotonoha_SchoolDaysKtrfProbeOrs(&asset, bytes, error) != 0;

    std::vector<uint8_t> content;
    std::string assetError;
    if (!ReadSchoolDaysAssetReference(reference, &content, &assetError)) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_IO;
            std::snprintf(error->message, sizeof(error->message),
                          "cannot read School Days ORS asset: %s",
                          assetError.c_str());
        }
        return false;
    }
    if (content.empty()) {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days ORS asset is empty");
    }
    if (bytes != nullptr) *bytes = content.size();
    return true;
}

} // namespace

bool SchoolDaysKtrfSession::StepResult::HasHook(const std::string& symbol) const {
    for (const auto& hook : hooks) {
        if (hook == symbol) return true;
    }
    return false;
}

SchoolDaysKtrfSession::SchoolDaysKtrfSession() {
    Kotonoha_KtrfInit(&document);
    Kotonoha_SchoolDaysKtrfAdapterInit(&adapter);
}

SchoolDaysKtrfSession::~SchoolDaysKtrfSession() {
    Close();
}

int SchoolDaysKtrfSession::OnEnding(int64_t endingCode, void* userdata,
                                    Kotonoha_KtrfError* error) {
    auto* self = static_cast<SchoolDaysKtrfSession*>(userdata);
    if (self == nullptr) return 0;
    Kotonoha_SchoolDaysKtrfNodeView view{};
    if (!Kotonoha_SchoolDaysKtrfCurrentNodeView(&self->adapter, &view, error) ||
        view.kind != KOTONOHA_SDHQ_NODE_SCENE || view.scene_key == nullptr) {
        return 0;
    }
    const std::string sceneKey(view.scene_key, view.scene_key_length);
    if (!self->persistence.RegisterEnding(static_cast<int>(endingCode),
                                          sceneKey, &self->state, error)) {
        return 0;
    }
    self->pendingEndings.push_back(endingCode);
    return 1;
}

int SchoolDaysKtrfSession::OnHook(const char* symbol, size_t symbolLength,
                                  void* userdata, Kotonoha_KtrfError* error) {
    auto* self = static_cast<SchoolDaysKtrfSession*>(userdata);
    if (self == nullptr || symbol == nullptr) return 0;
    const std::string hook(symbol, symbolLength);
    if (hook == "overflow.sdhq:callback_00") {
        int route = 0;
        int scene = 0;
        Kotonoha_SchoolDaysKtrfNodeView view{};
        if (!self->state.ReadCoordinate(&route, &scene, error) ||
            !Kotonoha_SchoolDaysKtrfCurrentNodeView(&self->adapter, &view, error) ||
            view.kind != KOTONOHA_SDHQ_NODE_SCENE || view.scene_key == nullptr) {
            return 0;
        }
        const std::string sceneKey(view.scene_key, view.scene_key_length);
        if (!self->persistence.RecordCheckpoint(
                route, scene, sceneKey, &self->state, error)) {
            return 0;
        }
    }
    self->pendingHooks.emplace_back(symbol, symbolLength);
    return 1;
}

int SchoolDaysKtrfSession::OnEffectApplied(uint32_t effectIndex,
                                           void* userdata,
                                           Kotonoha_KtrfError* error) {
    auto* self = static_cast<SchoolDaysKtrfSession*>(userdata);
    if (self == nullptr) return 0;
    if (!self->persistence.ObserveRoutingEffect(effectIndex, error)) return 0;
    if (self->persistence.RoutingEffectTargetsTopMeter(effectIndex)) {
        self->topMeterRefreshRequested = true;
    }
    return 1;
}

bool SchoolDaysKtrfSession::ResolveOrsPhysicalPath(
    Kotonoha_SchoolDaysKtrfOrsAsset* asset, Kotonoha_KtrfError* error) const {
    if (asset == nullptr || asset->scene_key == nullptr ||
        asset->scene_key_length == 0u) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days ORS filesystem resolve argument is invalid");
        }
        return false;
    }

    std::string relative(asset->scene_key, asset->scene_key_length);
    relative += KOTONOHA_SDHQ_ORS_EXTENSION;

    // The retail source of truth is Script.GPK/English.  Preserve the historic
    // extracted ORS root only as a compatibility fallback for development
    // corpora and tests that do not mount the retail packs.
    const std::string packedLogical = "Script/English/" + relative;
    SchoolDaysImageResolution resolved;
    std::string reportedLogical = relative;
    std::string scriptOverride;
    if (ResolveSchoolDaysScriptDirectoryFile(packedLogical, &scriptOverride)) {
        resolved.status = SchoolDaysAssetResolutionStatus::Present;
        resolved.present = true;
        resolved.physical = std::move(scriptOverride);
    } else if (gSchoolDaysVirtualAssetLookup != nullptr) {
        resolved = ResolveSchoolDaysAssetPath("", packedLogical);
        reportedLogical = packedLogical;
    }
    if (!resolved.present &&
        (gSchoolDaysVirtualAssetLookup == nullptr ||
         resolved.status == SchoolDaysAssetResolutionStatus::Missing) &&
        !assetsRoot.empty()) {
        resolved = ResolveSchoolDaysAssetPath(assetsRoot.c_str(), relative);
        reportedLogical = relative;
    }
    if (resolved.status != SchoolDaysAssetResolutionStatus::Present) {
        if (error != nullptr) {
            error->code = resolved.status == SchoolDaysAssetResolutionStatus::Missing
                              ? KOTONOHA_KTRF_ERROR_IO
                              : KOTONOHA_KTRF_ERROR_FORMAT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days ORS %s: %s",
                          resolved.status == SchoolDaysAssetResolutionStatus::Missing
                              ? "asset is missing"
                              : resolved.status == SchoolDaysAssetResolutionStatus::Collision
                                    ? "path has a case-insensitive collision"
                                    : "path is invalid",
                          reportedLogical.c_str());
        }
        return false;
    }
    if (resolved.physical.size() + 1u > sizeof(asset->path)) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_RANGE;
            std::snprintf(error->message, sizeof(error->message),
                          "resolved School Days ORS path exceeds asset capacity");
        }
        return false;
    }
    std::memcpy(asset->path, resolved.physical.c_str(), resolved.physical.size() + 1u);
    asset->path_length = resolved.physical.size();
    return true;
}

bool SchoolDaysKtrfSession::FillScene(
    const Kotonoha_SchoolDaysKtrfOrsAsset& asset,
    SchoolDaysKtrfScene* out) const {
    if (out == nullptr || asset.scene_key == nullptr || asset.path[0] == '\0') {
        return false;
    }
    out->nodeIndex = asset.node_index;
    out->resourceIndex = asset.resource_index;
    out->sceneKey.assign(asset.scene_key, asset.scene_key_length);
    out->orsPath.assign(asset.path, asset.path_length);
    return true;
}

bool SchoolDaysKtrfSession::Open(const char* ktrfPath, const char* root,
                                 Kotonoha_KtrfError* error) {
    Close();
    Kotonoha_KtrfInit(&document);
    Kotonoha_SchoolDaysKtrfAdapterInit(&adapter);

    if (ktrfPath == nullptr || *ktrfPath == '\0' || root == nullptr || *root == '\0') {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days KTRF session path/assets root is empty");
        }
        return false;
    }

    if (!Kotonoha_KtrfLoadFile(ktrfPath, &document, error)) {
        return false;
    }

    Kotonoha_SchoolDaysKtrfCallbacks callbacks{};
    callbacks.register_ending = &SchoolDaysKtrfSession::OnEnding;
    callbacks.call_hook = &SchoolDaysKtrfSession::OnHook;
    callbacks.effect_applied = &SchoolDaysKtrfSession::OnEffectApplied;
    callbacks.userdata = this;

    if (!Kotonoha_SchoolDaysKtrfAdapterBind(&adapter, &document, &callbacks, error)) {
        Kotonoha_KtrfClean(&document);
        Kotonoha_KtrfInit(&document);
        return false;
    }
    if (!state.Bind(&adapter, error)) {
        Kotonoha_SchoolDaysKtrfAdapterClean(&adapter);
        Kotonoha_KtrfClean(&document);
        Kotonoha_KtrfInit(&document);
        return false;
    }
    if (!persistence.ValidateEffectOracle(adapter.document, error)) {
        state.Unbind();
        Kotonoha_SchoolDaysKtrfAdapterClean(&adapter);
        Kotonoha_KtrfClean(&document);
        Kotonoha_KtrfInit(&document);
        return false;
    }

    assetsRoot = root;
    history.Clear();
    pendingHooks.clear();
    pendingEndings.clear();
    topMeterRefreshRequested = false;
    opened = true;
    return true;
}

void SchoolDaysKtrfSession::Close() {
    history.Clear();
    persistence = SchoolDaysPersistence{};
    state.Unbind();
    if (opened || adapter.document != nullptr) {
        Kotonoha_SchoolDaysKtrfAdapterClean(&adapter);
    }
    Kotonoha_KtrfClean(&document);
    Kotonoha_KtrfInit(&document);
    Kotonoha_SchoolDaysKtrfAdapterInit(&adapter);
    assetsRoot.clear();
    pendingHooks.clear();
    pendingEndings.clear();
    topMeterRefreshRequested = false;
    opened = false;
}

bool SchoolDaysKtrfSession::LoadGlobalFlag(
    const std::vector<uint8_t>& bytes, Kotonoha_KtrfError* error) {
    if (!opened) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days GlobalFlag load requires an open session");
        }
        return false;
    }
    return persistence.LoadGlobalFlag(bytes, &state, error);
}

bool SchoolDaysKtrfSession::SerializeGlobalFlag(
    std::vector<uint8_t>* bytes, Kotonoha_KtrfError* error) const {
    if (!opened) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days GlobalFlag save requires an open session");
        }
        return false;
    }
    return persistence.SerializeGlobalFlag(state, bytes, error);
}

bool SchoolDaysKtrfSession::PlanLoadSave(
    const std::vector<uint8_t>& bytes, LoadPlan* plan,
    Kotonoha_KtrfError* error) const {
    if (!opened || plan == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days save load plan requires an open session/result");
        }
        return false;
    }

    *plan = LoadPlan{};
    if (!persistence.ParseSaveImage(bytes, &plan->image, error)) return false;

    // The original load prepares the saved SceneKey directly; ROUTE/SCENE are
    // then the restored local FlagsHash coordinate and must agree with it. Use
    // SceneKey as the locator authority and the Back oracle only as an
    // independent coordinate cross-check.
    const Kotonoha_KtrfSection* nodes =
        Kotonoha_KtrfFindSection(adapter.document, "NODE");
    if (nodes == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_MISSING_SECTION;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days KTRF document has no NODE section");
        }
        return false;
    }

    uint32_t matchedNode = KOTONOHA_KTRF_NULL_INDEX;
    for (uint32_t nodeIndex = 0u; nodeIndex < nodes->item_count; ++nodeIndex) {
        Kotonoha_SchoolDaysKtrfNodeView view{};
        if (!Kotonoha_SchoolDaysKtrfGetNodeView(&adapter, nodeIndex, &view,
                                                error)) {
            return false;
        }
        if (view.kind != KOTONOHA_SDHQ_NODE_SCENE || view.scene_key == nullptr ||
            view.scene_key_length != plan->image.sceneKey.size() ||
            std::memcmp(view.scene_key, plan->image.sceneKey.data(),
                        view.scene_key_length) != 0) {
            continue;
        }
        if (matchedNode != KOTONOHA_KTRF_NULL_INDEX) {
            if (error != nullptr) {
                error->code = KOTONOHA_KTRF_ERROR_FORMAT;
                std::snprintf(error->message, sizeof(error->message),
                              "School Days save SceneKey resolves to multiple physical nodes");
            }
            return false;
        }
        matchedNode = nodeIndex;
    }
    if (matchedNode == KOTONOHA_KTRF_NULL_INDEX ||
        !ResolveSceneNode(matchedNode, &plan->scene, error)) {
        if (error != nullptr && error->code == KOTONOHA_KTRF_OK) {
            error->code = KOTONOHA_KTRF_ERROR_RANGE;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days save SceneKey has no physical KTRF node");
        }
        return false;
    }

    const char* expected = SchoolDaysBackRules::SceneKeyFor(
        plan->image.route, plan->image.scene);
    if (expected == nullptr || plan->image.sceneKey != expected) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days save SceneKey disagrees with ROUTE/SCENE");
        }
        return false;
    }
    plan->destination.route = plan->image.route;
    plan->destination.scene = plan->image.scene;
    plan->destination.nodeIndex = matchedNode;
    plan->destination.sceneKey = plan->image.sceneKey;
    return true;
}

bool SchoolDaysKtrfSession::CommitLoadSave(const LoadPlan& plan,
                                           Kotonoha_KtrfError* error) {
    if (!opened || plan.destination.nodeIndex == KOTONOHA_KTRF_NULL_INDEX ||
        plan.scene.orsPath.empty()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days save load plan is invalid");
        }
        return false;
    }

    SchoolDaysStateStore::Snapshot before;
    if (!state.Capture(&before, error)) return false;
    const SchoolDaysHistory historyBefore = history;
    const SchoolDaysPersistence persistenceBefore = persistence;
    const auto hooksBefore = pendingHooks;
    const auto endingsBefore = pendingEndings;

    if (!state.ReplaceSessionInts(plan.image.sessionInts, error) ||
        !Kotonoha_KtrfRouterActivateNode(&adapter.router,
                                         plan.destination.nodeIndex, error) ||
        !state.ResetChoicesForNode(plan.destination.nodeIndex, error) ||
        !history.Rebase(plan.destination, state, error)) {
        Kotonoha_KtrfError ignored{};
        (void)state.Restore(before, &ignored);
        history = historyBefore;
        persistence = persistenceBefore;
        pendingHooks = hooksBefore;
        pendingEndings = endingsBefore;
        return false;
    }

    int route = 0;
    int scene = 0;
    if (!state.ReadCoordinate(&route, &scene, error) ||
        route != plan.image.route || scene != plan.image.scene) {
        if (error != nullptr && error->code == KOTONOHA_KTRF_OK) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days save coordinate changed during restore");
        }
        Kotonoha_KtrfError ignored{};
        (void)state.Restore(before, &ignored);
        history = historyBefore;
        persistence = persistenceBefore;
        pendingHooks = hooksBefore;
        pendingEndings = endingsBefore;
        return false;
    }

    persistence.CommitSaveImage(plan.image);
    if (!persistence.MarkLocalInteger("BackSel", 3, error) ||
        !state.WriteSessionInt("BackSel", -1, error) ||
        !persistence.ReplaceCurrentScriptSnapshot(
            plan.scene.sceneKey, UINT32_C(0x3f800000), state, error)) {
        Kotonoha_KtrfError ignored{};
        (void)state.Restore(before, &ignored);
        history = historyBefore;
        persistence = persistenceBefore;
        pendingHooks = hooksBefore;
        pendingEndings = endingsBefore;
        return false;
    }
    pendingHooks.clear();
    pendingEndings.clear();
    return true;
}

bool SchoolDaysKtrfSession::PlanRouteMapCheckpoint(
    int selector, RouteMapCheckpointPlan* plan,
    Kotonoha_KtrfError* error) const {
    if (!opened || plan == nullptr || selector <= 0) {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                            "School Days RouteMap checkpoint plan argument is invalid");
    }

    *plan = RouteMapCheckpointPlan{};
    char spBuffer[32];
    std::snprintf(spBuffer, sizeof(spBuffer), "SP%03d", selector);
    const std::u16string selectedKey(spBuffer, spBuffer + std::strlen(spBuffer));

    const SchoolDaysSLogRecord* selected = nullptr;
    for (const auto& record : persistence.SaveLogDocument().records) {
        if (record.tag != 3 || record.secondary != selectedKey) continue;
        if (selected != nullptr) {
            return RouteMapFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                                "School Days RouteMap selector resolves to duplicate checkpoints");
        }
        selected = &record;
    }
    if (selected == nullptr) {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_RANGE,
                            "School Days RouteMap selector has no checkpoint");
    }

    std::string sceneKey;
    int route = 0;
    int scene = 0;
    if (!RouteMapAscii(selected->name, &sceneKey, error) ||
        !RouteMapCoordinateValue(selected->flags, u"ROUTE", &route, error) ||
        !RouteMapCoordinateValue(selected->flags, u"SCENE", &scene, error)) {
        return false;
    }

    SchoolDaysKtrfScene resolved;
    if (!ResolveSceneKey(sceneKey, &resolved, error)) return false;
    const char* expectedSceneKey = SchoolDaysBackRules::SceneKeyFor(route, scene);
    if (expectedSceneKey == nullptr || sceneKey != expectedSceneKey) {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days RouteMap checkpoint SceneKey disagrees with ROUTE/SCENE");
    }
    if (SchoolDaysBackRules::SpForScene(route, scene) != selector) {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days RouteMap checkpoint coordinate disagrees with selector");
    }

    plan->selector = selector;
    plan->destination.route = route;
    plan->destination.scene = scene;
    plan->destination.nodeIndex = resolved.nodeIndex;
    plan->destination.sceneKey = sceneKey;
    plan->scene = std::move(resolved);
    return true;
}

bool SchoolDaysKtrfSession::CommitRouteMapCheckpoint(
    const RouteMapCheckpointPlan& plan, Kotonoha_KtrfError* error) {
    if (!opened || plan.selector <= 0 ||
        plan.destination.nodeIndex == KOTONOHA_KTRF_NULL_INDEX ||
        plan.scene.nodeIndex != plan.destination.nodeIndex ||
        plan.scene.sceneKey != plan.destination.sceneKey || plan.scene.orsPath.empty()) {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                            "School Days RouteMap checkpoint plan is invalid");
    }

    RouteMapCheckpointPlan current;
    if (!PlanRouteMapCheckpoint(plan.selector, &current, error)) return false;
    if (current.destination.route != plan.destination.route ||
        current.destination.scene != plan.destination.scene ||
        current.destination.nodeIndex != plan.destination.nodeIndex ||
        current.destination.sceneKey != plan.destination.sceneKey ||
        current.scene.resourceIndex != plan.scene.resourceIndex ||
        current.scene.orsPath != plan.scene.orsPath) {
        return RouteMapFail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days RouteMap checkpoint changed before commit");
    }

    SchoolDaysStateStore::Snapshot stateBefore;
    if (!state.Capture(&stateBefore, error)) return false;
    const SchoolDaysHistory historyBefore = history;
    const SchoolDaysPersistence persistenceBefore = persistence;
    const auto hooksBefore = pendingHooks;
    const auto endingsBefore = pendingEndings;

    auto rollback = [&]() {
        Kotonoha_KtrfError ignored{};
        (void)state.Restore(stateBefore, &ignored);
        history = historyBefore;
        persistence = persistenceBefore;
        pendingHooks = hooksBefore;
        pendingEndings = endingsBefore;
    };

    bool checkpointRestored = false;
    if (!persistence.RestoreBackCheckpoint(
            current.destination, &state, &checkpointRestored, error) ||
        !checkpointRestored) {
        if (error != nullptr && error->code == KOTONOHA_KTRF_OK) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            std::snprintf(error->message, sizeof(error->message), "%s",
                          "School Days RouteMap checkpoint disappeared during commit");
        }
        rollback();
        return false;
    }

    int restoredRoute = 0;
    int restoredScene = 0;
    if (!state.ReadCoordinate(&restoredRoute, &restoredScene, error) ||
        restoredRoute != current.destination.route ||
        restoredScene != current.destination.scene ||
        !Kotonoha_KtrfRouterActivateNode(&adapter.router,
                                         current.destination.nodeIndex, error) ||
        !state.ResetChoicesForNode(current.destination.nodeIndex, error) ||
        !persistence.ReplaceCurrentScriptSnapshot(
            current.destination.sceneKey, UINT32_C(0x3f800000), state, error) ||
        !history.Rebase(current.destination, state, error)) {
        if (error != nullptr && error->code == KOTONOHA_KTRF_OK &&
            (restoredRoute != current.destination.route ||
             restoredScene != current.destination.scene)) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            std::snprintf(error->message, sizeof(error->message), "%s",
                          "School Days RouteMap checkpoint restored a different coordinate");
        }
        rollback();
        return false;
    }

    pendingHooks.clear();
    pendingEndings.clear();
    return true;
}

bool SchoolDaysKtrfSession::ResetNewGame(SchoolDaysKtrfScene* scene,
                                         Kotonoha_KtrfError* error) {
    if (!opened || scene == nullptr) return false;

    // RouterResetEntry resets every KTRF variable to its document default, but
    // native new-game only resets the local/session FlagsHash. GlobalFlag is
    // process-persistent and must survive returning from Title to a fresh game.
    // Preserve both runtime-backed and detached global projections across the
    // generic router reset.
    SchoolDaysStateStore::Snapshot before;
    std::unordered_map<std::string, int64_t> globalBefore;
    if (!state.Capture(&before, error) ||
        !state.EnumerateGlobalInts(&globalBefore, error)) {
        return false;
    }
    const SchoolDaysHistory historyBefore = history;
    const SchoolDaysPersistence persistenceBefore = persistence;
    const auto hooksBefore = pendingHooks;
    const auto endingsBefore = pendingEndings;

    auto rollback = [&]() {
        Kotonoha_KtrfError ignored{};
        (void)state.Restore(before, &ignored);
        history = historyBefore;
        persistence = persistenceBefore;
        pendingHooks = hooksBefore;
        pendingEndings = endingsBefore;
    };

    Kotonoha_SchoolDaysKtrfNodeView view{};
    if (!Kotonoha_SchoolDaysKtrfResetNewGame(&adapter, &view, error)) {
        rollback();
        return false;
    }

    std::vector<std::pair<std::string, int64_t>> globals;
    globals.reserve(globalBefore.size());
    for (const auto& entry : globalBefore) globals.emplace_back(entry.first, entry.second);
    if (!state.ReplaceGlobalInts(globals, error)) {
        rollback();
        return false;
    }

    Kotonoha_SchoolDaysKtrfOrsAsset asset{};
    if (!Kotonoha_SchoolDaysKtrfCurrentOrs(&adapter, assetsRoot.c_str(), &asset,
                                           error) ||
        !ResolveOrsPhysicalPath(&asset, error)) {
        rollback();
        return false;
    }

    size_t bytes = 0;
    if (!ProbeResolvedOrs(asset, &bytes, error)) {
        rollback();
        return false;
    }
    (void)bytes;

    pendingHooks.clear();
    pendingEndings.clear();
    history.Clear();
    persistence.ResetNewGame();
    return FillScene(asset, scene);
}

bool SchoolDaysKtrfSession::CurrentScene(SchoolDaysKtrfScene* scene,
                                         Kotonoha_KtrfError* error) const {
    if (!opened || scene == nullptr) return false;
    Kotonoha_SchoolDaysKtrfOrsAsset asset{};
    if (!Kotonoha_SchoolDaysKtrfCurrentOrs(&adapter, assetsRoot.c_str(), &asset,
                                           error) ||
        !ResolveOrsPhysicalPath(&asset, error)) {
        return false;
    }
    return FillScene(asset, scene);
}

bool SchoolDaysKtrfSession::ResolveSceneNode(uint32_t nodeIndex,
                                             SchoolDaysKtrfScene* scene,
                                             Kotonoha_KtrfError* error) const {
    if (!opened || scene == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days debug scene resolve argument is invalid");
        }
        return false;
    }

    Kotonoha_SchoolDaysKtrfOrsAsset asset{};
    if (!Kotonoha_SchoolDaysKtrfResolveNodeOrs(
            &adapter, nodeIndex, assetsRoot.c_str(), &asset, error) ||
        !ResolveOrsPhysicalPath(&asset, error)) {
        return false;
    }

    size_t bytes = 0;
    if (!ProbeResolvedOrs(asset, &bytes, error)) {
        return false;
    }
    (void)bytes;
    return FillScene(asset, scene);
}

bool SchoolDaysKtrfSession::ResolveSceneKey(
    const std::string& sceneKey, SchoolDaysKtrfScene* scene,
    Kotonoha_KtrfError* error) const {
    if (!opened || scene == nullptr || sceneKey.empty()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days read-only SceneKey resolve argument is invalid");
        }
        return false;
    }

    const Kotonoha_KtrfSection* nodes =
        Kotonoha_KtrfFindSection(adapter.document, "NODE");
    if (nodes == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_MISSING_SECTION;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days KTRF document has no NODE section");
        }
        return false;
    }

    uint32_t matchedNode = KOTONOHA_KTRF_NULL_INDEX;
    for (uint32_t nodeIndex = 0u; nodeIndex < nodes->item_count; ++nodeIndex) {
        Kotonoha_SchoolDaysKtrfNodeView view{};
        if (!Kotonoha_SchoolDaysKtrfGetNodeView(&adapter, nodeIndex, &view, error))
            return false;
        if (view.kind != KOTONOHA_SDHQ_NODE_SCENE || view.scene_key == nullptr ||
            view.scene_key_length != sceneKey.size() ||
            std::memcmp(view.scene_key, sceneKey.data(), sceneKey.size()) != 0) {
            continue;
        }
        if (matchedNode != KOTONOHA_KTRF_NULL_INDEX) {
            if (error != nullptr) {
                error->code = KOTONOHA_KTRF_ERROR_FORMAT;
                std::snprintf(error->message, sizeof(error->message),
                              "School Days SceneKey resolves to multiple physical nodes");
            }
            return false;
        }
        matchedNode = nodeIndex;
    }

    if (matchedNode == KOTONOHA_KTRF_NULL_INDEX) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_RANGE;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days SceneKey is absent from physical KTRF nodes");
        }
        return false;
    }
    return ResolveSceneNode(matchedNode, scene, error);
}

bool SchoolDaysKtrfSession::ResolveSemanticCoordinate(
    int route, int scene, SchoolDaysCoordinate* coordinate,
    SchoolDaysKtrfScene* resolvedScene, bool* routingOnly,
    Kotonoha_KtrfError* error) const {
    if (!opened || coordinate == nullptr || resolvedScene == nullptr ||
        routingOnly == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days semantic coordinate resolve argument is invalid");
        }
        return false;
    }
    *resolvedScene = {};
    *routingOnly = false;

    const char* expectedKey = SchoolDaysBackRules::SceneKeyFor(route, scene);
    if (expectedKey == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_RANGE;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days semantic coordinate is not a physical Back scene");
        }
        return false;
    }

    const Kotonoha_KtrfSection* nodes =
        Kotonoha_KtrfFindSection(adapter.document, "NODE");
    if (nodes == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_MISSING_SECTION;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days KTRF document has no NODE section");
        }
        return false;
    }

    const size_t expectedLength = std::strlen(expectedKey);
    for (uint32_t nodeIndex = 0u; nodeIndex < nodes->item_count; ++nodeIndex) {
        Kotonoha_SchoolDaysKtrfNodeView view{};
        if (!Kotonoha_SchoolDaysKtrfGetNodeView(&adapter, nodeIndex, &view, error))
            return false;
        bool matches = view.scene_key != nullptr &&
                       view.scene_key_length == expectedLength &&
                       std::memcmp(view.scene_key, expectedKey,
                                   expectedLength) == 0;
        if (!matches && view.kind == KOTONOHA_SDHQ_NODE_DISPATCHER) {
            Kotonoha_KtrfNode node{};
            const char* id = nullptr;
            size_t idLength = 0u;
            if (!Kotonoha_KtrfGetNode(adapter.document, nodeIndex, &node,
                                      error) ||
                !Kotonoha_KtrfGetString(adapter.document, node.id_str, &id,
                                        &idLength, error)) {
                return false;
            }
            static constexpr const char kNodePrefix[] = "sdhq:node:";
            constexpr size_t kPrefixLength = sizeof(kNodePrefix) - 1u;
            matches = id != nullptr &&
                      idLength == kPrefixLength + expectedLength &&
                      std::memcmp(id, kNodePrefix, kPrefixLength) == 0 &&
                      std::memcmp(id + kPrefixLength, expectedKey,
                                  expectedLength) == 0;
        }
        if (!matches) continue;
        coordinate->route = route;
        coordinate->scene = scene;
        coordinate->nodeIndex = nodeIndex;
        coordinate->sceneKey = expectedKey;
        if (view.kind == KOTONOHA_SDHQ_NODE_DISPATCHER) {
            *routingOnly = true;
            return true;
        }
        if (view.kind != KOTONOHA_SDHQ_NODE_SCENE) {
            if (error != nullptr) {
                error->code = KOTONOHA_KTRF_ERROR_FORMAT;
                std::snprintf(error->message, sizeof(error->message),
                              "School Days semantic Back node has unknown kind");
            }
            return false;
        }
        if (!ResolveSceneNode(nodeIndex, resolvedScene, error)) return false;
        return true;
    }

    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_ERROR_RANGE;
        std::snprintf(error->message, sizeof(error->message),
                      "School Days semantic Back scene is absent from KTRF nodes");
    }
    return false;
}

bool SchoolDaysKtrfSession::DebugActivateNode(uint32_t nodeIndex,
                                              Kotonoha_KtrfError* error) {
    if (!opened) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days debug router session is not open");
        }
        return false;
    }

    Kotonoha_SchoolDaysKtrfNodeView view{};
    if (!Kotonoha_SchoolDaysKtrfGetNodeView(&adapter, nodeIndex, &view, error)) {
        return false;
    }
    if (view.kind != KOTONOHA_SDHQ_NODE_SCENE) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days debug jump target %u is not a physical scene",
                          static_cast<unsigned>(nodeIndex));
        }
        return false;
    }

    if (!Kotonoha_KtrfRouterActivateNode(&adapter.router, nodeIndex, error)) {
        return false;
    }

    pendingHooks.clear();
    pendingEndings.clear();
    history.Clear();
    return true;
}

bool SchoolDaysKtrfSession::CommitChoice(int64_t value, int* accepted,
                                         Kotonoha_KtrfError* error) {
    if (!opened || accepted == nullptr) return false;

    uint32_t count = 0u;
    uint32_t choiceIndex = KOTONOHA_KTRF_NULL_INDEX;
    if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter.router, &count, error) ||
        count != 1u ||
        !Kotonoha_KtrfRouterCurrentChoiceAt(&adapter.router, 0u,
                                            &choiceIndex, error)) {
        return false;
    }
    const bool wasCommitted = adapter.router.choice_committed[choiceIndex] != 0u;

    SchoolDaysStateStore::Snapshot stateBefore;
    if (!state.Capture(&stateBefore, error)) return false;
    const SchoolDaysPersistence persistenceBefore = persistence;
    const bool topMeterBefore = topMeterRefreshRequested;

    if (wasCommitted && value < 0) {
        // A later timeout/reset (-1) must not override a pick already captured.
        *accepted = 1;
        return true;
    }

    if (!Kotonoha_SchoolDaysKtrfCommitChoice(&adapter, value, accepted, error)) {
        Kotonoha_KtrfError ignored{};
        (void)state.Restore(stateBefore, &ignored);
        persistence = persistenceBefore;
        topMeterRefreshRequested = topMeterBefore;
        return false;
    }
    if (*accepted != 0 && !wasCommitted) {
        Kotonoha_SchoolDaysKtrfNodeView view{};
        if (!Kotonoha_SchoolDaysKtrfCurrentNodeView(&adapter, &view, error) ||
            view.kind != KOTONOHA_SDHQ_NODE_SCENE || view.scene_key == nullptr ||
            !persistence.RecordChoice(
                std::string(view.scene_key, view.scene_key_length), value,
                error)) {
            Kotonoha_KtrfError ignored{};
            (void)state.Restore(stateBefore, &ignored);
            persistence = persistenceBefore;
            topMeterRefreshRequested = topMeterBefore;
            return false;
        }
    }
    return true;
}

bool SchoolDaysKtrfSession::Advance(const char* trigger, StepResult* out,
                                    Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr) return false;

    SchoolDaysStateStore::Snapshot stateBefore;
    if (!state.Capture(&stateBefore, error)) return false;
    const SchoolDaysPersistence persistenceBefore = persistence;
    const auto hooksBefore = pendingHooks;
    const auto endingsBefore = pendingEndings;
    const bool topMeterBefore = topMeterRefreshRequested;
    const auto rollback = [&]() {
        Kotonoha_KtrfError ignored{};
        (void)state.Restore(stateBefore, &ignored);
        persistence = persistenceBefore;
        pendingHooks = hooksBefore;
        pendingEndings = endingsBefore;
        topMeterRefreshRequested = topMeterBefore;
    };

    *out = StepResult{};
    pendingHooks.clear();
    pendingEndings.clear();

    Kotonoha_SchoolDaysKtrfAssetAdvanceResult result{};
    if (!Kotonoha_SchoolDaysKtrfAdvanceToOrs(
            &adapter, assetsRoot.c_str(), trigger, &result, error)) {
        rollback();
        return false;
    }

    out->transitionIndex = result.advance.route.transition_index;
    out->hopCount = result.advance.hop_count;
    out->hooks = pendingHooks;
    out->endings = pendingEndings;

    switch (result.advance.status) {
    case KOTONOHA_SDHQ_ADVANCE_UNRESOLVED:
        out->kind = StepKind::Unresolved;
        return true;
    case KOTONOHA_SDHQ_ADVANCE_BLOCKED_CHOICE:
        out->kind = StepKind::BlockedChoice;
        return true;
    case KOTONOHA_SDHQ_ADVANCE_ADVANCED: {
        size_t bytes = 0;
        out->kind = StepKind::Scene;
        if (!ResolveOrsPhysicalPath(&result.asset, error) ||
            !ProbeResolvedOrs(result.asset, &bytes, error)) {
            rollback();
            return false;
        }
        (void)bytes;
        if (!FillScene(result.asset, &out->scene)) {
            rollback();
            return false;
        }
        return true;
    }
    case KOTONOHA_SDHQ_ADVANCE_TERMINAL:
        out->kind = StepKind::Terminal;
        return true;
    default:
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days KTRF session received unknown advance status %u",
                          static_cast<unsigned>(result.advance.status));
        }
        rollback();
        return false;
    }
}

bool SchoolDaysKtrfSession::CommitTerminalRoutingReset(
    StepResult* terminal, Kotonoha_KtrfError* error) {
    if (!opened || terminal == nullptr || terminal->kind != StepKind::Terminal ||
        !terminal->HasHook("overflow.sdhq:callback_38")) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days terminal reset requires callback_38 terminal state");
        }
        return false;
    }

    SchoolDaysStateStore::Snapshot stateBefore;
    if (!state.Capture(&stateBefore, error)) return false;
    const SchoolDaysPersistence persistenceBefore = persistence;
    const SchoolDaysHistory historyBefore = history;

    // RouteProc FUN_10006760(...,1), exact authenticated order:
    //   1) global FLAG_LOGO=false through interface +0x1C (VT_BOOL/type11)
    //   2) interface +0x2C reset
    //   3) local ROUTE=-1 through interface +0x0C (VT_I4/type3)
    //   4) clear returned script string
    if (!persistence.WriteGlobalInteger("FLAG_LOGO", 11, 0, &state, error)) {
        Kotonoha_KtrfError ignored{};
        (void)state.Restore(stateBefore, &ignored);
        persistence = persistenceBefore;
        history = historyBefore;
        return false;
    }

    history.Clear();
    terminal->hooks.emplace_back("overflow.sdhq:callback_2C");

    if (!persistence.MarkLocalInteger("ROUTE", 3, error) ||
        !state.WriteSessionInt("ROUTE", -1, error)) {
        Kotonoha_KtrfError ignored{};
        (void)state.Restore(stateBefore, &ignored);
        persistence = persistenceBefore;
        history = historyBefore;
        terminal->hooks.pop_back();
        return false;
    }

    terminal->scene = {};
    pendingHooks = terminal->hooks;
    return true;
}

bool SchoolDaysKtrfSession::PlanBack(BackPlan* plan,
                                     Kotonoha_KtrfError* error) {
    if (!opened || plan == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days Back plan requires an open session/result");
        }
        return false;
    }

    *plan = BackPlan{};
    int liveRoute = 0;
    int liveScene = 0;
    if (!state.ReadCoordinate(&liveRoute, &liveScene, error)) return false;
    plan->source.route = liveRoute;
    plan->source.scene = liveScene;
    plan->source.nodeIndex = adapter.router.current_node_index;
    if (const char* key = SchoolDaysBackRules::SceneKeyFor(liveRoute, liveScene)) {
        plan->source.sceneKey = key;
        if (adapter.router.current_node_index == KOTONOHA_KTRF_NULL_INDEX) {
            if (error != nullptr) {
                error->code = KOTONOHA_KTRF_ERROR_FORMAT;
                std::snprintf(error->message, sizeof(error->message),
                              "School Days Back physical state has no active KTRF node");
            }
            return false;
        }
        Kotonoha_SchoolDaysKtrfNodeView sourceView{};
        if (!Kotonoha_SchoolDaysKtrfGetNodeView(
                &adapter, adapter.router.current_node_index, &sourceView, error)) {
            return false;
        }
        const size_t keyLength = std::strlen(key);
        if (sourceView.kind != KOTONOHA_SDHQ_NODE_SCENE ||
            sourceView.scene_key == nullptr || sourceView.scene_key_length != keyLength ||
            std::memcmp(sourceView.scene_key, key, keyLength) != 0) {
            if (error != nullptr) {
                error->code = KOTONOHA_KTRF_ERROR_FORMAT;
                std::snprintf(error->message, sizeof(error->message),
                              "School Days Back ROUTE/SCENE disagrees with active KTRF node");
            }
            return false;
        }
    }

    SchoolDaysBackRules::PredecessorResult predecessor;
    if (!SchoolDaysBackRules::ResolvePredecessor(
            liveRoute, liveScene, &state, &predecessor, error)) {
        return false;
    }
    switch (predecessor.kind) {
    case SchoolDaysBackRules::PredecessorKind::Resolved:
        {
        bool routingOnly = false;
        if (!ResolveSemanticCoordinate(predecessor.route, predecessor.scene,
                                       &plan->semanticPredecessor, &plan->scene,
                                       &routingOnly, error)) {
            return false;
        }
        if (routingOnly) {
            SchoolDaysStateStore::Snapshot before;
            if (!state.Capture(&before, error)) return false;

            std::vector<std::string> ignoredHooks;
            SchoolDaysBackRules::PredecessorResult collapsed;
            bool resolvedCollapsed = false;
            if (SchoolDaysBackRules::Apply(plan->source,
                                           plan->semanticPredecessor,
                                           &state, &ignoredHooks, error) &&
                state.WriteCoordinate(plan->semanticPredecessor.route,
                                      plan->semanticPredecessor.scene, error) &&
                SchoolDaysBackRules::ApplySourceOnly(
                    plan->semanticPredecessor, &state, &ignoredHooks, error) &&
                SchoolDaysBackRules::ResolvePredecessor(
                    plan->semanticPredecessor.route,
                    plan->semanticPredecessor.scene, &state, &collapsed,
                    error)) {
                resolvedCollapsed = true;
            }

            Kotonoha_KtrfError restoreError{};
            if (!state.Restore(before, &restoreError)) {
                if (error != nullptr) *error = restoreError;
                return false;
            }

            if (!resolvedCollapsed) return false;
            if (collapsed.kind !=
                SchoolDaysBackRules::PredecessorKind::Resolved) {
                if (error != nullptr) {
                    error->code = KOTONOHA_KTRF_ERROR_FORMAT;
                    std::snprintf(
                        error->message, sizeof(error->message),
                        "School Days routing-only Back predecessor did not collapse to a scene");
                }
                return false;
            }

            bool finalRoutingOnly = false;
            if (!ResolveSemanticCoordinate(collapsed.route, collapsed.scene,
                                           &plan->destination, &plan->scene,
                                           &finalRoutingOnly, error)) {
                return false;
            }
            if (finalRoutingOnly) {
                if (error != nullptr) {
                    error->code = KOTONOHA_KTRF_ERROR_FORMAT;
                    std::snprintf(
                        error->message, sizeof(error->message),
                        "School Days Back dispatcher chain did not reach a physical scene");
                }
                return false;
            }
        }
        else {
            plan->destination = plan->semanticPredecessor;
        }
        plan->kind = BackKind::Scene;
        return true;
        }
    case SchoolDaysBackRules::PredecessorKind::TrialPv:
        plan->kind = BackKind::TrialPv;
        return true;
    case SchoolDaysBackRules::PredecessorKind::Boundary:
        plan->kind = BackKind::Boundary;
        return true;
    case SchoolDaysBackRules::PredecessorKind::NoScript:
        plan->kind = BackKind::NoScript;
        return true;
    case SchoolDaysBackRules::PredecessorKind::Unresolved:
        plan->kind = BackKind::Unresolved;
        return true;
    }
    return false;
}

bool SchoolDaysKtrfSession::CommitBack(BackPlan* plan,
                                       Kotonoha_KtrfError* error) {
    if (!opened || plan == nullptr || plan->kind == BackKind::Unresolved) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days Back plan is unresolved or invalid");
        }
        return false;
    }

    int liveRoute = 0;
    int liveScene = 0;
    if (!state.ReadCoordinate(&liveRoute, &liveScene, error)) return false;
    if (liveRoute != plan->source.route || liveScene != plan->source.scene) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days Back plan source changed before commit");
        }
        return false;
    }

    SchoolDaysBackRules::PredecessorResult currentPredecessor;
    if (!SchoolDaysBackRules::ResolvePredecessor(
            liveRoute, liveScene, &state, &currentPredecessor, error)) {
        return false;
    }
    bool predecessorMatches = false;
    switch (plan->kind) {
    case BackKind::Scene:
        predecessorMatches =
            currentPredecessor.kind ==
                SchoolDaysBackRules::PredecessorKind::Resolved &&
            currentPredecessor.route == plan->semanticPredecessor.route &&
            currentPredecessor.scene == plan->semanticPredecessor.scene;
        break;
    case BackKind::Boundary:
        predecessorMatches =
            currentPredecessor.kind ==
            SchoolDaysBackRules::PredecessorKind::Boundary;
        break;
    case BackKind::TrialPv:
        predecessorMatches =
            currentPredecessor.kind ==
            SchoolDaysBackRules::PredecessorKind::TrialPv;
        break;
    case BackKind::NoScript:
        predecessorMatches =
            currentPredecessor.kind ==
            SchoolDaysBackRules::PredecessorKind::NoScript;
        break;
    case BackKind::Unresolved:
        predecessorMatches = false;
        break;
    }
    if (!predecessorMatches) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            std::snprintf(error->message, sizeof(error->message),
                          "School Days Back plan predicate changed before commit");
        }
        return false;
    }

    SchoolDaysStateStore::Snapshot before;
    if (!state.Capture(&before, error)) return false;
    const SchoolDaysHistory historyBefore = history;
    const SchoolDaysPersistence persistenceBefore = persistence;
    const auto hooksBefore = pendingHooks;
    const auto endingsBefore = pendingEndings;

    std::vector<std::string> backHooks = plan->hooks;
    std::vector<SchoolDaysBackRules::PersistenceWrite> persistenceWrites;
    bool applied = false;
    if (plan->kind == BackKind::Scene) {
        applied = SchoolDaysBackRules::Apply(plan->source,
                                             plan->semanticPredecessor,
                                             &state, &backHooks, error,
                                             &persistenceWrites);
    }
    else if (plan->kind == BackKind::Boundary) {
        applied = SchoolDaysBackRules::ApplyBoundary(
            &state, &backHooks, error, &persistenceWrites);
    }
    else if (plan->kind == BackKind::TrialPv) {
        applied = SchoolDaysBackRules::ApplyTrialPv(
            &state, &backHooks, error, &persistenceWrites);
    }
    else if (plan->kind == BackKind::NoScript) {
        applied = SchoolDaysBackRules::ApplySourceOnly(plan->source, &state,
                                                       &backHooks, error,
                                                       &persistenceWrites);
    }
    if (!applied) {
        Kotonoha_KtrfError ignored{};
        (void)state.Restore(before, &ignored);
        history = historyBefore;
        persistence = persistenceBefore;
        pendingHooks = hooksBefore;
        pendingEndings = endingsBefore;
        return false;
    }

    if (plan->kind == BackKind::Scene) {
        if (!(plan->semanticPredecessor == plan->destination)) {
            // The authenticated predecessor itself is a routing-only state.
            // It has no ORS, but it has its own Back handler. Collapse that
            // second reverse step inside the same transaction, preserving the
            // original handler order: publish intermediate ROUTE/SCENE, apply
            // its source effects, resolve predecessor, then clear destination SP.
            if (!state.WriteCoordinate(plan->semanticPredecessor.route,
                                       plan->semanticPredecessor.scene, error) ||
                !SchoolDaysBackRules::ApplySourceOnly(
                    plan->semanticPredecessor, &state, &backHooks, error,
                    &persistenceWrites)) {
                Kotonoha_KtrfError ignored{};
                (void)state.Restore(before, &ignored);
                history = historyBefore;
                persistence = persistenceBefore;
                pendingHooks = hooksBefore;
                pendingEndings = endingsBefore;
                return false;
            }

            SchoolDaysBackRules::PredecessorResult collapsed;
            if (!SchoolDaysBackRules::ResolvePredecessor(
                    plan->semanticPredecessor.route,
                    plan->semanticPredecessor.scene, &state, &collapsed,
                    error) ||
                collapsed.kind !=
                    SchoolDaysBackRules::PredecessorKind::Resolved ||
                collapsed.route != plan->destination.route ||
                collapsed.scene != plan->destination.scene) {
                if (error != nullptr && error->code == KOTONOHA_KTRF_OK) {
                    error->code = KOTONOHA_KTRF_ERROR_FORMAT;
                    std::snprintf(error->message, sizeof(error->message),
                                  "School Days collapsed Back predecessor changed before commit");
                }
                Kotonoha_KtrfError ignored{};
                (void)state.Restore(before, &ignored);
                history = historyBefore;
                persistence = persistenceBefore;
                pendingHooks = hooksBefore;
                pendingEndings = endingsBefore;
                return false;
            }
            if (!SchoolDaysBackRules::ApplyDestinationOnly(
                    plan->destination, &state, error, &persistenceWrites)) {
                Kotonoha_KtrfError ignored{};
                (void)state.Restore(before, &ignored);
                history = historyBefore;
                persistence = persistenceBefore;
                pendingHooks = hooksBefore;
                pendingEndings = endingsBefore;
                return false;
            }
        }

        for (const auto& write : persistenceWrites) {
            if (!persistence.MarkLocalInteger(write.symbol, write.type, error)) {
                Kotonoha_KtrfError ignored{};
                (void)state.Restore(before, &ignored);
                history = historyBefore;
                persistence = persistenceBefore;
                pendingHooks = hooksBefore;
                pendingEndings = endingsBefore;
                return false;
            }
        }

        if (!state.WriteCoordinate(plan->destination.route,
                                   plan->destination.scene, error) ||
            !Kotonoha_KtrfRouterActivateNode(&adapter.router,
                                             plan->destination.nodeIndex,
                                             error) ||
            !state.ResetChoicesForNode(plan->destination.nodeIndex, error)) {
            Kotonoha_KtrfError ignored{};
            (void)state.Restore(before, &ignored);
            history = historyBefore;
            persistence = persistenceBefore;
            pendingHooks = hooksBefore;
            pendingEndings = endingsBefore;
            return false;
        }

        bool checkpointRestored = false;
        if (!persistence.RestoreBackCheckpoint(
                plan->destination, &state, &checkpointRestored, error) ||
            !persistence.ReplaceCurrentScriptSnapshot(
                plan->destination.sceneKey, UINT32_C(0x3f800000), state,
                error)) {
            Kotonoha_KtrfError ignored{};
            (void)state.Restore(before, &ignored);
            history = historyBefore;
            persistence = persistenceBefore;
            pendingHooks = hooksBefore;
            pendingEndings = endingsBefore;
            return false;
        }
        (void)checkpointRestored;

        int restoredRoute = 0;
        int restoredScene = 0;
        if (!state.ReadCoordinate(&restoredRoute, &restoredScene, error) ||
            restoredRoute != plan->destination.route ||
            restoredScene != plan->destination.scene) {
            if (error != nullptr && error->code == KOTONOHA_KTRF_OK) {
                error->code = KOTONOHA_KTRF_ERROR_FORMAT;
                std::snprintf(error->message, sizeof(error->message),
                              "School Days Back checkpoint restored a different coordinate");
            }
            Kotonoha_KtrfError ignored{};
            (void)state.Restore(before, &ignored);
            history = historyBefore;
            persistence = persistenceBefore;
            pendingHooks = hooksBefore;
            pendingEndings = endingsBefore;
            return false;
        }

        if (!history.Rebase(plan->destination, state, error)) {
            Kotonoha_KtrfError ignored{};
            (void)state.Restore(before, &ignored);
            history = historyBefore;
            persistence = persistenceBefore;
            pendingHooks = hooksBefore;
            pendingEndings = endingsBefore;
            return false;
        }
    }
    else {
        for (const auto& write : persistenceWrites) {
            if (!persistence.MarkLocalInteger(write.symbol, write.type, error)) {
                Kotonoha_KtrfError ignored{};
                (void)state.Restore(before, &ignored);
                history = historyBefore;
                persistence = persistenceBefore;
                pendingHooks = hooksBefore;
                pendingEndings = endingsBefore;
                return false;
            }
        }
        const bool callback2c = std::any_of(
            backHooks.begin(), backHooks.end(), [](const std::string& hook) {
                return hook == "overflow.sdhq:callback_2c" ||
                       hook == "overflow.sdhq:callback_2C";
            });
        if (callback2c) history.Clear();
    }

    plan->hooks = backHooks;
    if (std::find(backHooks.begin(), backHooks.end(),
                  "overflow.sdhq:callback_30") != backHooks.end()) {
        topMeterRefreshRequested = true;
    }
    pendingHooks = std::move(backHooks);
    pendingEndings.clear();
    return true;
}

} // namespace Kotonoha
