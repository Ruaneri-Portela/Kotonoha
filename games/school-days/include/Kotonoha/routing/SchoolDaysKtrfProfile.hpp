#pragma once

extern "C" {
#include <Kotonoha/routing/KtrfAdapter.h>
}

namespace Kotonoha {

struct SchoolDaysKtrfRuntimeConfig {
    int (*validateSceneKey)(const char* sceneKey, size_t length,
                            Kotonoha_KtrfError* error) = nullptr;
    const char* assetExtension = nullptr;
    const char* sessionVariablePrefix = nullptr;
    const char* globalVariablePrefix = nullptr;
    const char* internalVariablePrefix = nullptr;
    const char* routeSymbol = nullptr;
    const char* sceneSymbol = nullptr;
    const char* choiceResultSymbol = nullptr;
    int64_t pendingChoiceValue = -2;
};

const Kotonoha_KtrfProfileConfig& SchoolDaysKtrfProfile();
const SchoolDaysKtrfRuntimeConfig& SchoolDaysKtrfRuntimeProfile();

} // namespace Kotonoha
