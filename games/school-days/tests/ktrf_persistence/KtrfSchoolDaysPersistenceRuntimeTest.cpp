#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>
#include <Kotonoha/routing/SchoolDaysSaveService.hpp>
#include <Kotonoha/routing/SchoolDaysEndBackgroundResolver.hpp>
#include <Kotonoha/routing/SchoolDaysBackRules.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

struct Kotonoha_Game {};

namespace Kotonoha {
class Gameplay {
public:
    explicit Gameplay(std::string p) : path(std::move(p)) {}
    std::string path;
};
}

namespace fs = std::filesystem;

namespace {

struct FactoryState {
    uint32_t creates = 0u;
    uint32_t destroys = 0u;
    uint32_t live = 0u;
    uint32_t maxLive = 0u;
    bool failNextCreate = false;
};

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "KtrfSchoolDaysPersistenceRuntimeTest: " << message << '\n';
    std::exit(2);
}

void Check(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

std::vector<uint8_t> ReadBytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) Fail("cannot open " + path.string());
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0) Fail("cannot size " + path.string());
    stream.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
    }
    if (!stream) Fail("cannot read " + path.string());
    return bytes;
}

Kotonoha::Gameplay* FakeCreate(const char* path, Kotonoha_Game*, void* userdata) {
    auto* state = static_cast<FactoryState*>(userdata);
    if (state == nullptr || path == nullptr || *path == '\0') return nullptr;
    if (state->failNextCreate) {
        state->failNextCreate = false;
        return nullptr;
    }
    ++state->creates;
    ++state->live;
    if (state->live > state->maxLive) state->maxLive = state->live;
    return new Kotonoha::Gameplay(path);
}

void FakeDestroy(Kotonoha::Gameplay* gameplay, void* userdata) {
    auto* state = static_cast<FactoryState*>(userdata);
    if (gameplay == nullptr || state == nullptr) return;
    delete gameplay;
    ++state->destroys;
    if (state->live == 0u) Fail("factory live-count underflow");
    --state->live;
}

int64_t SessionInt(Kotonoha::SchoolDaysKtrfGameplayBridge& bridge,
                   const char* key, Kotonoha_KtrfError* error) {
    int64_t value = 0;
    if (!bridge.Session().State().ReadSessionInt(key, &value, error)) {
        Fail(error->message);
    }
    return value;
}

int64_t GlobalInt(Kotonoha::SchoolDaysKtrfGameplayBridge& bridge,
                  const char* key, Kotonoha_KtrfError* error) {
    int64_t value = 0;
    if (!bridge.Session().State().ReadGlobalInt(key, &value, error)) {
        Fail(error->message);
    }
    return value;
}

const Kotonoha::SchoolDaysFlgHEntry* FindFlag(
    const Kotonoha::SchoolDaysFlgH& node, const std::u16string& key) {
    for (const auto& entry : node.entries) {
        if (entry.key == key) return &entry;
    }
    return nullptr;
}

size_t CountTag(const Kotonoha::SchoolDaysSLog& log, int32_t tag) {
    size_t count = 0u;
    for (const auto& record : log.records) {
        if (record.tag == tag) ++count;
    }
    return count;
}

const Kotonoha::SchoolDaysSLogRecord* FindTag3Secondary(
    const Kotonoha::SchoolDaysSLog& log, const std::u16string& secondary) {
    for (const auto& record : log.records) {
        if (record.tag == 3 && record.secondary == secondary) return &record;
    }
    return nullptr;
}

const Kotonoha::SchoolDaysSLogRecord* FindTag4Name(
    const Kotonoha::SchoolDaysSLog& log, const std::u16string& name) {
    for (const auto& record : log.records) {
        if (record.tag == 4 && record.name == name) return &record;
    }
    return nullptr;
}

int64_t FirstChoiceValue(const Kotonoha_KtrfAdapter* adapter,
                         Kotonoha_KtrfError* error) {
    uint32_t count = 0u;
    if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router, &count, error))
        Fail(error->message);
    Check(count == 1u, "expected exactly one current choice");
    uint32_t choiceIndex = KOTONOHA_KTRF_NULL_INDEX;
    if (!Kotonoha_KtrfRouterCurrentChoiceAt(&adapter->router, 0u,
                                            &choiceIndex, error))
        Fail(error->message);
    Kotonoha_KtrfChoice choice{};
    if (!Kotonoha_KtrfGetChoice(adapter->document, choiceIndex, &choice, error))
        Fail(error->message);
    Check(choice.option_count > 0u, "choice has no options");
    Kotonoha_KtrfChoiceOption option{};
    if (!Kotonoha_KtrfGetChoiceOption(adapter->document, choiceIndex, 0u,
                                      &option, error))
        Fail(error->message);
    Check(option.value.kind == KOTONOHA_KTRF_SCALAR_INT64,
          "first choice value is not int64");
    return option.value.as.int64_value;
}

uint32_t FindEffectById(const Kotonoha_KtrfDocument* document,
                        const std::string& expected,
                        Kotonoha_KtrfError* error) {
    const Kotonoha_KtrfSection* effects =
        Kotonoha_KtrfFindSection(document, "EFFT");
    if (effects == nullptr) Fail("KTRF document has no EFFT section");
    for (uint32_t i = 0u; i < effects->item_count; ++i) {
        Kotonoha_KtrfEffect effect{};
        const char* raw = nullptr;
        size_t size = 0u;
        if (!Kotonoha_KtrfGetEffect(document, i, &effect, error) ||
            !Kotonoha_KtrfGetString(document, effect.id_str,
                                    &raw, &size, error)) {
            Fail(error->message);
        }
        if (raw != nullptr && size == expected.size() &&
            std::string(raw, size) == expected) {
            return i;
        }
    }
    Fail("effect id not found: " + expected);
}

uint32_t FindNodeBySceneKey(const Kotonoha_KtrfAdapter* adapter,
                            const std::string& sceneKey,
                            Kotonoha_KtrfError* error) {
    if (adapter == nullptr || adapter->document == nullptr)
        Fail("node lookup adapter is null");
    const Kotonoha_KtrfSection* nodes =
        Kotonoha_KtrfFindSection(adapter->document, "NODE");
    if (nodes == nullptr) Fail("KTRF document has no NODE section");
    for (uint32_t i = 0u; i < nodes->item_count; ++i) {
        Kotonoha_KtrfNodeView view{};
        if (!Kotonoha_KtrfGetNodeView(adapter, i, &view, error))
            Fail(error->message);
        if (view.kind == KOTONOHA_KTRF_NODE_SCENE &&
            view.scene_key != nullptr &&
            view.scene_key_length == sceneKey.size() &&
            std::string(view.scene_key, view.scene_key_length) == sceneKey) {
            return i;
        }
    }
    Fail("scene node not found: " + sceneKey);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: KtrfSchoolDaysPersistenceRuntimeTest "
                     "<route.ktnroute> <assets-root> <copied-save-dir>\n";
        return 2;
    }

    const fs::path saveRoot(argv[3]);
    const auto globalBytes = ReadBytes(saveRoot / "GlobalFlag.DAT");
    const auto saveBytes = ReadBytes(saveRoot / "SaveFile0030.DAT");

    FactoryState factoryState;
    Kotonoha::SchoolDaysKtrfGameplayBridge::Factory factory;
    factory.create = &FakeCreate;
    factory.destroy = &FakeDestroy;
    factory.userdata = &factoryState;
    Kotonoha_Game gameContext{};
    Kotonoha_KtrfError error{};
    Kotonoha::SchoolDaysKtrfGameplayBridge bridge;
    if (!bridge.Open(argv[1], argv[2], &gameContext, factory, &error)) {
        Fail(std::string("open failed: ") + error.message);
    }

    // GlobalFlag is a persisted FlagsHash subset, not the complete KTRF global
    // namespace. Loading it must not zero the KTRF-only default-one variable.
    Check(GlobalInt(bridge, "dword_3A2294", &error) == 1,
          "KTRF-only global default changed before GlobalFlag load");
    if (!bridge.Session().LoadGlobalFlag(globalBytes, &error)) Fail(error.message);
    Check(GlobalInt(bridge, "dword_3A2294", &error) == 1,
          "GlobalFlag load zeroed KTRF-only default-one variable");
    Check(GlobalInt(bridge, "dword_3A6F40", &error) == 0,
          "GlobalFlag load changed KTRF-only default-zero variable");
    Check(GlobalInt(bridge, "REP05_KC_E01", &error) == -1,
          "unmapped persisted REP05_KC_E01 was not preserved as detached global state");

    std::vector<uint8_t> globalRoundtrip;
    if (!bridge.Session().SerializeGlobalFlag(&globalRoundtrip, &error))
        Fail(error.message);
    Check(globalRoundtrip == globalBytes,
          "runtime GlobalFlag projection did not serialize byte-identically");

    // Mutating one mapped REP must not disturb an unmapped persisted-only REP
    // or any opaque type8 SaveConfig metadata from the loaded GlobalFlag.
    Kotonoha::SchoolDaysFlgH originalGlobal;
    std::string loadedGlobalCodecError;
    if (!Kotonoha::SchoolDaysPersistenceCodec::ParseFlgH(
            globalBytes, &originalGlobal, &loadedGlobalCodecError))
        Fail(loadedGlobalCodecError);
    const auto* originalOpaqueRep = FindFlag(originalGlobal, u"REP05_KC_E01");
    const auto* originalSaveConfig =
        FindFlag(originalGlobal, u"FILMEngine/SaveFile000");
    Check(originalOpaqueRep != nullptr && originalOpaqueRep->type == 11 &&
              originalSaveConfig != nullptr && originalSaveConfig->type == 8,
          "authenticated GlobalFlag preservation witnesses are missing");
    const auto stateBeforeGlobalProjection = [&]() {
        Kotonoha::KtrfStateStore::Snapshot snapshot;
        if (!bridge.Session().State().Capture(&snapshot, &error))
            Fail(error.message);
        return snapshot;
    }();
    if (!bridge.Session().State().WriteGlobalInt("REP02_2K_J03", 1, &error))
        Fail(error.message);
    std::vector<uint8_t> projectedGlobalBytes;
    if (!bridge.Session().SerializeGlobalFlag(&projectedGlobalBytes, &error))
        Fail(error.message);
    Kotonoha::SchoolDaysFlgH projectedGlobal;
    if (!Kotonoha::SchoolDaysPersistenceCodec::ParseFlgH(
            projectedGlobalBytes, &projectedGlobal, &loadedGlobalCodecError))
        Fail(loadedGlobalCodecError);
    const auto* projectedMappedRep = FindFlag(projectedGlobal, u"REP02_2K_J03");
    const auto* projectedOpaqueRep = FindFlag(projectedGlobal, u"REP05_KC_E01");
    const auto* projectedSaveConfig =
        FindFlag(projectedGlobal, u"FILMEngine/SaveFile000");
    Check(projectedMappedRep != nullptr && projectedMappedRep->type == 11 &&
              projectedMappedRep->integer == -1 &&
              projectedOpaqueRep != nullptr &&
              projectedOpaqueRep->identifier == originalOpaqueRep->identifier &&
              projectedOpaqueRep->type == originalOpaqueRep->type &&
              projectedOpaqueRep->integer == originalOpaqueRep->integer &&
              projectedSaveConfig != nullptr &&
              projectedSaveConfig->identifier == originalSaveConfig->identifier &&
              projectedSaveConfig->type == originalSaveConfig->type &&
              projectedSaveConfig->stringValue == originalSaveConfig->stringValue,
          "mapped GlobalFlag projection disturbed opaque REP/type8 metadata");
    if (!bridge.Session().State().Restore(stateBeforeGlobalProjection, &error))
        Fail(error.message);

    // New-game live persistence begins sparse.  The first authenticated
    // callback_00 at A00 snapshots exactly the seven baseline fields, then
    // creates SP100 locally/globally.  A failed physical swap must roll all of
    // those persistence mutations back together with KTRF state.
    Check(bridge.CurrentScene().sceneKey == "00/00-00-A00",
          "unexpected new-game scene before callback_00 test");
    Kotonoha::SchoolDaysFlgH freshLocal;
    if (!bridge.Session().Persistence().CaptureLocalFlgH(
            bridge.Session().State(), &freshLocal, &error))
        Fail(error.message);
    Check(freshLocal.entries.size() == 7u &&
              FindFlag(freshLocal, u"000") != nullptr &&
              FindFlag(freshLocal, u"004") != nullptr &&
              FindFlag(freshLocal, u"ROUTE") != nullptr &&
              FindFlag(freshLocal, u"SCENE") != nullptr &&
              FindFlag(freshLocal, u"SP100") == nullptr,
          "new-game sparse FlgH baseline mismatch");

    const auto globalBeforeFailedAdvance = globalRoundtrip;
    factoryState.failNextCreate = true;
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult liveStep;
    Check(!bridge.Advance("ktrf:next", &liveStep, &error),
          "forced first-scene swap failure unexpectedly succeeded");
    Check(bridge.CurrentScene().sceneKey == "00/00-00-A00" &&
              CountTag(bridge.Session().Persistence().SaveLogDocument(), 3) == 0u,
          "failed first-scene swap retained callback_00 checkpoint");
    std::vector<uint8_t> globalAfterFailedAdvance;
    if (!bridge.Session().SerializeGlobalFlag(&globalAfterFailedAdvance, &error))
        Fail(error.message);
    Check(globalAfterFailedAdvance == globalBeforeFailedAdvance,
          "failed first-scene swap retained callback_00 GlobalFlag mutation");

    if (!bridge.Advance("ktrf:next", &liveStep, &error))
        Fail(std::string("first live advance failed: ") + error.message);
    Check(liveStep.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              bridge.CurrentScene().sceneKey == "00/00-00-A01",
          "first live advance did not reach A01");
    const auto* firstCheckpoint = FindTag3Secondary(
        bridge.Session().Persistence().SaveLogDocument(), u"SP100");
    Check(firstCheckpoint != nullptr && firstCheckpoint->integer == 0 &&
              firstCheckpoint->name == u"00/00-00-A00" &&
              firstCheckpoint->flags.entries.size() == 7u &&
              FindFlag(firstCheckpoint->flags, u"SP100") == nullptr,
          "callback_00 did not snapshot pre-SP local FlgH exactly");
    Kotonoha::SchoolDaysFlgH afterCheckpoint;
    if (!bridge.Session().Persistence().CaptureLocalFlgH(
            bridge.Session().State(), &afterCheckpoint, &error))
        Fail(error.message);
    const auto* localSp100 = FindFlag(afterCheckpoint, u"SP100");
    const auto* globalSp100 = FindFlag(
        bridge.Session().Persistence().GlobalFlagDocument(), u"SP100");
    Check(localSp100 != nullptr && localSp100->type == 11 &&
              localSp100->integer == -1 &&
              globalSp100 != nullptr && globalSp100->type == 11 &&
              globalSp100->integer == -1,
          "callback_00 did not create local/global SP100 bool state");
    Check(CountTag(bridge.Session().Persistence().SaveLogDocument(), 1) == 1u &&
              bridge.Session().Persistence().SaveLogDocument().records.front().name ==
                  u"00/00-00-A01",
          "physical A01 activation did not replace current tag1");

    // A01 -> Back -> A00 uses the persisted SP100 checkpoint.  Failing the
    // predecessor Gameplay creation must leave both tag3 and tag1 untouched;
    // successful Back applies the checkpoint, prunes SP100 inclusively and
    // refreshes the sole tag1 to the restored active script.
    factoryState.failNextCreate = true;
    Check(!bridge.Back(&liveStep, &error),
          "forced Back Gameplay failure unexpectedly succeeded");
    Check(bridge.CurrentScene().sceneKey == "00/00-00-A01" &&
              FindTag3Secondary(bridge.Session().Persistence().SaveLogDocument(),
                                u"SP100") != nullptr &&
              CountTag(bridge.Session().Persistence().SaveLogDocument(), 1) == 1u &&
              bridge.Session().Persistence().SaveLogDocument().records.front().name ==
                  u"00/00-00-A01",
          "failed Back mutated persistent checkpoint/tag1 state");

    if (!bridge.Back(&liveStep, &error))
        Fail(std::string("live Back to A00 failed: ") + error.message);
    Check(liveStep.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              bridge.CurrentScene().sceneKey == "00/00-00-A00" &&
              CountTag(bridge.Session().Persistence().SaveLogDocument(), 3) == 0u &&
              CountTag(bridge.Session().Persistence().SaveLogDocument(), 1) == 1u &&
              bridge.Session().Persistence().SaveLogDocument().records.front().name ==
                  u"00/00-00-A00" &&
              SessionInt(bridge, "SP100", &error) == 0,
          "Back checkpoint restore/prune/tag1 activation mismatch");

    if (!bridge.Advance("ktrf:next", &liveStep, &error))
        Fail(std::string("re-advance after Back failed: ") + error.message);
    Check(bridge.CurrentScene().sceneKey == "00/00-00-A01" &&
              FindTag3Secondary(bridge.Session().Persistence().SaveLogDocument(),
                                u"SP100") != nullptr &&
              CountTag(bridge.Session().Persistence().SaveLogDocument(), 1) == 1u &&
              bridge.Session().Persistence().SaveLogDocument().records.front().name ==
                  u"00/00-00-A01",
          "scene activation after Back did not recreate checkpoint/current tag1");

    // The effect-type oracle must distinguish native bool setters from integer
    // setters and ignore the synthetic choice_result effect.
    Kotonoha::SchoolDaysPersistence effectStore;
    effectStore.ResetNewGame();
    if (!effectStore.ValidateEffectOracle(bridge.Session().Adapter()->document,
                                         &error))
        Fail(error.message);
    Kotonoha::KtrfStateStore::Snapshot stateBeforeOracleProbe;
    if (!bridge.Session().State().Capture(&stateBeforeOracleProbe, &error))
        Fail(error.message);
    const uint32_t bool999Effect = FindEffectById(
        bridge.Session().Adapter()->document, "sdhq:effect:source:197", &error);
    const uint32_t choiceResetEffect = FindEffectById(
        bridge.Session().Adapter()->document,
        "sdhq:effect:profile:reset-choice-result", &error);
    if (!effectStore.ObserveRoutingEffect(bool999Effect, &error)) Fail(error.message);
    if (!bridge.Session().State().WriteSessionInt("999", 1, &error)) Fail(error.message);
    Kotonoha::SchoolDaysFlgH typedSnapshot;
    if (!effectStore.CaptureLocalFlgH(bridge.Session().State(),
                                     &typedSnapshot, &error))
        Fail(error.message);
    const size_t typedCount = typedSnapshot.entries.size();
    if (!effectStore.ObserveRoutingEffect(choiceResetEffect, &error)) Fail(error.message);
    if (!effectStore.CaptureLocalFlgH(bridge.Session().State(),
                                     &typedSnapshot, &error))
        Fail(error.message);
    const auto* flag999 = FindFlag(typedSnapshot, u"999");
    Check(flag999 != nullptr && flag999->type == 11 && flag999->integer == -1 &&
              typedSnapshot.entries.size() == typedCount,
          "persistence effect type oracle polluted/mistyped sparse FlgH");

    // Global SetGlobalConst effects are pinned separately from the sparse local
    // FlgH oracle.  A fresh document must create REP* as VT_BOOL/type11 true,
    // while the local FlgH shape remains untouched.  Serializing with KTRF's
    // runtime value 1 must canonicalize back to native FlgH bool -1.
    Kotonoha::SchoolDaysPersistence freshGlobalStore;
    freshGlobalStore.ResetNewGame();
    if (!freshGlobalStore.ValidateEffectOracle(
            bridge.Session().Adapter()->document, &error))
        Fail(error.message);
    const uint32_t repEffect = FindEffectById(
        bridge.Session().Adapter()->document, "sdhq:effect:source:857", &error);
    Kotonoha::SchoolDaysFlgH localBeforeRep;
    if (!freshGlobalStore.CaptureLocalFlgH(bridge.Session().State(),
                                          &localBeforeRep, &error))
        Fail(error.message);
    if (!bridge.Session().State().WriteGlobalInt("REP02_2K_J03", 1, &error) ||
        !freshGlobalStore.ObserveRoutingEffect(repEffect, &error))
        Fail(error.message);
    const auto* freshRep = FindFlag(
        freshGlobalStore.GlobalFlagDocument(), u"REP02_2K_J03");
    Kotonoha::SchoolDaysFlgH localAfterRep;
    if (!freshGlobalStore.CaptureLocalFlgH(bridge.Session().State(),
                                          &localAfterRep, &error))
        Fail(error.message);
    Check(freshRep != nullptr && freshRep->type == 11 && freshRep->integer == -1 &&
              localAfterRep.entries.size() == localBeforeRep.entries.size() &&
              FindFlag(localAfterRep, u"REP02_2K_J03") == nullptr,
          "global REP observer did not create isolated type11/-1 GlobalFlag entry");
    std::vector<uint8_t> freshGlobalBytes;
    if (!freshGlobalStore.SerializeGlobalFlag(bridge.Session().State(),
                                              &freshGlobalBytes, &error))
        Fail(error.message);
    Kotonoha::SchoolDaysFlgH freshGlobalParsed;
    std::string freshGlobalCodecError;
    if (!Kotonoha::SchoolDaysPersistenceCodec::ParseFlgH(
            freshGlobalBytes, &freshGlobalParsed, &freshGlobalCodecError))
        Fail(freshGlobalCodecError);
    const auto* serializedRep = FindFlag(freshGlobalParsed, u"REP02_2K_J03");
    Check(serializedRep != nullptr && serializedRep->type == 11 &&
              serializedRep->integer == -1,
          "GlobalFlag serializer did not canonicalize REP runtime 1 to bool -1");

    // Manual RouteProc Back writes bypass the KTRF effect observer, so
    // BackRules must carry native persistence types explicitly.
    std::vector<std::string> manualBackHooks;
    std::vector<Kotonoha::SchoolDaysBackRules::PersistenceWrite> manualBackWrites;
    std::vector<Kotonoha::SchoolDaysBackRules::PersistenceWrite> allManualBackWrites;
    Kotonoha::SchoolDaysCoordinate feelingSource{};
    feelingSource.route = 0;
    feelingSource.scene = 5;
    if (!Kotonoha::SchoolDaysBackRules::ApplySourceOnly(
            feelingSource, &bridge.Session().State(), &manualBackHooks, &error,
            &manualBackWrites))
        Fail(error.message);
    Check(std::any_of(manualBackWrites.begin(), manualBackWrites.end(),
                      [](const auto& write) {
                          return write.symbol == "001" && write.type == 3u;
                      }),
          "Back feeling inverse did not expose type3 persistence write");
    allManualBackWrites.insert(allManualBackWrites.end(),
                               manualBackWrites.begin(), manualBackWrites.end());

    manualBackHooks.clear();
    manualBackWrites.clear();
    Kotonoha::SchoolDaysCoordinate flagClearSource{};
    flagClearSource.route = 1;
    flagClearSource.scene = 58;
    if (!Kotonoha::SchoolDaysBackRules::ApplySourceOnly(
            flagClearSource, &bridge.Session().State(), &manualBackHooks, &error,
            &manualBackWrites))
        Fail(error.message);
    Check(std::any_of(manualBackWrites.begin(), manualBackWrites.end(),
                      [](const auto& write) {
                          return write.symbol == "986" && write.type == 11u;
                      }),
          "Back flag clear did not expose type11 persistence write");
    allManualBackWrites.insert(allManualBackWrites.end(),
                               manualBackWrites.begin(), manualBackWrites.end());

    manualBackHooks.clear();
    manualBackWrites.clear();
    if (!bridge.Session().State().WriteSessionInt("BS0100I00", 0x25, &error) ||
        !bridge.Session().State().WriteSessionInt("999", 77, &error))
        Fail(error.message);
    Kotonoha::SchoolDaysCoordinate directIntSource{};
    directIntSource.route = 1;
    directIntSource.scene = 51;
    if (!Kotonoha::SchoolDaysBackRules::ApplySourceOnly(
            directIntSource, &bridge.Session().State(), &manualBackHooks, &error,
            &manualBackWrites))
        Fail(error.message);
    Check(std::any_of(manualBackWrites.begin(), manualBackWrites.end(),
                      [](const auto& write) {
                          return write.symbol == "999" && write.type == 3u;
                      }) &&
              SessionInt(bridge, "999", &error) == 0,
          "Back direct-int zero did not expose/materialize 999 as type3");
    allManualBackWrites.insert(allManualBackWrites.end(),
                               manualBackWrites.begin(), manualBackWrites.end());

    manualBackWrites.clear();
    Kotonoha::SchoolDaysCoordinate spDestination{};
    spDestination.route = 0;
    spDestination.scene = 0;
    if (!Kotonoha::SchoolDaysBackRules::ApplyDestinationOnly(
            spDestination, &bridge.Session().State(), &error,
            &manualBackWrites))
        Fail(error.message);
    Check(std::any_of(manualBackWrites.begin(), manualBackWrites.end(),
                      [](const auto& write) {
                          return write.symbol == "SP100" && write.type == 11u;
                      }),
          "Back reverse SP clear did not expose type11 persistence write");
    allManualBackWrites.insert(allManualBackWrites.end(),
                               manualBackWrites.begin(), manualBackWrites.end());

    Kotonoha::SchoolDaysPersistence materializedBackStore;
    materializedBackStore.ResetNewGame();
    for (const auto& write : allManualBackWrites) {
        if (!materializedBackStore.MarkLocalInteger(write.symbol, write.type, &error))
            Fail(error.message);
    }
    Kotonoha::SchoolDaysFlgH materializedBackFlags;
    if (!materializedBackStore.CaptureLocalFlgH(
            bridge.Session().State(), &materializedBackFlags, &error))
        Fail(error.message);
    const auto* materialized986 = FindFlag(materializedBackFlags, u"986");
    const auto* materialized999 = FindFlag(materializedBackFlags, u"999");
    const auto* materializedSp100 = FindFlag(materializedBackFlags, u"SP100");
    Check(materialized986 != nullptr && materialized986->type == 11 &&
              materialized999 != nullptr && materialized999->type == 3 &&
              materializedSp100 != nullptr && materializedSp100->type == 11,
          "manual Back writes did not materialize native sparse FlgH types");

    if (!bridge.Session().State().Restore(stateBeforeOracleProbe, &error))
        Fail(error.message);

    // Production rollback witness: fail CommitBack only after source effects
    // and sparse persistence metadata have been applied.  Both StateStore and
    // Persistence must return to the pre-commit image.
    {
        Kotonoha::SchoolDaysKtrfSession rollbackSession;
        if (!rollbackSession.Open(argv[1], argv[2], &error)) Fail(error.message);
        Kotonoha::SchoolDaysKtrfScene rollbackFirst;
        if (!rollbackSession.ResetNewGame(&rollbackFirst, &error)) Fail(error.message);
        const uint32_t sourceNode = FindNodeBySceneKey(
            rollbackSession.Adapter(), "01/01-00-I00", &error);
        if (!rollbackSession.DebugActivateNode(sourceNode, &error) ||
            !rollbackSession.State().WriteCoordinate(1, 51, &error) ||
            !rollbackSession.State().WriteSessionInt("BS0100I00", 0x25, &error) ||
            !rollbackSession.State().WriteSessionInt("999", 77, &error))
            Fail(error.message);

        Kotonoha::SchoolDaysKtrfSession::BackPlan rollbackPlan;
        if (!rollbackSession.PlanBack(&rollbackPlan, &error)) Fail(error.message);
        Check(rollbackPlan.kind == Kotonoha::SchoolDaysKtrfSession::BackKind::Scene &&
                  rollbackPlan.destination.route == 1 &&
                  rollbackPlan.destination.scene == 37,
              "rollback witness did not resolve authenticated R1/S51 predecessor");

        Kotonoha::SchoolDaysFlgH rollbackLocalBefore;
        if (!rollbackSession.Persistence().CaptureLocalFlgH(
                rollbackSession.State(), &rollbackLocalBefore, &error))
            Fail(error.message);
        std::vector<uint8_t> rollbackLocalBeforeBytes;
        std::string rollbackCodecError;
        if (!Kotonoha::SchoolDaysPersistenceCodec::SerializeFlgH(
                rollbackLocalBefore, &rollbackLocalBeforeBytes,
                &rollbackCodecError))
            Fail(rollbackCodecError);
        std::vector<uint8_t> rollbackSlogBefore;
        if (!rollbackSession.Persistence().SerializeSaveLog(
                &rollbackSlogBefore, &error))
            Fail(error.message);

        rollbackPlan.destination.nodeIndex = KOTONOHA_KTRF_NULL_INDEX;
        Check(!rollbackSession.CommitBack(&rollbackPlan, &error),
              "poisoned post-write Back commit unexpectedly succeeded");
        int rollbackRoute = 0;
        int rollbackScene = 0;
        Check(rollbackSession.State().ReadCoordinate(
                  &rollbackRoute, &rollbackScene, &error) &&
                  rollbackRoute == 1 && rollbackScene == 51,
              "failed post-write Back commit did not restore source coordinate");
        int64_t rollback999 = 0;
        Check(rollbackSession.State().ReadSessionInt("999", &rollback999, &error) &&
                  rollback999 == 77,
              "failed post-write Back commit did not restore 999 state");
        Kotonoha::SchoolDaysFlgH rollbackLocalAfter;
        if (!rollbackSession.Persistence().CaptureLocalFlgH(
                rollbackSession.State(), &rollbackLocalAfter, &error))
            Fail(error.message);
        std::vector<uint8_t> rollbackLocalAfterBytes;
        if (!Kotonoha::SchoolDaysPersistenceCodec::SerializeFlgH(
                rollbackLocalAfter, &rollbackLocalAfterBytes,
                &rollbackCodecError))
            Fail(rollbackCodecError);
        std::vector<uint8_t> rollbackSlogAfter;
        if (!rollbackSession.Persistence().SerializeSaveLog(
                &rollbackSlogAfter, &error))
            Fail(error.message);
        Check(rollbackLocalAfterBytes == rollbackLocalBeforeBytes &&
                  rollbackSlogAfter == rollbackSlogBefore,
              "failed post-write Back commit did not restore persistence image");
        rollbackSession.Close();
    }

    // Walk to the first deferred choice.  A new commit creates one tag4 keyed
    // by the active SceneKey; an idempotent recommit must not add/update a
    // second live record.
    bool choiceSeen = false;
    for (uint32_t step = 0u; step < 64u && !choiceSeen; ++step) {
        uint32_t choiceCount = 0u;
        if (!Kotonoha_KtrfRouterCurrentChoiceCount(
                &bridge.Session().Adapter()->router, &choiceCount, &error))
            Fail(error.message);
        if (choiceCount != 0u) {
            const std::string choiceScene = bridge.CurrentScene().sceneKey;
            const int64_t choiceValue = FirstChoiceValue(
                bridge.Session().Adapter(), &error);
            const size_t beforeTag4 = CountTag(
                bridge.Session().Persistence().SaveLogDocument(), 4);
            int accepted = 0;
            if (!bridge.CommitChoice(choiceValue, &accepted, &error))
                Fail(error.message);
            Check(accepted != 0, "first live choice was rejected");
            const auto* choiceRecord = FindTag4Name(
                bridge.Session().Persistence().SaveLogDocument(),
                std::u16string(choiceScene.begin(), choiceScene.end()));
            Check(choiceRecord != nullptr && choiceRecord->integer == choiceValue &&
                      CountTag(bridge.Session().Persistence().SaveLogDocument(), 4) ==
                          beforeTag4 + 1u,
                  "new live choice did not create exact tag4 record");
            if (!bridge.CommitChoice(choiceValue, &accepted, &error))
                Fail(error.message);
            Check(accepted != 0 &&
                      CountTag(bridge.Session().Persistence().SaveLogDocument(), 4) ==
                          beforeTag4 + 1u,
                  "idempotent choice recommit duplicated tag4 persistence");
            choiceSeen = true;
            break;
        }
        if (!bridge.Advance("ktrf:next", &liveStep, &error)) Fail(error.message);
        if (liveStep.kind ==
            Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::PendingHandoff) {
            if (!bridge.ContinueHandoff(&liveStep, &error)) Fail(error.message);
        }
        if (liveStep.kind ==
            Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::Terminal) {
            break;
        }
    }
    Check(choiceSeen, "smoke path did not reach a live choice");

    // Ending registration updates global EndXX/EndClear/EndNo and local
    // EndClear, while retaining opaque type-8 metadata and not inventing an
    // AllClear write.
    Kotonoha::SchoolDaysPersistence endingStore;
    if (!endingStore.LoadGlobalFlag(globalBytes, &bridge.Session().State(), &error))
        Fail(error.message);
    endingStore.ResetNewGame();
    const std::string endingSceneKey = bridge.CurrentScene().sceneKey;
    const auto* type8Before = FindFlag(endingStore.GlobalFlagDocument(),
                                       u"FILMEngine/SaveFile000");
    std::u16string type8Value;
    if (type8Before != nullptr && type8Before->type == 8) {
        type8Value = type8Before->stringValue;
    }
    if (!bridge.Session().State().WriteGlobalInt("AllClear", 0, &error))
        Fail(error.message);
    if (!endingStore.RegisterEnding(7, endingSceneKey,
                                    &bridge.Session().State(), &error))
        Fail(error.message);
    Check(GlobalInt(bridge, "[End07]=\"", &error) == -1 &&
              GlobalInt(bridge, "EndClear", &error) == -1 &&
              GlobalInt(bridge, "EndNo", &error) == 7 &&
              GlobalInt(bridge, "AllClear", &error) == 0 &&
              SessionInt(bridge, "EndClear", &error) == -1,
          "ending registration did not reproduce original flag writes");
    const auto* allClear = FindFlag(endingStore.GlobalFlagDocument(), u"AllClear");
    Check(allClear != nullptr && allClear->type == 11 && allClear->integer == -1,
          "ending registration changed authenticated AllClear value");
    if (type8Before != nullptr) {
        const auto* type8After = FindFlag(endingStore.GlobalFlagDocument(),
                                          u"FILMEngine/SaveFile000");
        Check(type8After != nullptr && type8After->type == 8 &&
                  type8After->stringValue == type8Value,
              "ending registration damaged opaque GlobalFlag type-8 metadata");
    }
    Check(CountTag(endingStore.SaveLogDocument(), 1) == 1u,
          "ending registration did not replace/create current tag1 snapshot");
    const auto& endingScript = endingStore.SaveLogDocument().records.front();
    const auto* endingLocal = FindFlag(endingScript.flags, u"EndClear");
    const std::u16string endingSceneWide(endingSceneKey.begin(), endingSceneKey.end());
    Check(endingScript.tag == 1 && endingScript.name == endingSceneWide &&
              endingScript.floatBits == UINT32_C(0x3f800000) &&
              endingLocal != nullptr && endingLocal->type == 11 &&
              endingLocal->integer == -1,
          "ending +0x3C tag1 snapshot mismatch");

    // Restore the authenticated GlobalFlag document before save-load checks.
    if (!bridge.Session().LoadGlobalFlag(globalBytes, &error)) Fail(error.message);

    // The original reader compares the tag-1 float route version before
    // applying the record. Reject a structurally valid SLog carrying another
    // version before it can mutate live state.
    Kotonoha::SchoolDaysSLog wrongVersion;
    std::string codecError;
    if (!Kotonoha::SchoolDaysPersistenceCodec::ParseSLog(
            saveBytes, &wrongVersion, &codecError)) {
        Fail(codecError);
    }
    bool changedVersion = false;
    for (auto& record : wrongVersion.records) {
        if (record.tag == 1) {
            record.floatBits = 0u;
            changedVersion = true;
            break;
        }
    }
    Check(changedVersion, "SaveFile0030 has no tag-1 record for version test");
    std::vector<uint8_t> wrongVersionBytes;
    if (!Kotonoha::SchoolDaysPersistenceCodec::SerializeSLog(
            wrongVersion, &wrongVersionBytes, &codecError)) {
        Fail(codecError);
    }
    Kotonoha::SchoolDaysKtrfSession::LoadPlan wrongVersionPlan;
    Check(!bridge.Session().PlanLoadSave(wrongVersionBytes, &wrongVersionPlan,
                                         &error),
          "mismatched save route version was accepted");

    // Poison both a runtime-backed and detached session key. The native reader
    // clears local FlagsHash once before its record loop, applies tag-1 FlgHs
    // sequentially, then sets BackSel=-1 at the high-level load caller.
    if (!bridge.Session().State().WriteSessionInt("997", 123, &error) ||
        !bridge.Session().State().WriteSessionInt("SP644", 123, &error)) {
        Fail(error.message);
    }
    auto* gameplayBefore = bridge.CurrentGameplay();
    int routeBefore = 0, sceneBefore = 0;
    if (!bridge.Session().State().ReadCoordinate(&routeBefore, &sceneBefore, &error))
        Fail(error.message);

    factoryState.failNextCreate = true;
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult loadResult;
    Check(!bridge.LoadSave(saveBytes, &loadResult, &error),
          "forced load Gameplay creation failure unexpectedly succeeded");
    int routeAfterFailure = 0, sceneAfterFailure = 0;
    if (!bridge.Session().State().ReadCoordinate(&routeAfterFailure,
                                                &sceneAfterFailure, &error))
        Fail(error.message);
    Check(bridge.CurrentGameplay() == gameplayBefore &&
              routeAfterFailure == routeBefore && sceneAfterFailure == sceneBefore &&
              SessionInt(bridge, "997", &error) == 123 &&
              SessionInt(bridge, "SP644", &error) == 123,
          "failed save load mutated live state/Gameplay");

    if (!bridge.LoadSave(saveBytes, &loadResult, &error))
        Fail(std::string("SaveFile0030 load failed: ") + error.message);
    Check(loadResult.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              bridge.CurrentScene().sceneKey == "00/00-00-A02",
          "SaveFile0030 did not restore its physical scene");
    int route = -1, scene = -1;
    if (!bridge.Session().State().ReadCoordinate(&route, &scene, &error))
        Fail(error.message);
    Check(route == 0 && scene == 2,
          "SaveFile0030 ROUTE/SCENE restore mismatch");
    Check(SessionInt(bridge, "SP100", &error) == -1,
          "SaveFile0030 detached SP100 was not restored");
    Check(SessionInt(bridge, "997", &error) == 0 &&
              SessionInt(bridge, "SP644", &error) == 0 &&
              SessionInt(bridge, "BackSel", &error) == -1,
          "save load did not reproduce native clear-once/tag1/BackSel semantics");
    Check(bridge.Session().History().HasPresented() &&
              bridge.Session().History().Presented().route == 0 &&
              bridge.Session().History().Presented().scene == 2 &&
              bridge.Session().History().Entries().empty(),
          "save load did not rebase transient History to loaded scene");
    Check(bridge.Session().Persistence().HasSaveLog(),
          "save load did not retain persistent SLog document");

    // R3 high-level Save(slot,metadata) boundary. FILMENGINE.INI in the
    // authenticated data uses Save/SaveFile00%d.DAT and
    // FILMEngine/SaveFile00%d, so slot 10 must be ...0010 rather than ...010.
    // Planning is side-effect free; metadata becomes live only after the
    // external writer reports success and commits the plan.
    const auto* slot10Before = FindFlag(
        bridge.Session().Persistence().GlobalFlagDocument(),
        u"FILMEngine/SaveFile0010");
    Check(slot10Before != nullptr && slot10Before->type == 8,
          "authenticated GlobalFlag lacks slot-10 SaveConfig metadata");
    const std::u16string slot10Original = slot10Before->stringValue;
    Kotonoha::SchoolDaysSaveService::Plan savePlan;
    if (!Kotonoha::SchoolDaysSaveService::PlanSlot(
            10, u"R3 PRIMARY", u"R3 SUB",
            bridge.Session().Persistence(), bridge.Session().State(),
            &savePlan, &error)) {
        Fail(error.message);
    }
    Check(savePlan.saveFilePath == "Save/SaveFile0010.DAT" &&
              savePlan.globalFlagPath == "Save/GlobalFlag.DAT" &&
              savePlan.saveConfigKey == "FILMEngine/SaveFile0010" &&
              savePlan.saveConfigSubKey == "FILMEngine/SaveFile0010_Sub" &&
              !savePlan.saveFileBytes.empty() &&
              !savePlan.globalFlagBytes.empty(),
          "high-level save plan did not honor authenticated filename/config templates");
    const auto* slot10StillLive = FindFlag(
        bridge.Session().Persistence().GlobalFlagDocument(),
        u"FILMEngine/SaveFile0010");
    Check(slot10StillLive != nullptr &&
              slot10StillLive->stringValue == slot10Original,
          "save planning mutated live GlobalFlag before commit");
    Kotonoha::SchoolDaysFlgH plannedGlobal;
    if (!Kotonoha::SchoolDaysPersistenceCodec::ParseFlgH(
            savePlan.globalFlagBytes, &plannedGlobal, &codecError)) {
        Fail(codecError);
    }
    const auto* plannedPrimary = FindFlag(plannedGlobal,
                                          u"FILMEngine/SaveFile0010");
    const auto* plannedSub = FindFlag(plannedGlobal,
                                      u"FILMEngine/SaveFile0010_Sub");
    Check(plannedPrimary != nullptr && plannedPrimary->type == 8 &&
              plannedPrimary->stringValue == u"R3 PRIMARY" &&
              plannedSub != nullptr && plannedSub->type == 8 &&
              plannedSub->stringValue == u"R3 SUB",
          "save plan did not stage exact type-8 SaveConfig metadata");
    if (!Kotonoha::SchoolDaysSaveService::Commit(
            savePlan, &bridge.Session().Persistence(), &error)) {
        Fail(error.message);
    }
    const auto* committedPrimary = FindFlag(
        bridge.Session().Persistence().GlobalFlagDocument(),
        u"FILMEngine/SaveFile0010");
    Check(committedPrimary != nullptr && committedPrimary->type == 8 &&
              committedPrimary->stringValue == u"R3 PRIMARY",
          "save plan commit did not publish staged SaveConfig metadata");

    // C4 title/end-background decision. Exercise it on a snapshot so these
    // assertions never contaminate the live runtime used by the rest of Gate7.
    Kotonoha::KtrfStateStore::Snapshot endBgState;
    if (!bridge.Session().State().Capture(&endBgState, &error)) Fail(error.message);
    Kotonoha::SchoolDaysEndBackgroundResolver::Profile endBgProfile;
    endBgProfile.endBgView = false;
    endBgProfile.endingMax = 22;
    endBgProfile.baseAsset = "System/Title/TitleBase.png";
    endBgProfile.allClearAsset = "System/EndTitle/END-ALL-complete.png";
    for (int i = 1; i <= 22; ++i) {
        endBgProfile.endingAssets.push_back("ending-" + std::to_string(i));
    }
    Kotonoha::SchoolDaysEndBackgroundResolver::Result endBg;
    if (!Kotonoha::SchoolDaysEndBackgroundResolver::Resolve(
            endBgProfile, &bridge.Session().State(), &endBg, &error)) {
        Fail(error.message);
    }
    Check(endBg.kind ==
              Kotonoha::SchoolDaysEndBackgroundResolver::Kind::Base &&
              endBg.asset == "System/Title/TitleBase.png",
          "EndBGView=0 did not bypass ending-background selection");

    // Native startup does not load EndingList at all when EndBGView is zero.
    // Prove the resolver likewise ignores an otherwise invalid ending table.
    const auto configuredEndingAssets = endBgProfile.endingAssets;
    endBgProfile.endingAssets.clear();
    endBgProfile.endingMax = 999;
    if (!Kotonoha::SchoolDaysEndBackgroundResolver::Resolve(
            endBgProfile, &bridge.Session().State(), &endBg, &error)) {
        Fail("EndBGView=0 incorrectly depended on EndingList: " +
             std::string(error.message));
    }
    Check(endBg.kind ==
              Kotonoha::SchoolDaysEndBackgroundResolver::Kind::Base,
          "EndBGView=0 invalid EndingList did not retain BaseFile");
    endBgProfile.endingAssets = configuredEndingAssets;
    endBgProfile.endingMax = 22;

    endBgProfile.endBgView = true;
    if (!bridge.Session().State().WriteGlobalInt("AllClear", 0, &error) ||
        !bridge.Session().State().WriteGlobalInt("EndClear", 1, &error) ||
        !bridge.Session().State().WriteGlobalInt("EndNo", 0, &error)) {
        Fail(error.message);
    }
    for (int i = 0; i < 22; ++i) {
        char key[32];
        std::snprintf(key, sizeof(key), "[End%02d]=\"", i);
        if (!bridge.Session().State().WriteGlobalInt(key, 0, &error))
            Fail(error.message);
    }
    if (!Kotonoha::SchoolDaysEndBackgroundResolver::Resolve(
            endBgProfile, &bridge.Session().State(), &endBg, &error)) {
        Fail(error.message);
    }
    Check(endBg.kind ==
              Kotonoha::SchoolDaysEndBackgroundResolver::Kind::Ending &&
              endBg.endingIndex == 0 && endBg.asset == "ending-1",
          "EndNo=0 was not treated as zero-based Ending01 index");

    for (int i = 0; i < 22; ++i) {
        char key[32];
        std::snprintf(key, sizeof(key), "[End%02d]=\"", i);
        if (!bridge.Session().State().WriteGlobalInt(key, 1, &error))
            Fail(error.message);
    }
    if (!Kotonoha::SchoolDaysEndBackgroundResolver::Resolve(
            endBgProfile, &bridge.Session().State(), &endBg, &error)) {
        Fail(error.message);
    }
    Check(endBg.kind ==
              Kotonoha::SchoolDaysEndBackgroundResolver::Kind::AllClear &&
              endBg.promotedAllClear &&
              endBg.asset == "System/EndTitle/END-ALL-complete.png" &&
              GlobalInt(bridge, "AllClear", &error) != 0,
          "22/22 ending completion did not promote/select AllClear");
    if (!bridge.Session().State().Restore(endBgState, &error)) Fail(error.message);

    // The raw file itself round-trips byte-identically in Gate 6.  Native load
    // then sets BackSel=-1 and the common loaded/ready tail replaces tag1 with
    // the active ScriptObject snapshot.  Therefore the live in-memory SLog is
    // expected to differ from source bytes while tag3/tag4 remain intact.
    Kotonoha::SchoolDaysSLog sourceSave;
    if (!Kotonoha::SchoolDaysPersistenceCodec::ParseSLog(
            saveBytes, &sourceSave, &codecError)) {
        Fail(codecError);
    }
    const auto& liveSave = bridge.Session().Persistence().SaveLogDocument();
    Check(CountTag(liveSave, 1) == 1u &&
              CountTag(liveSave, 3) == CountTag(sourceSave, 3) &&
              CountTag(liveSave, 4) == CountTag(sourceSave, 4) &&
              !liveSave.records.empty() && liveSave.records.front().tag == 1 &&
              liveSave.records.front().name == u"00/00-00-A02",
          "live loaded SLog did not preserve collections/current tag1 contract");
    const auto* liveBackSel = FindFlag(liveSave.records.front().flags, u"BackSel");
    Check(liveBackSel != nullptr && liveBackSel->type == 3 &&
              liveBackSel->integer == -1,
          "post-load active tag1 did not capture BackSel=-1");
    const auto* sourceCheckpoint = FindTag3Secondary(sourceSave, u"SP100");
    const auto* liveCheckpoint = FindTag3Secondary(liveSave, u"SP100");
    Check(sourceCheckpoint != nullptr && liveCheckpoint != nullptr &&
              sourceCheckpoint->name == liveCheckpoint->name &&
              sourceCheckpoint->secondary == liveCheckpoint->secondary &&
              sourceCheckpoint->integer == liveCheckpoint->integer &&
              sourceCheckpoint->flags.entries.size() ==
                  liveCheckpoint->flags.entries.size(),
          "post-load active tag1 refresh disturbed tag3 collection");

    // R4 RouteMap result8 is not a direct ROUTE/SCENE/script selector. Native
    // 0x428400 formats SP%03d and restores that tag3 checkpoint. Prove the
    // production bridge keeps the current Gameplay/state/persistence untouched
    // when target creation fails, then restores SP100 transactionally.
    auto* routeMapGameplayBefore = bridge.CurrentGameplay();
    int routeMapRouteBefore = 0;
    int routeMapSceneBefore = 0;
    if (!bridge.Session().State().ReadCoordinate(
            &routeMapRouteBefore, &routeMapSceneBefore, &error))
        Fail(error.message);
    const auto routeMapHistoryBefore = bridge.Session().History();
    std::vector<uint8_t> routeMapSlogBefore;
    if (!bridge.Session().Persistence().SerializeSaveLog(
            &routeMapSlogBefore, &error))
        Fail(error.message);

    factoryState.failNextCreate = true;
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult routeMapResult;
    Check(!bridge.LoadRouteMapSelector(100, &routeMapResult, &error),
          "forced RouteMap checkpoint Gameplay creation failure unexpectedly succeeded");
    int routeMapRouteAfterFailure = 0;
    int routeMapSceneAfterFailure = 0;
    if (!bridge.Session().State().ReadCoordinate(
            &routeMapRouteAfterFailure, &routeMapSceneAfterFailure, &error))
        Fail(error.message);
    std::vector<uint8_t> routeMapSlogAfterFailure;
    if (!bridge.Session().Persistence().SerializeSaveLog(
            &routeMapSlogAfterFailure, &error))
        Fail(error.message);
    const auto& routeMapHistoryAfterFailure = bridge.Session().History();
    bool routeMapHistoryEntriesMatch =
        routeMapHistoryAfterFailure.Entries().size() ==
        routeMapHistoryBefore.Entries().size();
    if (routeMapHistoryEntriesMatch) {
        for (size_t i = 0u; i < routeMapHistoryBefore.Entries().size(); ++i) {
            const auto& beforeEntry = routeMapHistoryBefore.Entries()[i];
            const auto& afterEntry = routeMapHistoryAfterFailure.Entries()[i];
            if (!(beforeEntry.source == afterEntry.source) ||
                !(beforeEntry.destination == afterEntry.destination) ||
                beforeEntry.inverseFeeling != afterEntry.inverseFeeling) {
                routeMapHistoryEntriesMatch = false;
                break;
            }
        }
    }
    Check(bridge.CurrentGameplay() == routeMapGameplayBefore &&
              routeMapRouteAfterFailure == routeMapRouteBefore &&
              routeMapSceneAfterFailure == routeMapSceneBefore &&
              routeMapSlogAfterFailure == routeMapSlogBefore &&
              routeMapHistoryAfterFailure.HasPresented() ==
                  routeMapHistoryBefore.HasPresented() &&
              routeMapHistoryAfterFailure.Presented() ==
                  routeMapHistoryBefore.Presented() &&
              routeMapHistoryEntriesMatch,
          "failed RouteMap checkpoint load mutated live Gameplay/state/history/persistence");

    Check(!bridge.LoadRouteMapSelector(99, &routeMapResult, &error),
          "non-RouteMap selector below 100 unexpectedly resolved as checkpoint load");
    Check(bridge.CurrentGameplay() == routeMapGameplayBefore,
          "invalid RouteMap selector changed current Gameplay");

    if (!bridge.LoadRouteMapSelector(100, &routeMapResult, &error))
        Fail(std::string("RouteMap SP100 restore failed: ") + error.message);
    Check(routeMapResult.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              routeMapResult.scene.sceneKey == "00/00-00-A00",
          "RouteMap SP100 did not resolve the checkpoint SceneKey");
    int routeMapRestoredRoute = -1;
    int routeMapRestoredScene = -1;
    if (!bridge.Session().State().ReadCoordinate(
            &routeMapRestoredRoute, &routeMapRestoredScene, &error))
        Fail(error.message);
    Check(routeMapRestoredRoute == 0 && routeMapRestoredScene == 0,
          "RouteMap SP100 checkpoint coordinate mismatch");
    const auto& routeMapLiveLog =
        bridge.Session().Persistence().SaveLogDocument();
    Check(FindTag3Secondary(routeMapLiveLog, u"SP100") == nullptr,
          "RouteMap SP100 did not prune selected-and-later tag3 checkpoints");
    Check(CountTag(routeMapLiveLog, 1) == 1u &&
              !routeMapLiveLog.records.empty() &&
              routeMapLiveLog.records.front().tag == 1 &&
              routeMapLiveLog.records.front().name == u"00/00-00-A00",
          "RouteMap SP100 did not refresh current tag1 to restored SceneKey");
    Check(bridge.Session().History().HasPresented() &&
              bridge.Session().History().Presented().route == 0 &&
              bridge.Session().History().Presented().scene == 0 &&
              bridge.Session().History().Entries().empty(),
          "RouteMap SP100 did not rebase History to restored scene");

    Check(factoryState.maxLive <= 2u && factoryState.live == 1u,
          "save load violated previous+next Gameplay ownership bound");
    bridge.Close();
    Check(factoryState.live == 0u && factoryState.creates == factoryState.destroys,
          "save-load Gameplay ownership leaked");

    std::cout << "SCHOOL DAYS PERSISTENCE RUNTIME GATE 7 PASS\n"
              << "globalflag_runtime_roundtrip=PASS\n"
              << "ktrf_only_global_defaults_preserved=PASS\n"
              << "opaque_unmapped_global_preserved=PASS\n"
              << "ending_registration_projection=PASS\n"
              << "callback00_pre_sp_snapshot=PASS\n"
              << "callback00_factory_rollback=PASS\n"
              << "live_back_checkpoint_prune=PASS\n"
              << "physical_scene_tag1_refresh=PASS\n"
              << "persistence_effect_type_oracle=PASS\n"
              << "global_rep_type11_projection=PASS\n"
              << "loaded_global_opaque_metadata_preserved=PASS\n"
              << "manual_back_persistence_types=PASS\n"
              << "manual_back_direct_int_type3=PASS\n"
              << "manual_back_sparse_type_materialization=PASS\n"
              << "post_write_back_rollback=PASS\n"
              << "live_choice_tag4_upsert=PASS\n"
              << "ending_tag1_replace=PASS\n"
              << "route_version_rejection=PASS\n"
              << "load_factory_failure_atomic=PASS\n"
              << "savefile0030_scene_restore=PASS\n"
              << "session_flagshash_clear_once=PASS\n"
              << "live_load_tag1_refresh=PASS\n"
              << "routemap_sp_checkpoint_restore=PASS\n"
              << "routemap_factory_failure_atomic=PASS\n"
              << "routemap_tag3_prune_tag1_refresh=PASS\n"
              << "high_level_save_plan=PASS\n"
              << "end_background_resolver=PASS\n"
              << "max_live_gameplays=" << factoryState.maxLive << '\n';
    return 0;
}
