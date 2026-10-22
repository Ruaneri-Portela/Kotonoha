#pragma once

#include <cstdint>

namespace Kotonoha {

// Injectable boundary for the legacy SOM serial protocol.  Production wiring
// intentionally supplies no backend, so this interface cannot perform physical
// I/O unless a future caller explicitly provides an isolated implementation.
class SchoolDaysLegacySomConBackend {
public:
    virtual ~SchoolDaysLegacySomConBackend() = default;

    // Native helper 0x100214B0 closes/resets the current device before each
    // scan/manual-open attempt.
    virtual void Reset() = 0;
    virtual bool OpenDevice(int index) = 0;
    virtual bool Transact(const char* command) = 0;
};

// Pure state machine for ConfigMenu's authenticated SomCon controls.  Device
// indices 0..8 are the only statically proven safe native opener inputs.  The
// shipped control15 maps to index9 through an out-of-bounds native table access
// and is therefore represented as an explicit fail-closed result.
class SchoolDaysLegacySomCon {
public:
    struct State {
        bool useSom = false;       // ConfigMenu +0x328 / persisted UseSOM
        bool connected = false;    // ConfigMenu +0x31C
        bool secondary = false;    // ConfigMenu +0x320
        int deviceIndex = 0;       // ConfigMenu +0x324; meaningful if connected
        bool errorFlag = false;    // ConfigMenu +0x32C / outer mode7 latch
    };

    enum class ControlResult : uint8_t {
        Unavailable,
        Handled,
        RequestErrorPopup,
        UnsafeDeviceIndex,
    };

    explicit SchoolDaysLegacySomCon(
        SchoolDaysLegacySomConBackend* backend = nullptr)
        : backend_(backend) {}

    const State& CurrentState() const { return state_; }

    // Startup projection of persisted UseSOM.  Native startup scans only when
    // the persisted flag is true.  A failed startup scan clears UseSOM for the
    // next persistence write but does not manufacture ConfigMenu's control4
    // error popup latch.
    bool ApplyPersistedUseSom(bool enabled);

    ControlResult ActivateControl(int control);

    // Runtime protocol seams used by the authenticated host callbacks.  They
    // are no-ops unless an authenticated backend is connected.
    bool SomMove(uint8_t value);
    bool SomStop();

    void ClearErrorFlag() { state_.errorFlag = false; }

private:
    bool AutoScan(bool setErrorOnFailure);
    bool HandshakeDevice(int index, bool updateConnectionFlags);

    SchoolDaysLegacySomConBackend* backend_ = nullptr;
    State state_{};
};

} // namespace Kotonoha
