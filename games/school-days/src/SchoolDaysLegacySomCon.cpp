#include <Kotonoha/routing/SchoolDaysLegacySomCon.hpp>

#include <cstdio>

namespace Kotonoha {

bool SchoolDaysLegacySomCon::HandshakeDevice(int index,
                                             bool updateConnectionFlags) {
    if (backend_ == nullptr || index < 0 || index > 8) return false;

    backend_->Reset();
    if (!backend_->OpenDevice(index) || !backend_->Transact("s00")) return false;

    state_.deviceIndex = index;
    if (updateConnectionFlags) {
        state_.connected = true;
        state_.useSom = true;
    }
    return true;
}

bool SchoolDaysLegacySomCon::AutoScan(bool setErrorOnFailure) {
    for (int index = 0; index <= 8; ++index) {
        if (HandshakeDevice(index, true)) return true;

        // FUN_10007850 clears all three flags after every failed candidate
        // before advancing to the next safe index.
        state_.connected = false;
        state_.useSom = false;
        state_.secondary = false;
    }

    if (setErrorOnFailure) state_.errorFlag = true;
    return false;
}

bool SchoolDaysLegacySomCon::ApplyPersistedUseSom(bool enabled) {
    state_.useSom = enabled;
    state_.connected = false;
    state_.secondary = false;
    if (!enabled) return true;
    return AutoScan(false);
}

SchoolDaysLegacySomCon::ControlResult
SchoolDaysLegacySomCon::ActivateControl(int control) {
    if (control == 4) {
        return AutoScan(true) ? ControlResult::Handled
                              : ControlResult::RequestErrorPopup;
    }
    if (control == 5) {
        // Native control5 mutates only the two ConfigMenu flags.  It does not
        // close/reset the helper object or alter the remembered device/secondary
        // state at this call site.
        state_.useSom = false;
        state_.connected = false;
        return ControlResult::Handled;
    }

    if (control < 6 || control > 17 || !state_.useSom || !state_.connected)
        return ControlResult::Unavailable;

    if (control >= 6 && control <= 14) {
        // Manual controls6..14 map exactly to safe indices0..8.  Native leaves
        // useSom/connected unchanged when the replacement open/handshake fails
        // and updates +0x324 only after both operations succeed.
        (void)HandshakeDevice(control - 6, false);
        return ControlResult::Handled;
    }
    if (control == 15) {
        return ControlResult::UnsafeDeviceIndex;
    }
    if (control == 16) {
        if (!state_.secondary && backend_ != nullptr &&
            backend_->Transact("s96")) {
            state_.secondary = true;
        }
        return ControlResult::Handled;
    }

    if (state_.secondary && backend_ != nullptr && backend_->Transact("b")) {
        state_.secondary = false;
    }
    return ControlResult::Handled;
}

bool SchoolDaysLegacySomCon::SomMove(uint8_t value) {
    if (!state_.connected || backend_ == nullptr) return false;
    char command[4] = {};
    std::snprintf(command, sizeof(command), "s%02x",
                  static_cast<unsigned>(value));
    return backend_->Transact(command);
}

bool SchoolDaysLegacySomCon::SomStop() {
    return state_.connected && backend_ != nullptr && backend_->Transact("b");
}

} // namespace Kotonoha
