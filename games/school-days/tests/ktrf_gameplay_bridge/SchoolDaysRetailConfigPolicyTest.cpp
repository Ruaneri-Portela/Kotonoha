#include <Kotonoha/routing/SchoolDaysRetailConfigPolicy.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
[[noreturn]] void Fail(const char* message) {
    std::cerr << "SchoolDaysRetailConfigPolicyTest: " << message << '\n';
    std::exit(2);
}
void Check(bool value, const char* message) {
    if (!value) Fail(message);
}
bool Near(float a, float b, float epsilon = 0.00001f) {
    return std::fabs(a - b) <= epsilon;
}
} // namespace

int main() {
    using Policy = Kotonoha::SchoolDaysRetailConfigPolicy;
    using Native = Policy::NativeAudioBank;
    using Ui = Policy::UiAudioBank;

    Policy::State state = Policy::Defaults();
    Check(Policy::Validate(state), "retail defaults invalid");
    Check(state.masterVolume == -1.0f && state.voiceVolume == 5 &&
              state.bgmVolume == 5 && state.seVolume == 5 && state.textView &&
              state.menVoice && !state.mute && !state.skip && state.autoDraw &&
              !state.superSkip,
          "0x10006CE0 defaults drifted");

    Check(Policy::NativeBankForUi(Ui::Bgm) == Native::Bgm &&
              Policy::NativeBankForUi(Ui::Se) == Native::Se &&
              Policy::NativeBankForUi(Ui::Voice) == Native::Voice,
          "Config visual bank order was confused with native bank ids");

    Check(Policy::SetUiLevel(&state, Ui::Bgm, 9) && state.bgmVolume == 9 &&
              state.voiceVolume == 5 && state.seVolume == 5,
          "direct BGM level wrote wrong bank");
    Check(Policy::SetUiLevel(&state, Ui::Se, 3) && state.seVolume == 3 &&
              Policy::SetUiLevel(&state, Ui::Voice, 7) && state.voiceVolume == 7,
          "direct SE/Voice level wrote wrong bank");
    Check(!Policy::SetUiLevel(&state, Ui::Bgm, 11) &&
              !Policy::SetUiLevel(nullptr, Ui::Bgm, 5),
          "invalid direct level accepted");

    state.bgmVolume = 5;
    Check(Policy::StepUiLevel(&state, Ui::Bgm, 1) && state.bgmVolume == 4,
          "nonzero step did not decrement");
    Check(Policy::StepUiLevel(&state, Ui::Bgm, 0) && state.bgmVolume == 5,
          "zero step did not increment");
    state.bgmVolume = 0;
    Check(Policy::StepUiLevel(&state, Ui::Bgm, 1) && state.bgmVolume == 0,
          "level underflow was not clamped");
    state.bgmVolume = 10;
    Check(Policy::StepUiLevel(&state, Ui::Bgm, 0) && state.bgmVolume == 10,
          "level overflow was not clamped");

    state = Policy::Defaults();
    Check(Policy::StepMasterVolume(&state, 0) && state.masterVolume == -2.0f,
          "zero master direction did not subtract one");
    Check(Policy::StepMasterVolume(&state, 1) && state.masterVolume == -1.0f,
          "nonzero master direction did not add one");
    for (int i = 0; i < 20; ++i) Policy::StepMasterVolume(&state, 0);
    Check(state.masterVolume == -10.0f, "master lower clamp drifted");
    for (int i = 0; i < 20; ++i) Policy::StepMasterVolume(&state, 1);
    Check(state.masterVolume == -1.0f, "master upper clamp drifted");

    state = Policy::Defaults();
    state.voiceVolume = 0;
    state.seVolume = 10;
    state.bgmVolume = 5;
    Check(Policy::NativeMasterValue(state, Native::Voice) == -11.0f &&
              Policy::NativeMasterValue(state, Native::Se) == -1.0f &&
              Policy::NativeMasterValue(state, Native::Bgm) == -6.0f,
          "GetMasterVolume native formula drifted");
    Check(Policy::DirectSoundHundredthDb(state, Native::Voice) == -10000 &&
              Policy::DirectSoundHundredthDb(state, Native::Se) == 0 &&
              Policy::DirectSoundHundredthDb(state, Native::Bgm) == -1050,
          "0x4434A0 DirectSound curve drifted");

    Check(Policy::DirectSoundHundredthDb(-10.0f) == -1750 &&
              Policy::DirectSoundHundredthDb(-2.0f) == -350 &&
              Policy::DirectSoundHundredthDb(-11.1f) == -10000 &&
              Policy::DirectSoundHundredthDb(-0.5f) == 0,
          "DirectSound boundary/interior conversion drifted");
    Check(Policy::LinearGainFromHundredthDb(-10000) == 0.0f &&
              Policy::LinearGainFromHundredthDb(0) == 1.0f &&
              Near(Policy::LinearGainFromHundredthDb(-600),
                   static_cast<float>(std::pow(10.0, -600.0 / 2000.0))),
          "hundredths-dB to SDL linear gain conversion drifted");

    Policy::State invalid = Policy::Defaults();
    invalid.voiceVolume = 11;
    Check(!Policy::Validate(invalid), "invalid level validated");
    invalid = Policy::Defaults();
    invalid.masterVolume = -10.1f;
    Check(!Policy::Validate(invalid), "invalid master volume validated");

    std::cout << "SCHOOL DAYS RETAIL CONFIG POLICY GATE PASS\n"
              << "native_defaults=PASS\n"
              << "ui_bgm_se_voice_to_native_2_1_0=PASS\n"
              << "direct_levels_1_10=PASS\n"
              << "step_level_direction_clamp=PASS\n"
              << "master_step_minus10_minus1=PASS\n"
              << "getmaster_formula=PASS\n"
              << "directsound_175_curve=PASS\n"
              << "linear_gain_adapter=PASS\n";
    return 0;
}
