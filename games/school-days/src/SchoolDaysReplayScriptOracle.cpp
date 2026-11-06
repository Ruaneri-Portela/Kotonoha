#include <Kotonoha/routing/SchoolDaysReplayScriptOracle.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>


namespace Kotonoha {
namespace {

using Oracle = SchoolDaysReplayScriptOracle;
using Choice = Oracle::ChoiceDescriptor;
using Descriptor = Oracle::HSceneDescriptor;
using Kind = Oracle::Kind;
using Resolution = Oracle::TransitionCursorResolution;
using Sequence = Oracle::SequenceDescriptor;

template <std::size_t N>
constexpr Sequence Seq(const char* const (&sceneKeys)[N]) {
    return {sceneKeys, N};
}

constexpr const char* k00[] = {"02/02-2S-W03"};
constexpr const char* k01[] = {
    "02/02-2K-J03", "02/02-2K-J04", "02/02-2K-J05", "02/02-2K-J06",
    "02/02-2K-J07", "02/02-2K-J08", "02/02-2K-J09", "02/02-2K-J10"};
constexpr const char* k02[] = {"02/02-2K-L01"};
constexpr const char* k03[] = {"02/02-2K-O01", "02/02-2K-O04"};
constexpr const char* k04[] = {"02/02-2K-Q03", "02/02-2K-Q04", "02/02-2K-Q05"};
constexpr const char* k05[] = {"03/03-SA-B07"};
constexpr const char* k06[] = {
    "03/03-A0-A00", "03/03-A0-A04", "03/03-A0-A05", "03/03-A0-A06",
    "03/03-A0-A07", "03/03-A0-A08", "03/03-A0-A09", "03/03-A0-A10"};
constexpr const char* k07[] = {"03/03-A1-E08", "03/03-A1-Z00", "03/03-A1-Z01"};
constexpr const char* k08[] = {"03/03-AK-E00"};
constexpr const char* k09[] = {"03/03-SB-A00", "03/03-SB-A01", "03/03-SB-A03"};
constexpr const char* k10[] = {
    "03/03-B2-C00", "03/03-B2-N02", "03/03-B2-N03", "03/03-B2-N04",
    "03/03-B2-N06", "03/03-B2-N07", "03/03-B2-O03"};

constexpr const char* k11Choice0[] = {
    "03/03-KB-N00", "03/03-KB-N02", "03/03-KB-N03", "03/03-KB-N04",
    "03/03-KB-N05", "03/03-KB-N06", "03/03-KB-N07", "03/03-KB-N08",
    "03/03-KB-N10", "03/03-KB-O02", "03/03-KB-O03", "03/03-KB-O04",
    "03/03-KB-O05", "03/03-KB-O07", "03/03-KB-O08"};
constexpr const char* k11Choice1[] = {
    "03/03-KB-N00", "03/03-KB-N02", "03/03-KB-N03", "03/03-KB-N04",
    "03/03-KB-N05", "03/03-KB-N06", "03/03-KB-N07", "03/03-KB-N09",
    "03/03-KB-O01", "03/03-KB-O03", "03/03-KB-O05", "03/03-KB-O07",
    "03/03-KB-O08", "03/03-KB-O09"};
constexpr const char* k11Choice2[] = {
    "03/03-KB-N00", "03/03-KB-N02", "03/03-KB-N03", "03/03-KB-N04",
    "03/03-KB-N05", "03/03-KB-N06", "03/03-KB-N07", "03/03-KB-N08",
    "03/03-KB-N10", "03/03-KB-O02", "03/03-KB-O03", "03/03-KB-O04",
    "03/03-KB-O05", "03/03-KB-O07", "03/03-KB-O08", "03/03-KB-O09"};
constexpr const char* k11Choice3[] = {
    "03/03-KB-N00", "03/03-KB-N02", "03/03-KB-N03", "03/03-KB-N04",
    "03/03-KB-N05", "03/03-KB-N06", "03/03-KB-N07", "03/03-KB-N09",
    "03/03-KB-O01", "03/03-KB-O03", "03/03-KB-O05", "03/03-KB-O07",
    "03/03-KB-O08"};
constexpr Choice k11Choices[] = {
    {0, Seq(k11Choice0)}, {1, Seq(k11Choice1)},
    {2, Seq(k11Choice2)}, {3, Seq(k11Choice3)},
};

constexpr int k11Next0[][3] = {
    {3,1,2},{6,4,5},{10,0,0},{10,0,0},{7,0,0},{8,0,0},{9,0,0},
    {10,0,0},{11,0,0},{11,0,0},{13,12,0},{10,0,0},{14,0,0},{15,0,0},
    {13,0,0}};
constexpr int k11Next1[][3] = {
    {3,1,2},{6,4,5},{9,0,0},{9,0,0},{7,0,0},{8,0,0},{9,0,0},
    {9,0,0},{9,0,0},{11,10,0},{12,0,0},{14,0,0},{13,0,0},{14,0,0}};
constexpr int k11Next2[][3] = {
    {3,1,2},{6,4,5},{10,0,0},{10,0,0},{7,0,0},{8,0,0},{9,0,0},
    {10,0,0},{11,0,0},{11,0,0},{13,12,0},{10,0,0},{14,0,0},{16,0,0},
    {15,0,0},{16,0,0}};
constexpr int k11Next3[][3] = {
    {3,1,2},{6,4,5},{9,0,0},{9,0,0},{7,0,0},{8,0,0},{9,0,0},
    {9,0,0},{9,0,0},{11,10,0},{12,0,0},{13,0,0},{11,0,0}};

constexpr const char* k12[] = {
    "04/04-KA-D01", "04/04-KA-D03", "04/04-KA-D04", "04/04-KA-D05",
    "04/04-KA-D06", "04/04-KA-D07", "04/04-KA-D08"};
constexpr const char* k13[] = {"04/04-KA-B03"};
constexpr const char* k14[] = {"04/04-KA-L00", "04/04-KA-L01"};
constexpr const char* k15[] = {"04/04-KA-B04"};
constexpr const char* k16[] = {
    "04/04-SB-G05", "04/04-SB-G06", "04/04-SB-G07", "04/04-SB-G08",
    "04/04-SB-I00", "04/04-SB-I01", "04/04-SB-I02", "04/04-SB-I03",
    "04/04-SB-I04"};
constexpr const char* k17[] = {"04/04-KB-C03"};
constexpr const char* k18[] = {"04/04-KB-E01"};
constexpr const char* k19[] = {"04/04-KC-C03"};
constexpr const char* k20[] = {"04/04-KC-E01"};
constexpr const char* k21[] = {"04/04-SC-B00"};

constexpr const char* k22Choice0[] = {
    "04/04-C1-A00", "04/04-C1-A01", "04/04-C1-A03"};
constexpr const char* k22Choice1[] = {
    "04/04-C1-A00", "04/04-C1-A02", "04/04-C1-A03"};
constexpr Choice k22Choices[] = {
    {0, Seq(k22Choice0)}, {1, Seq(k22Choice1)},
};

constexpr const char* k23[] = {"04/04-SC-D02"};
constexpr const char* k24[] = {"04/04-SC-F00"};
constexpr const char* k25[] = {
    "04/04-C0-I00", "04/04-C0-I01", "04/04-C0-I02", "04/04-C0-I03",
    "04/04-C0-I04"};
constexpr const char* k26[] = {"04/04-KE-C01"};
constexpr const char* k27[] = {"04/04-KE-B07"};
constexpr const char* k28[] = {"05/05-KA-A05", "05/05-KA-A06", "05/05-KA-A07"};
constexpr const char* k29[] = {"05/05-5H-C03", "05/05-5H-C06", "05/05-5H-C07"};

constexpr const char* k30Choice0[] = {
    "05/05-5H-D00", "05/05-5H-D01", "05/05-5H-D03"};
constexpr const char* k30Choice1[] = {
    "05/05-5H-D00", "05/05-5H-D02", "05/05-5H-D03"};
constexpr Choice k30Choices[] = {
    {0, Seq(k30Choice0)}, {1, Seq(k30Choice1)},
};

constexpr const char* k31[] = {"05/05-SD-D05"};
constexpr const char* k32[] = {
    "05/05-SD-A02", "05/05-SD-B03", "05/05-SD-B05", "05/05-SD-A04"};
constexpr const char* k33[] = {"05/05-SA-I00"};
constexpr const char* k34[] = {"05/05-SB-I01", "05/05-SB-I02"};
constexpr const char* k35[] = {"05/05-KC-A00", "05/05-KC-A01"};
constexpr const char* k36[] = {
    "05/05-KC-D00", "05/05-KC-D02", "05/05-KC-D03", "05/05-KC-D04"};
constexpr const char* k37[] = {"05/05-KC-E00"};
constexpr const char* k38[] = {"05/05-KC-A02"};
constexpr const char* k39[] = {"05/05-5O-D10"};
constexpr const char* k40[] = {"05/05-SE-N00"};

constexpr Descriptor kHScenes[] = {
    {0, Kind::Ordinary, -1, Seq(k00), nullptr, 0, Resolution::StaticTableOrder},
    {1, Kind::Ordinary, -1, Seq(k01), nullptr, 0, Resolution::StaticTableOrder},
    {2, Kind::Ordinary, -1, Seq(k02), nullptr, 0, Resolution::StaticTableOrder},
    {3, Kind::Ordinary, -1, Seq(k03), nullptr, 0, Resolution::StaticTableOrder},
    {4, Kind::Ordinary, -1, Seq(k04), nullptr, 0, Resolution::StaticTableOrder},
    {5, Kind::Ordinary, -1, Seq(k05), nullptr, 0, Resolution::StaticTableOrder},
    {6, Kind::Ordinary, -1, Seq(k06), nullptr, 0, Resolution::StaticTableOrder},
    {7, Kind::Ordinary, -1, Seq(k07), nullptr, 0, Resolution::StaticTableOrder},
    {8, Kind::Ordinary, -1, Seq(k08), nullptr, 0, Resolution::StaticTableOrder},
    {9, Kind::Ordinary, -1, Seq(k09), nullptr, 0, Resolution::StaticTableOrder},
    {10, Kind::Ordinary, -1, Seq(k10), nullptr, 0, Resolution::StaticTableOrder},
    {11, Kind::SpecialChoiceTable, 0, {}, k11Choices, 4,
     Resolution::NativeSelectorRequired},
    {12, Kind::Ordinary, -1, Seq(k12), nullptr, 0, Resolution::StaticTableOrder},
    {13, Kind::Ordinary, -1, Seq(k13), nullptr, 0, Resolution::StaticTableOrder},
    {14, Kind::Ordinary, -1, Seq(k14), nullptr, 0, Resolution::StaticTableOrder},
    {15, Kind::Ordinary, -1, Seq(k15), nullptr, 0, Resolution::StaticTableOrder},
    {16, Kind::Ordinary, -1, Seq(k16), nullptr, 0, Resolution::StaticTableOrder},
    {17, Kind::Ordinary, -1, Seq(k17), nullptr, 0, Resolution::StaticTableOrder},
    {18, Kind::Ordinary, -1, Seq(k18), nullptr, 0, Resolution::StaticTableOrder},
    {19, Kind::Ordinary, -1, Seq(k19), nullptr, 0, Resolution::StaticTableOrder},
    {20, Kind::Ordinary, -1, Seq(k20), nullptr, 0, Resolution::StaticTableOrder},
    {21, Kind::Ordinary, -1, Seq(k21), nullptr, 0, Resolution::StaticTableOrder},
    {22, Kind::SpecialChoiceTable, 2, {}, k22Choices, 2,
     Resolution::NativeSelectorRequired},
    {23, Kind::Ordinary, -1, Seq(k23), nullptr, 0, Resolution::StaticTableOrder},
    {24, Kind::Ordinary, -1, Seq(k24), nullptr, 0, Resolution::StaticTableOrder},
    {25, Kind::Ordinary, -1, Seq(k25), nullptr, 0, Resolution::StaticTableOrder},
    {26, Kind::Ordinary, -1, Seq(k26), nullptr, 0, Resolution::StaticTableOrder},
    {27, Kind::Ordinary, -1, Seq(k27), nullptr, 0, Resolution::StaticTableOrder},
    {28, Kind::Ordinary, -1, Seq(k28), nullptr, 0, Resolution::StaticTableOrder},
    {29, Kind::Ordinary, -1, Seq(k29), nullptr, 0, Resolution::StaticTableOrder},
    {30, Kind::SpecialChoiceTable, 3, {}, k30Choices, 2,
     Resolution::NativeSelectorRequired},
    {31, Kind::Ordinary, -1, Seq(k31), nullptr, 0, Resolution::StaticTableOrder},
    {32, Kind::Ordinary, -1, Seq(k32), nullptr, 0, Resolution::StaticTableOrder},
    {33, Kind::Ordinary, -1, Seq(k33), nullptr, 0, Resolution::StaticTableOrder},
    {34, Kind::Ordinary, -1, Seq(k34), nullptr, 0, Resolution::StaticTableOrder},
    {35, Kind::Ordinary, -1, Seq(k35), nullptr, 0, Resolution::StaticTableOrder},
    {36, Kind::Ordinary, -1, Seq(k36), nullptr, 0, Resolution::StaticTableOrder},
    {37, Kind::Ordinary, -1, Seq(k37), nullptr, 0, Resolution::StaticTableOrder},
    {38, Kind::Ordinary, -1, Seq(k38), nullptr, 0, Resolution::StaticTableOrder},
    {39, Kind::Ordinary, -1, Seq(k39), nullptr, 0, Resolution::StaticTableOrder},
    {40, Kind::Ordinary, -1, Seq(k40), nullptr, 0, Resolution::StaticTableOrder},
};

static_assert(sizeof(kHScenes) / sizeof(kHScenes[0]) == Oracle::HSceneCount(),
              "authenticated HScene script table size drifted");

void ClearError(std::string* error) {
    if (error != nullptr) error->clear();
}

bool Fail(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
}

bool ValidateSequence(const Sequence& sequence, const char* orsRoot,
                      std::string* error) {
    if (orsRoot == nullptr) return true;
    for (std::size_t i = 0; i < sequence.count; ++i) {
        const std::string relative = Oracle::OrsRelativePath(sequence.sceneKeys[i]);
        SchoolDaysImageResolution resolved;
        if (gSchoolDaysVirtualAssetLookup != nullptr) {
            resolved = ResolveSchoolDaysAssetPath(
                "", "Script/English/" + relative);
        }
        if (gSchoolDaysVirtualAssetLookup == nullptr ||
            resolved.status == SchoolDaysAssetResolutionStatus::Missing) {
            resolved = ResolveSchoolDaysAssetPath(orsRoot, relative);
        }
        if (resolved.status != SchoolDaysAssetResolutionStatus::Present) {
            return Fail(error,
                        resolved.status == SchoolDaysAssetResolutionStatus::Collision
                            ? "School Days Replay SceneKey ORS has case-insensitive collision: " + relative
                            : "School Days Replay SceneKey is missing/invalid ORS: " + relative);
        }
    }
    return true;
}

bool ValidateDescriptor(const Descriptor& descriptor, const char* orsRoot,
                        std::string* error) {
    if (orsRoot == nullptr) return true;
    if (descriptor.kind == Kind::Ordinary)
        return ValidateSequence(descriptor.ordinary, orsRoot, error);
    for (std::size_t i = 0; i < descriptor.choiceCount; ++i) {
        if (!ValidateSequence(descriptor.choices[i].sequence, orsRoot, error))
            return false;
    }
    return true;
}

const Descriptor* FindDescriptor(int index) {
    if (index < 0 || index >= Oracle::HSceneCount()) return nullptr;
    return &kHScenes[static_cast<std::size_t>(index)];
}

} // namespace

const SchoolDaysReplayScriptOracle::HSceneDescriptor*
SchoolDaysReplayScriptOracle::HScene(int index, const char* orsRoot,
                                     std::string* error) {
    ClearError(error);
    const Descriptor* descriptor = FindDescriptor(index);
    if (descriptor == nullptr) {
        Fail(error, "School Days Replay HScene index is out of range");
        return nullptr;
    }
    if (!ValidateDescriptor(*descriptor, orsRoot, error)) return nullptr;
    return descriptor;
}

const SchoolDaysReplayScriptOracle::SequenceDescriptor*
SchoolDaysReplayScriptOracle::OrdinarySequence(int index, const char* orsRoot,
                                               std::string* error) {
    ClearError(error);
    const Descriptor* descriptor = FindDescriptor(index);
    if (descriptor == nullptr) {
        Fail(error, "School Days Replay HScene index is out of range");
        return nullptr;
    }
    if (descriptor->kind != Kind::Ordinary) {
        Fail(error, "School Days Replay HScene index uses a special choice table");
        return nullptr;
    }
    if (!ValidateSequence(descriptor->ordinary, orsRoot, error)) return nullptr;
    return &descriptor->ordinary;
}

const SchoolDaysReplayScriptOracle::SequenceDescriptor*
SchoolDaysReplayScriptOracle::SpecialChoiceSequence(int index, int choice,
                                                    const char* orsRoot,
                                                    std::string* error) {
    ClearError(error);
    const Descriptor* descriptor = FindDescriptor(index);
    if (descriptor == nullptr) {
        Fail(error, "School Days Replay HScene index is out of range");
        return nullptr;
    }
    if (descriptor->kind != Kind::SpecialChoiceTable) {
        Fail(error, "School Days Replay HScene index has no special choice table");
        return nullptr;
    }
    for (std::size_t i = 0; i < descriptor->choiceCount; ++i) {
        if (descriptor->choices[i].choice != choice) continue;
        if (!ValidateSequence(descriptor->choices[i].sequence, orsRoot, error))
            return nullptr;
        return &descriptor->choices[i].sequence;
    }
    Fail(error, "School Days Replay special choice is out of range");
    return nullptr;
}

bool SchoolDaysReplayScriptOracle::ResolveSpecialNextCursor(
    int index, int choice, int currentCursor, int priorSelectResult,
    int* nextCursor, bool* completed, std::string* error) {
    ClearError(error);
    if (nextCursor == nullptr || completed == nullptr) {
        return Fail(error, "School Days Replay special cursor output is null");
    }
    *nextCursor = -1;
    *completed = false;

    const SequenceDescriptor* sequence =
        SpecialChoiceSequence(index, choice, nullptr, error);
    if (sequence == nullptr) return false;
    if (currentCursor < 0 ||
        static_cast<std::size_t>(currentCursor) >= sequence->count) {
        return Fail(error, "School Days Replay special cursor is out of range");
    }

    int column = 0;
    switch (priorSelectResult) {
    case -2:
    case -1:
        column = 0;
        break;
    case 0:
        column = 1;
        break;
    case 1:
        column = 2;
        break;
    default:
        return Fail(error,
                    "School Days Replay prior select result is unsupported");
    }

    int resolved = -1;
    if (index == 11) {
        switch (choice) {
        case 0: resolved = k11Next0[currentCursor][column]; break;
        case 1: resolved = k11Next1[currentCursor][column]; break;
        case 2: resolved = k11Next2[currentCursor][column]; break;
        case 3: resolved = k11Next3[currentCursor][column]; break;
        default:
            return Fail(error, "School Days Replay HScene11 choice is unsupported");
        }
    } else if (index == 22 || index == 30) {
        resolved = currentCursor + 1;
    } else {
        return Fail(error, "School Days Replay HScene is not a special selector");
    }

    if (resolved == static_cast<int>(sequence->count)) {
        *completed = true;
        return true;
    }
    if (resolved < 0 || static_cast<std::size_t>(resolved) >= sequence->count) {
        return Fail(error, "School Days Replay native special cursor escaped its table");
    }
    *nextCursor = resolved;
    return true;
}

SchoolDaysReplayScriptOracle::TransitionCursorResolution
SchoolDaysReplayScriptOracle::CursorResolutionFor(int index) {
    const Descriptor* descriptor = FindDescriptor(index);
    return descriptor != nullptr ? descriptor->cursorResolution
                                 : TransitionCursorResolution::Invalid;
}

bool SchoolDaysReplayScriptOracle::ValidateCorpus(const char* orsRoot,
                                                  std::string* error) {
    ClearError(error);
    if (orsRoot == nullptr)
        return Fail(error, "School Days Replay ORS root was not provided");
    for (const Descriptor& descriptor : kHScenes) {
        if (!ValidateDescriptor(descriptor, orsRoot, error)) return false;
    }
    return true;
}

std::string SchoolDaysReplayScriptOracle::OrsRelativePath(const char* sceneKey) {
    if (sceneKey == nullptr || sceneKey[0] == '\0') return {};
    return std::string(sceneKey) + ".ENG.ORS";
}

} // namespace Kotonoha
