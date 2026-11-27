#include <Kotonoha/routing/SchoolDaysLxhFade.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
using Fade = Kotonoha::SchoolDaysLxhFade;

void Check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

constexpr uint32_t Tick(unsigned minutes, unsigned seconds, unsigned frames) {
  return (minutes * 60u + seconds) * 24u + frames;
}

void TestEpilogue(uint32_t creditsEnd, uint32_t movieStart) {
  Fade fade;
  fade.Configure(true, creditsEnd, movieStart);
  Check(fade.Enabled(), "LxH ending did not configure");
  Check(!fade.BeginAtCreditBoundary(creditsEnd - 1u, 1000u),
        "Original credits ended early");
  Check(!fade.InputLocked() && fade.BlackAlpha(1000u) == 0u,
        "Normal credits should remain visible");
  Check(fade.BeginAtCreditBoundary(creditsEnd, 1000u),
        "EndRoll boundary did not start outgoing fade");
  Check(fade.ClockHeld() && fade.InputLocked(),
        "Entry OUT did not hold story clock/input");
  Check(fade.BlackAlpha(1000u) == 0u &&
        fade.BlackAlpha(1250u) >= 126u &&
        fade.BlackAlpha(1250u) <= 128u &&
        fade.BlackAlpha(1500u) == 255u,
        "Entry OUT does not interpolate to black in 500ms");
  fade.Update(1500u);
  Check(fade.CurrentPhase() == Fade::Phase::AwaitMovie &&
        !fade.ClockHeld() && fade.BlackAlpha(2500u) == 255u,
        "The authored gap to LxH movie was not held black");
  Check(!fade.BeginAtCreditBoundary(movieStart, 2600u),
        "Boundary triggered more than once");
  fade.MovieStarted(movieStart + 1u, 2600u);
  Check(fade.CurrentPhase() == Fade::Phase::AwaitMovie,
        "A different movie started the reveal");
  fade.MovieStarted(movieStart, 2600u);
  Check(fade.CurrentPhase() == Fade::Phase::EntryIn &&
        fade.BlackAlpha(2600u) == 255u,
        "The first LxH movie did not start black reveal");
  Check(fade.BlackAlpha(2850u) >= 126u &&
        fade.BlackAlpha(2850u) <= 128u,
        "Entry IN midpoint does not interpolate");
  fade.Update(3100u);
  Check(fade.CurrentPhase() == Fade::Phase::Epilogue &&
        fade.BlackAlpha(3100u) == 0u && !fade.InputLocked(),
        "Epilogue did not become fully visible");

  Check(fade.StartCountdownExit(7000u),
        "LxH Next did not start countdown fade");
  Check(!fade.ExitReady(7499u) && fade.BlackAlpha(7250u) >= 126u,
        "Countdown advanced before becoming fully black");
  Check(fade.StartCountdownExit(7250u),
        "Subsequent update incorrectly discarded pending exit");
  Check(fade.ExitReady(7500u), "Countdown handoff did not become ready");
  fade.CountdownOpened(7500u);
  Check(fade.CurrentPhase() == Fade::Phase::ExitIn &&
        fade.BlackAlpha(7500u) == 255u && fade.HoldCountdown(),
        "Countdown must be covered and its 30-second clock held");
  fade.Update(7750u);
  Check(fade.HoldCountdown() && fade.BlackAlpha(7750u) >= 126u,
        "Countdown reveal was too early");
  fade.Update(8000u);
  Check(!fade.HoldCountdown() && !fade.InputLocked() &&
        fade.CurrentPhase() == Fade::Phase::Complete &&
        fade.BlackAlpha(8000u) == 0u,
        "The full 30-second countdown must start after reveal");
  Check(!fade.StartCountdownExit(8500u),
        "The countdown fade can be retriggered after completion");
}
} // namespace

int main() {
  try {
    // Actual per-ending EndRoll→LxH boundaries from the private read-only
    // audit, covering variable gaps and the exact same-tick SB case.
    constexpr std::array<std::pair<uint32_t, uint32_t>, 7> cases{{
      {Tick(4, 4, 11), Tick(4, 6, 11)},
      {Tick(4, 10, 3), Tick(4, 13, 3)},
      {Tick(3, 56, 19), Tick(3, 58, 19)},
      {Tick(2, 50, 15), Tick(2, 51, 15)},
      {Tick(13, 51, 13), Tick(13, 53, 13)},
      {Tick(4, 37, 20), Tick(4, 37, 20)},
      {Tick(3, 32, 23), Tick(3, 35, 23)},
    }};
    for (const auto &item : cases)
      TestEpilogue(item.first, item.second);

    Fade disabled;
    disabled.Configure(false, cases.front().first, cases.front().second);
    Check(!disabled.Enabled() && !disabled.StartCountdownExit(1000u) &&
          disabled.BlackAlpha(1000u) == 0u && !disabled.InputLocked(),
          "LxH OFF must not render extra fades");
    Fade invalid;
    invalid.Configure(true, 100u, 99u);
    Check(!invalid.Enabled(), "Malformed LxH timeline must fail closed");
    Fade seek;
    seek.Configure(true, 200u, 300u);
    Check(seek.BeginAtCreditBoundary(200u, 1000u), "Seek fixture missing fade");
    seek.SeekTo(400u);
    Check(seek.CurrentPhase() == Fade::Phase::Epilogue &&
          !seek.InputLocked() && seek.BlackAlpha(1100u) == 0u,
          "Seeking past credits must not leave a black screen");
    Check(seek.StartCountdownExit(1200u),
          "Natural final Next after seek must still fade to countdown");
    seek.Reset();
    Check(!seek.Enabled() && seek.BlackAlpha(1400u) == 0u,
          "New scene must discard an interrupted fade");
    std::cout << "SchoolDaysLxhFadeTest PASS: seven entry/exit pairs, "
                 "500ms black out/in, countdown full 30s, OFF, seek\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysLxhFadeTest FAIL: " << error.what() << '\n';
    return 1;
  }
}
