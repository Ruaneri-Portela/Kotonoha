#include <Kotonoha/routing/SchoolDaysRetailConfigPolicy.hpp>

#include <algorithm>
#include <cmath>

namespace Kotonoha {

SchoolDaysRetailConfigPolicy::State SchoolDaysRetailConfigPolicy::Defaults() {
    return {};
}

bool SchoolDaysRetailConfigPolicy::Validate(const State& state) {
    const auto levelValid = [](int value) { return value >= 0 && value <= 10; };
    return std::isfinite(state.masterVolume) && state.masterVolume >= -10.0f &&
           state.masterVolume <= -1.0f && levelValid(state.voiceVolume) &&
           levelValid(state.bgmVolume) && levelValid(state.seVolume);
}

SchoolDaysRetailConfigPolicy::NativeAudioBank
SchoolDaysRetailConfigPolicy::NativeBankForUi(UiAudioBank bank) {
    switch (bank) {
    case UiAudioBank::Bgm: return NativeAudioBank::Bgm;
    case UiAudioBank::Se: return NativeAudioBank::Se;
    case UiAudioBank::Voice: return NativeAudioBank::Voice;
    }
    return NativeAudioBank::Voice;
}

int SchoolDaysRetailConfigPolicy::Level(const State& state,
                                        NativeAudioBank bank) {
    switch (bank) {
    case NativeAudioBank::Voice: return state.voiceVolume;
    case NativeAudioBank::Se: return state.seVolume;
    case NativeAudioBank::Bgm: return state.bgmVolume;
    }
    return 0;
}

bool SchoolDaysRetailConfigPolicy::SetLevel(State* state, NativeAudioBank bank,
                                            int level) {
    if (state == nullptr || level < 0 || level > 10) return false;
    switch (bank) {
    case NativeAudioBank::Voice: state->voiceVolume = level; break;
    case NativeAudioBank::Se: state->seVolume = level; break;
    case NativeAudioBank::Bgm: state->bgmVolume = level; break;
    }
    return true;
}

bool SchoolDaysRetailConfigPolicy::SetUiLevel(State* state, UiAudioBank bank,
                                              int level) {
    return SetLevel(state, NativeBankForUi(bank), level);
}

bool SchoolDaysRetailConfigPolicy::StepUiLevel(State* state, UiAudioBank bank,
                                               int direction) {
    if (state == nullptr) return false;
    const NativeAudioBank native = NativeBankForUi(bank);
    const int current = Level(*state, native);
    const int next = direction != 0 ? std::max(0, current - 1)
                                    : std::min(10, current + 1);
    return SetLevel(state, native, next);
}

bool SchoolDaysRetailConfigPolicy::StepMasterVolume(State* state,
                                                    int direction) {
    if (state == nullptr || !std::isfinite(state->masterVolume)) return false;
    if (direction != 0) {
        state->masterVolume = std::min(-1.0f, state->masterVolume + 1.0f);
    } else {
        state->masterVolume = std::max(-10.0f, state->masterVolume - 1.0f);
    }
    return true;
}

float SchoolDaysRetailConfigPolicy::NativeMasterValue(const State& state,
                                                       NativeAudioBank bank) {
    return static_cast<float>(11 - Level(state, bank)) * state.masterVolume;
}

int SchoolDaysRetailConfigPolicy::DirectSoundHundredthDb(
    float nativeMasterValue) {
    if (!std::isfinite(nativeMasterValue) || nativeMasterValue <= -11.0f)
        return -10000;
    if (nativeMasterValue >= -1.0f) return 0;
    const long rounded = std::lround(static_cast<double>(nativeMasterValue) * 175.0);
    return std::clamp(static_cast<int>(rounded), -10000, 0);
}

int SchoolDaysRetailConfigPolicy::DirectSoundHundredthDb(
    const State& state, NativeAudioBank bank) {
    return DirectSoundHundredthDb(NativeMasterValue(state, bank));
}

float SchoolDaysRetailConfigPolicy::LinearGainFromHundredthDb(int hundredthDb) {
    if (hundredthDb <= -10000) return 0.0f;
    if (hundredthDb >= 0) return 1.0f;
    return static_cast<float>(std::pow(10.0, static_cast<double>(hundredthDb) / 2000.0));
}

float SchoolDaysRetailConfigPolicy::LinearGain(const State& state,
                                               NativeAudioBank bank) {
    return LinearGainFromHundredthDb(DirectSoundHundredthDb(state, bank));
}

} // namespace Kotonoha
