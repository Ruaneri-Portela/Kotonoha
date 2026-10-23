#include <Kotonoha/parsers/Config.h>
#include <Kotonoha/utils/Time.h>
#include <imgui.h>

#include <cmath>

int main() {
  if (ImGui::GetCurrentContext() != nullptr)
    return 3;

  Kotonoha_Config config{};
  const bool configRequiresImGuiContext =
      !KotonohaConfigLoad(&config, nullptr, 0, nullptr) &&
      config.cancelOpen;
  KotonohaConfigFree(&config);

  Kotonoha_Time *clock = KotonohaTimeNew(true);
  if (clock == nullptr)
    return 1;

  KotonohaTimeSet(clock, 1250);
  const bool positionOk = KotonohaTimeGet(clock) == 1250;
  const bool rateOk = KotonohaTimeSetRate(clock, 2.0) &&
                      std::abs(KotonohaTimeGetRate(clock) - 2.0) < 0.000001;

  KotonohaTimeDestroy(clock);
  return configRequiresImGuiContext && positionOk && rateOk ? 0 : 2;
}
