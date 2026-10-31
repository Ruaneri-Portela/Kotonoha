#include <Kotonoha/routing/SchoolDaysRetailConfigPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapModel.hpp>

#include <cmath>
#include <iostream>
#include <string>

namespace {
using Policy = Kotonoha::SchoolDaysRetailConfigPolicy;

int failures = 0;
void Require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
    ++failures;
  }
}
}

int main() {
  auto state = Policy::Defaults();
  Require(Policy::Validate(state), "Retail config defaults must validate");
  Require(Policy::NativeBankForUi(Policy::UiAudioBank::Bgm) == Policy::NativeAudioBank::Bgm &&
              Policy::NativeBankForUi(Policy::UiAudioBank::Voice) == Policy::NativeAudioBank::Voice,
          "ConfigMenu bank order must map onto the native banks");

  Require(Policy::SetUiLevel(&state, Policy::UiAudioBank::Se, 7) &&
              Policy::Level(state, Policy::NativeAudioBank::Se) == 7,
          "SetUiLevel must store the SE level");
  Require(!Policy::SetUiLevel(&state, Policy::UiAudioBank::Se, 11), "Levels above 10 must be rejected");

  for (int i = 0; i < 20; ++i) Policy::StepUiLevel(&state, Policy::UiAudioBank::Bgm, 0);
  const int top = Policy::Level(state, Policy::NativeAudioBank::Bgm);
  for (int i = 0; i < 20; ++i) Policy::StepUiLevel(&state, Policy::UiAudioBank::Bgm, 1);
  const int bottom = Policy::Level(state, Policy::NativeAudioBank::Bgm);
  Require(top == 10 && bottom == 0, "StepUiLevel must clamp to [0,10]");

  bool bounded = true;
  for (int level = 0; level <= 10; ++level) {
    Policy::SetUiLevel(&state, Policy::UiAudioBank::Voice, level);
    const float gain = Policy::LinearGain(state, Policy::NativeAudioBank::Voice);
    bounded = bounded && std::isfinite(gain) && gain >= 0 && gain <= 1.0001f;
  }
  Require(bounded, "Linear gain must stay within [0,1]");
  Policy::SetUiLevel(&state, Policy::UiAudioBank::Voice, 0);
  const float low = Policy::LinearGain(state, Policy::NativeAudioBank::Voice);
  Policy::SetUiLevel(&state, Policy::UiAudioBank::Voice, 10);
  const float high = Policy::LinearGain(state, Policy::NativeAudioBank::Voice);
  Require(low != high, "Voice level must change the mixer gain");

  for (int major = 0; major < 6; ++major) {
    const int pages = Kotonoha::SchoolDaysRouteMapModel::SubpageCountForMajor(major);
    Require(pages > 0, "Every route map story needs at least one page");
    for (int page = 0; page < pages; ++page)
      Require(Kotonoha::SchoolDaysRouteMapModel::Page(major, page).nodeCount > 0,
              "Route map pages must expose nodes");
  }
  if (failures == 0) std::cout << "SchoolDaysUiModelTest passed\n";
  return failures == 0 ? 0 : 1;
}
