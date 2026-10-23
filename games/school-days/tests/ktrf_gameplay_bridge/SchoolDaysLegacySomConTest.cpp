#include <Kotonoha/routing/SchoolDaysLegacySomCon.hpp>
#include <Kotonoha/routing/SchoolDaysSomConSetModel.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void Fail(const char* message) {
    std::cerr << "SchoolDaysLegacySomConTest: " << message << '\n';
    std::exit(2);
}

void Check(bool condition, const char* message) {
    if (!condition) Fail(message);
}

struct Backend final : Kotonoha::SchoolDaysLegacySomConBackend {
    std::array<bool, 9> open{};
    std::array<bool, 9> handshake{};
    bool s96 = true;
    bool stop = true;
    int current = -1;
    std::vector<std::string> calls;

    void Reset() override {
        calls.emplace_back("reset");
        current = -1;
    }

    bool OpenDevice(int index) override {
        calls.emplace_back("open:" + std::to_string(index));
        if (index < 0 || index >= 9 || !open[static_cast<size_t>(index)]) {
            return false;
        }
        current = index;
        return true;
    }

    bool Transact(const char* command) override {
        const std::string value = command != nullptr ? command : "<null>";
        calls.emplace_back("tx:" + value);
        if (value == "s00") {
            return current >= 0 && current < 9 &&
                   handshake[static_cast<size_t>(current)];
        }
        if (value == "s96") return s96;
        if (value == "b") return stop;
        return true;
    }
};

void CheckCalls(const std::vector<std::string>& actual,
                const std::vector<std::string>& expected,
                const char* message) {
    if (actual != expected) {
        std::cerr << "expected:";
        for (const auto& value : expected) std::cerr << ' ' << value;
        std::cerr << "\nactual:";
        for (const auto& value : actual) std::cerr << ' ' << value;
        std::cerr << '\n';
        Fail(message);
    }
}

void WriteLe32(std::ofstream& stream, uint32_t value) {
    const uint8_t bytes[4] = {
        static_cast<uint8_t>(value & 0xffu),
        static_cast<uint8_t>((value >> 8u) & 0xffu),
        static_cast<uint8_t>((value >> 16u) & 0xffu),
        static_cast<uint8_t>((value >> 24u) & 0xffu),
    };
    stream.write(reinterpret_cast<const char*>(bytes), 4);
}

void WriteOneControlCmap(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    WriteLe32(stream, 2u);
    WriteLe32(stream, 1u);
    const uint8_t regions[2] = {1u, 0u};
    stream.write(reinterpret_cast<const char*>(regions), 2);
}

void TestNullBackendFailClosed() {
    using Som = Kotonoha::SchoolDaysLegacySomCon;
    Som som;
    Check(som.ApplyPersistedUseSom(false),
          "persisted UseSOM=false should not require a backend");
    Check(!som.CurrentState().useSom && !som.CurrentState().connected,
          "persisted UseSOM=false changed null-backend state");

    Check(som.ActivateControl(4) == Som::ControlResult::RequestErrorPopup,
          "control4 null-backend scan did not request mode7 popup");
    Check(!som.CurrentState().useSom && !som.CurrentState().connected &&
              som.CurrentState().errorFlag,
          "control4 null-backend failure truth table mismatch");
    Check(som.ActivateControl(6) == Som::ControlResult::Unavailable,
          "disconnected manual device control was accepted");
    som.ClearErrorFlag();
    Check(!som.CurrentState().errorFlag,
          "mode7 return did not clear errorFlag");

    Som startup;
    Check(!startup.ApplyPersistedUseSom(true),
          "null backend unexpectedly satisfied startup auto-scan");
    Check(!startup.CurrentState().useSom && !startup.CurrentState().connected &&
              !startup.CurrentState().errorFlag,
          "startup scan failure should clear UseSOM without popup latch");
}

void TestAutoScanTruthTable() {
    using Som = Kotonoha::SchoolDaysLegacySomCon;
    Backend backend;
    backend.open[1] = true;
    backend.open[2] = true;
    backend.handshake[1] = false;
    backend.handshake[2] = true;
    Som som(&backend);

    Check(som.ActivateControl(4) == Som::ControlResult::Handled,
          "auto-scan did not accept first successful candidate");
    CheckCalls(backend.calls,
               {"reset", "open:0", "reset", "open:1", "tx:s00",
                "reset", "open:2", "tx:s00"},
               "auto-scan reset/open/s00 sequence mismatch");
    Check(som.CurrentState().useSom && som.CurrentState().connected &&
              som.CurrentState().deviceIndex == 2 &&
              !som.CurrentState().errorFlag,
          "auto-scan success state mismatch");

    // A later total failure clears +0x328/+0x31C and secondary +0x320.
    backend.s96 = true;
    Check(som.ActivateControl(16) == Som::ControlResult::Handled &&
              som.CurrentState().secondary,
          "secondary start setup failed");
    backend.open.fill(false);
    backend.handshake.fill(false);
    backend.calls.clear();
    Check(som.ActivateControl(4) == Som::ControlResult::RequestErrorPopup,
          "total rescan failure did not request popup");
    std::vector<std::string> failedScanCalls;
    for (int index = 0; index <= 8; ++index) {
        failedScanCalls.emplace_back("reset");
        failedScanCalls.emplace_back("open:" + std::to_string(index));
    }
    CheckCalls(backend.calls, failedScanCalls,
               "total scan failure did not probe exactly safe indices0..8");
    Check(!som.CurrentState().useSom && !som.CurrentState().connected &&
              !som.CurrentState().secondary && som.CurrentState().errorFlag,
          "total rescan failure did not clear native flags");
}

void TestManualAndSecondaryControls() {
    using Som = Kotonoha::SchoolDaysLegacySomCon;
    Backend backend;
    backend.open.fill(true);
    backend.handshake.fill(true);
    Som som(&backend);
    Check(som.ActivateControl(4) == Som::ControlResult::Handled,
          "manual test setup scan failed");

    for (int control = 6; control <= 14; ++control) {
        backend.calls.clear();
        const int index = control - 6;
        Check(som.ActivateControl(control) == Som::ControlResult::Handled,
              "safe manual device control was not handled");
        CheckCalls(backend.calls,
                   {"reset", "open:" + std::to_string(index), "tx:s00"},
                   "manual reset/open/s00 sequence mismatch");
        Check(som.CurrentState().deviceIndex == index,
              "manual success did not project device index");
    }

    backend.calls.clear();
    const int priorIndex = som.CurrentState().deviceIndex;
    Check(som.ActivateControl(15) == Som::ControlResult::UnsafeDeviceIndex,
          "native-unsafe index9 was not rejected");
    Check(backend.calls.empty() && som.CurrentState().deviceIndex == priorIndex,
          "unsafe index9 reached backend or changed device state");

    backend.calls.clear();
    Check(som.ActivateControl(16) == Som::ControlResult::Handled &&
              som.CurrentState().secondary,
          "control16 s96 success did not set secondary");
    CheckCalls(backend.calls, {"tx:s96"}, "control16 transaction mismatch");
    backend.calls.clear();
    Check(som.ActivateControl(16) == Som::ControlResult::Handled &&
              backend.calls.empty(),
          "control16 retriggered while secondary already set");

    Check(som.ActivateControl(17) == Som::ControlResult::Handled &&
              !som.CurrentState().secondary,
          "control17 b success did not clear secondary");
    CheckCalls(backend.calls, {"tx:b"}, "control17 transaction mismatch");
    backend.calls.clear();
    Check(som.ActivateControl(17) == Som::ControlResult::Handled &&
              backend.calls.empty(),
          "control17 retriggered while secondary already clear");

    backend.s96 = false;
    Check(som.ActivateControl(16) == Som::ControlResult::Handled &&
              !som.CurrentState().secondary,
          "failed s96 transaction changed secondary state");
    backend.s96 = true;
    Check(som.ActivateControl(16) == Som::ControlResult::Handled &&
              som.CurrentState().secondary,
          "secondary restore setup failed");
    backend.stop = false;
    Check(som.ActivateControl(17) == Som::ControlResult::Handled &&
              som.CurrentState().secondary,
          "failed b transaction cleared secondary state");
    backend.stop = true;
    Check(som.ActivateControl(17) == Som::ControlResult::Handled &&
              !som.CurrentState().secondary,
          "secondary cleanup failed");

    backend.calls.clear();
    Check(som.SomMove(0x33) && som.SomStop(),
          "connected SomMove/SomStop abstract transactions failed");
    CheckCalls(backend.calls, {"tx:s33", "tx:b"},
               "SomMove/SomStop command formatting mismatch");

    Check(som.ActivateControl(5) == Som::ControlResult::Handled &&
              !som.CurrentState().useSom && !som.CurrentState().connected,
          "control5 did not clear UseSOM/connected");
    backend.calls.clear();
    Check(!som.SomMove(0xff) && !som.SomStop() && backend.calls.empty(),
          "disconnected callback path reached backend");
}

void TestManualFailureKeepsPublishedState() {
    using Som = Kotonoha::SchoolDaysLegacySomCon;
    Backend backend;
    backend.open[0] = true;
    backend.handshake[0] = true;
    Som som(&backend);
    Check(som.ActivateControl(4) == Som::ControlResult::Handled,
          "manual-failure setup scan failed");
    const int oldIndex = som.CurrentState().deviceIndex;
    backend.open[3] = false;
    backend.calls.clear();
    Check(som.ActivateControl(9) == Som::ControlResult::Handled,
          "manual failed open should still be a handled native control");
    CheckCalls(backend.calls, {"reset", "open:3"},
               "manual failed-open sequence mismatch");
    Check(som.CurrentState().useSom && som.CurrentState().connected &&
              som.CurrentState().deviceIndex == oldIndex,
          "manual failure changed native published flags/index");
}

void TestSomConSetPopupModel() {
    using Popup = Kotonoha::SchoolDaysSomConSetModel;
    using Variant = Popup::Variant;
    Check(Popup::CmapRelativePath(Variant::Standard) ==
              "System/Option/Pop_Som.cmap" &&
              Popup::CmapRelativePath(Variant::Wide) ==
              "System/Option/Pop_Som_Wide.cmap" &&
              Popup::CmapRelativePath(Variant::WideFull) ==
              "System/Option/Pop_Som_Wide_Full.cmap" &&
              Popup::CmapRelativePath(Variant::WideNote) ==
              "System/Option/Pop_Som_Wide_Note.cmap",
          "SomconSet variant CMAP paths mismatch");
    Check(std::string(Popup::BaseRelativePath()) == "System/Option/Pop_Som.png" &&
              std::string(Popup::ChipRelativePath()) ==
                  "System/Option/Pop_Som_Chip.png",
          "SomconSet PNG resource paths mismatch");

    const auto root = std::filesystem::temp_directory_path() /
                      "kotonoha_schooldays_somcon_set_test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    const std::array<Variant, 4> variants = {
        Variant::Standard, Variant::Wide, Variant::WideFull, Variant::WideNote};
    for (const auto variant : variants) {
        WriteOneControlCmap(root / Popup::CmapRelativePath(variant));
    }

    Popup popup;
    std::string error;
    for (const auto variant : variants) {
        Check(popup.Open(root.string(), variant, &error), error.c_str());
        Check(popup.Cmap().Width() == 2u && popup.Cmap().Height() == 1u,
              "SomconSet CMAP dimensions mismatch");
        Check(popup.SetSelectionFromPointer(0, 0) && popup.Selection() == 0,
              "SomconSet sole control hover mismatch");
        int transition = -1;
        Check(popup.Activate(&transition) &&
                  transition == Popup::kReturnToConfigHostTransition &&
                  transition == 2,
              "SomconSet sole control did not return host transition2");
        Check(popup.SetSelectionFromPointer(1, 0) && popup.Selection() == -1 &&
                  !popup.Activate(&transition),
              "SomconSet background incorrectly activated");
    }
    popup.Close();
    std::filesystem::remove_all(root, ignored);
}

} // namespace

int main() {
    TestNullBackendFailClosed();
    TestAutoScanTruthTable();
    TestManualAndSecondaryControls();
    TestManualFailureKeepsPublishedState();
    TestSomConSetPopupModel();
    std::cout << "SchoolDaysLegacySomConTest: PASS\n";
    return 0;
}
