#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <Kotonoha/routing/SchoolDaysPersistenceCodec.hpp>
#include <Kotonoha/routing/SchoolDaysStateStore.hpp>

namespace Kotonoha {

struct SchoolDaysCoordinate;

class SchoolDaysPersistence {
public:
    struct SaveImage {
        SchoolDaysSLog document;
        size_t scriptRecordIndex = 0u;
        std::string sceneKey;
        int route = 0;
        int scene = 0;
        std::vector<std::pair<std::string, int64_t>> sessionInts;
    };

    bool LoadGlobalFlag(const std::vector<uint8_t>& bytes,
                        SchoolDaysStateStore* state,
                        Kotonoha_KtrfError* error);
    bool SerializeGlobalFlag(const SchoolDaysStateStore& state,
                             std::vector<uint8_t>* bytes,
                             Kotonoha_KtrfError* error) const;

    bool ParseSaveImage(const std::vector<uint8_t>& bytes, SaveImage* image,
                        Kotonoha_KtrfError* error) const;
    void CommitSaveImage(const SaveImage& image);
    bool SerializeSaveLog(std::vector<uint8_t>* bytes,
                          Kotonoha_KtrfError* error) const;

    // Native-live persistence state.  Numeric values remain owned by
    // SchoolDaysStateStore; this class records only sparse FlgH membership/type
    // plus SLog collections.  New game seeds the seven fields observed in the
    // authenticated first checkpoint (000..004, ROUTE, SCENE).
    void ResetNewGame();
    // Native Script.Free -> 0x432870 with RouteLoad==0 destroys the current
    // script-manager SLog collections (tag1/tag3/tag4) while leaving the live
    // local FlagsHash and GlobalFlag owner intact.  This is narrower than a
    // new-game reset: sparse local metadata must survive until the next script
    // is explicitly started/loaded.
    void ResetScriptManagerForFrontendReturn();
    bool ValidateEffectOracle(const Kotonoha_KtrfDocument* document,
                              Kotonoha_KtrfError* error);
    bool ObserveRoutingEffect(uint32_t effectIndex,
                              Kotonoha_KtrfError* error);
    bool RoutingEffectTargetsTopMeter(uint32_t effectIndex) const;
    bool RecordCheckpoint(int route, int scene, const std::string& sceneKey,
                          SchoolDaysStateStore* state,
                          Kotonoha_KtrfError* error);
    bool RecordChoice(const std::string& sceneKey, int64_t value,
                      Kotonoha_KtrfError* error);
    bool ReadRecordedChoice(const std::string& sceneKey, int32_t* value,
                            bool* present,
                            Kotonoha_KtrfError* error) const;
    bool RestoreBackCheckpoint(const SchoolDaysCoordinate& destination,
                               SchoolDaysStateStore* state, bool* restored,
                               Kotonoha_KtrfError* error);
    bool MarkLocalInteger(const std::string& key, int32_t type,
                          Kotonoha_KtrfError* error);
    bool CaptureLocalFlgH(const SchoolDaysStateStore& state,
                          SchoolDaysFlgH* out,
                          Kotonoha_KtrfError* error) const;
    bool ReplaceCurrentScriptSnapshot(const std::string& sceneKey,
                                      uint32_t versionBits,
                                      const SchoolDaysStateStore& state,
                                      Kotonoha_KtrfError* error);

    bool RegisterEnding(int endingCode, const std::string& sceneKey,
                        SchoolDaysStateStore* state,
                        Kotonoha_KtrfError* error);

    // R3 high-level save metadata seam. The original Save(slot,...) updates
    // type-8 entries in the global FlagsHash only after the SLog writer has
    // succeeded.  Keep the primitive here; slot naming / file I/O remains in
    // SchoolDaysSaveService.
    bool UpsertGlobalString(const std::string& key,
                            const std::u16string& value,
                            Kotonoha_KtrfError* error);
    // Authenticated host/RouteProc writers can also mutate the GlobalFlag
    // FlagsHash outside KTRF effects (for example terminal FLAG_LOGO=false).
    // Keep those writes explicit so sparse type metadata is not inferred from
    // arbitrary StateStore writes.
    bool WriteGlobalInteger(const std::string& key, int32_t type, int32_t value,
                            SchoolDaysStateStore* state,
                            Kotonoha_KtrfError* error);

    bool HasGlobalFlag() const { return hasGlobalFlag_; }
    bool HasSaveLog() const { return hasSaveLog_; }
    const SchoolDaysFlgH& GlobalFlagDocument() const { return globalFlag_; }
    const SchoolDaysSLog& SaveLogDocument() const { return saveLog_; }

private:
    struct RuntimeEffectType {
        std::string target;
        uint8_t type = 0u;
        bool global = false;
    };

    static bool Utf16Ascii(const std::u16string& value, std::string* out,
                           Kotonoha_KtrfError* error);
    static std::u16string AsciiUtf16(const std::string& value);
    static bool NumericEntries(
        const SchoolDaysFlgH& node,
        std::vector<std::pair<std::string, int64_t>>* values,
        Kotonoha_KtrfError* error);
    static bool FindInteger(const SchoolDaysFlgH& node, const char* key,
                            int64_t* value, Kotonoha_KtrfError* error);
    static SchoolDaysFlgHEntry* FindEntry(SchoolDaysFlgH* node,
                                          const std::u16string& key);
    static const SchoolDaysFlgHEntry* FindEntry(const SchoolDaysFlgH& node,
                                                const std::u16string& key);
    static bool UpsertInteger(SchoolDaysFlgH* node, const std::string& key,
                              int32_t type, int32_t value,
                              Kotonoha_KtrfError* error);
    bool UpsertGlobalInteger(const std::string& key, int32_t type,
                             int32_t value, SchoolDaysStateStore* state,
                             Kotonoha_KtrfError* error);
    void RebuildLocalMetadataFromSave();

    SchoolDaysFlgH globalFlag_;
    SchoolDaysSLog saveLog_;
    std::map<std::string, SchoolDaysFlgHEntry> localMetadata_;
    std::vector<RuntimeEffectType> runtimeEffectTypes_;
    bool hasGlobalFlag_ = false;
    bool hasSaveLog_ = false;
};

} // namespace Kotonoha
