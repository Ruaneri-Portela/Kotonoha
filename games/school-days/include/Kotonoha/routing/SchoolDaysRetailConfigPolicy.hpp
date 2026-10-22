#pragma once

#include <cstdint>

namespace Kotonoha {

// Pure v1.02 Config/Sound state.  Values stay in retail units here; conversion
// to Kotonoha's linear audio gain belongs to the outer adapter.
class SchoolDaysRetailConfigPolicy {
public:
    enum class NativeAudioBank : int {
        Voice = 0,
        Se = 1,
        Bgm = 2,
    };

    // ConfigMenu's visible bank order is BGM, SE, Voice and therefore differs
    // from the exported GetMasterVolume native bank ids above.
    enum class UiAudioBank : int {
        Bgm = 0,
        Se = 1,
        Voice = 2,
    };

    struct State {
        float masterVolume = -1.0f;
        int voiceVolume = 5;
        int bgmVolume = 5;
        int seVolume = 5;
        bool textView = true;
        bool menVoice = true;
        bool mute = false;
        bool skip = false;
        bool autoDraw = true;
        bool autoMode = false;
        bool superSkip = false;
        bool useSom = false;
    };

    static State Defaults();
    static bool Validate(const State& state);

    static NativeAudioBank NativeBankForUi(UiAudioBank bank);
    static int Level(const State& state, NativeAudioBank bank);
    static bool SetLevel(State* state, NativeAudioBank bank, int level);
    static bool SetUiLevel(State* state, UiAudioBank bank, int level);

    // SysMenu 0x10007140: nonzero direction decrements level, zero increments;
    // result is clamped to [0,10].
    static bool StepUiLevel(State* state, UiAudioBank bank, int direction);

    // _SetMasterVolume@4 / 0x100071A0: nonzero adds 1.0 toward -1.0,
    // zero subtracts 1.0 toward -10.0.
    static bool StepMasterVolume(State* state, int direction);

    // _GetMasterVolume(bank): (11 - level) * masterVolume.
    static float NativeMasterValue(const State& state, NativeAudioBank bank);

    // EXE 0x4434A0 DirectSound curve.  It maps <= -11 to DSBVOLUME_MIN
    // (-10000 hundredths-dB), >= -1 to 0, and the interior via round(v*175).
    static int DirectSoundHundredthDb(float nativeMasterValue);
    static int DirectSoundHundredthDb(const State& state, NativeAudioBank bank);

    // Exact amplitude-domain equivalent for Kotonoha's linear SDL mix gain.
    static float LinearGainFromHundredthDb(int hundredthDb);
    static float LinearGain(const State& state, NativeAudioBank bank);
};

} // namespace Kotonoha
