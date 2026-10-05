#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>

#include <cstdio>
#include <cstring>

namespace Kotonoha {

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
                                    Kotonoha_KtrfError*) {
    auto* self = static_cast<SchoolDaysKtrfSession*>(userdata);
    if (self == nullptr) return 0;
    self->pendingEndings.push_back(endingCode);
    return 1;
}

int SchoolDaysKtrfSession::OnHook(const char* symbol, size_t symbolLength,
                                  void* userdata, Kotonoha_KtrfError*) {
    auto* self = static_cast<SchoolDaysKtrfSession*>(userdata);
    if (self == nullptr || symbol == nullptr) return 0;
    self->pendingHooks.emplace_back(symbol, symbolLength);
    return 1;
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
    callbacks.userdata = this;

    if (!Kotonoha_SchoolDaysKtrfAdapterBind(&adapter, &document, &callbacks, error)) {
        Kotonoha_KtrfClean(&document);
        Kotonoha_KtrfInit(&document);
        return false;
    }

    assetsRoot = root;
    pendingHooks.clear();
    pendingEndings.clear();
    opened = true;
    return true;
}

void SchoolDaysKtrfSession::Close() {
    if (opened || adapter.document != nullptr) {
        Kotonoha_SchoolDaysKtrfAdapterClean(&adapter);
    }
    Kotonoha_KtrfClean(&document);
    Kotonoha_KtrfInit(&document);
    Kotonoha_SchoolDaysKtrfAdapterInit(&adapter);
    assetsRoot.clear();
    pendingHooks.clear();
    pendingEndings.clear();
    opened = false;
}

bool SchoolDaysKtrfSession::ResetNewGame(SchoolDaysKtrfScene* scene,
                                         Kotonoha_KtrfError* error) {
    if (!opened || scene == nullptr) return false;

    Kotonoha_SchoolDaysKtrfNodeView view{};
    if (!Kotonoha_SchoolDaysKtrfResetNewGame(&adapter, &view, error)) {
        return false;
    }

    Kotonoha_SchoolDaysKtrfOrsAsset asset{};
    if (!Kotonoha_SchoolDaysKtrfCurrentOrs(&adapter, assetsRoot.c_str(), &asset,
                                           error)) {
        return false;
    }

    size_t bytes = 0;
    if (!Kotonoha_SchoolDaysKtrfProbeOrs(&asset, &bytes, error)) {
        return false;
    }
    (void)bytes;

    pendingHooks.clear();
    pendingEndings.clear();
    return FillScene(asset, scene);
}

bool SchoolDaysKtrfSession::CurrentScene(SchoolDaysKtrfScene* scene,
                                         Kotonoha_KtrfError* error) const {
    if (!opened || scene == nullptr) return false;
    Kotonoha_SchoolDaysKtrfOrsAsset asset{};
    if (!Kotonoha_SchoolDaysKtrfCurrentOrs(&adapter, assetsRoot.c_str(), &asset,
                                           error)) {
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
            &adapter, nodeIndex, assetsRoot.c_str(), &asset, error)) {
        return false;
    }

    size_t bytes = 0;
    if (!Kotonoha_SchoolDaysKtrfProbeOrs(&asset, &bytes, error)) {
        return false;
    }
    (void)bytes;
    return FillScene(asset, scene);
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
    return true;
}

bool SchoolDaysKtrfSession::CommitChoice(int64_t value, int* accepted,
                                         Kotonoha_KtrfError* error) {
    if (!opened) return false;
    return Kotonoha_SchoolDaysKtrfCommitChoice(&adapter, value, accepted, error) != 0;
}

bool SchoolDaysKtrfSession::Advance(const char* trigger, StepResult* out,
                                    Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr) return false;

    *out = StepResult{};
    pendingHooks.clear();
    pendingEndings.clear();

    Kotonoha_SchoolDaysKtrfAssetAdvanceResult result{};
    if (!Kotonoha_SchoolDaysKtrfAdvanceToOrs(
            &adapter, assetsRoot.c_str(), trigger, &result, error)) {
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
        if (!Kotonoha_SchoolDaysKtrfProbeOrs(&result.asset, &bytes, error)) {
            return false;
        }
        (void)bytes;
        return FillScene(result.asset, &out->scene);
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
        return false;
    }
}

} // namespace Kotonoha
