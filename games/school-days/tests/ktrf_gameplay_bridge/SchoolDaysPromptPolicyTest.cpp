#include <Kotonoha/Gameplay.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/routing/SchoolDaysPromptBehavior.hpp>
#include <Kotonoha/routing/SchoolDaysInputPolicy.hpp>

#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

Kotonoha::SchoolDaysPromptPolicy Policy(
    Kotonoha::SchoolDaysPromptOrientation orientation =
        Kotonoha::SchoolDaysPromptOrientation::Unconfigured) {
  Kotonoha::SchoolDaysPromptPolicy policy{};
  policy.enabled = true;
  policy.twoChoiceOrientation = orientation;
  policy.startTick = 10;
  policy.endTick = 20;
  return policy;
}

struct FixedRandom {
  Uint32 value = 0;
  int calls = 0;
};

Uint32 NextFixedRandom(void *userdata) {
  auto *random = static_cast<FixedRandom *>(userdata);
  ++random->calls;
  return random->value;
}

} // namespace

int main() {
  using Kotonoha::SchoolDaysPromptInteraction;
  using Kotonoha::SchoolDaysPromptOrientation;

  SchoolDaysPromptInteraction generic;
  Check(!generic.Enabled(), "generic Prompt policy must remain opt-in");

  SchoolDaysPromptInteraction one;
  one.Configure(Policy(), 1);
  Check(one.HitNormalized(0.0f, 0.0f) == 0,
        "one-choice CMAP must include canvas origin");
  Check(one.HitNormalized(0.9999f, 0.9999f) == 0,
        "one-choice CMAP must cover the full canvas");
  Check(one.HitNormalized(1.0f, 0.5f) == -1 &&
            one.HitNormalized(-0.001f, 0.5f) == -1,
        "one-choice CMAP must reject points outside the canvas");

  SchoolDaysPromptInteraction lr;
  lr.Configure(Policy(SchoolDaysPromptOrientation::LeftRight), 2);
  Check(lr.HitNormalized(0.4999f, 0.5f) == 0,
        "left half must map to choice zero");
  Check(lr.HitNormalized(0.5f, 0.5f) == 1,
        "right half must own the exact half boundary");

  SchoolDaysPromptInteraction topBottom;
  topBottom.Configure(Policy(SchoolDaysPromptOrientation::TopBottom), 2);
  Check(topBottom.HitNormalized(0.5f, 0.4999f) == 0,
        "top half must map to choice zero");
  Check(topBottom.HitNormalized(0.5f, 0.5f) == 1,
        "bottom half must own the exact half boundary");

  SchoolDaysPromptInteraction unknown;
  unknown.Configure(Policy(), 2);
  Check(unknown.HitNormalized(0.25f, 0.5f) == -1 &&
            unknown.HitNormalized(0.75f, 0.5f) == -1,
        "unproved two-choice orientation must fail closed");

  int result = -2;
  lr.SeedPointer(0.25f, 0.5f, 10);
  Check(lr.StoredSelection() == 0, "seed must capture current pointer hit");
  Check(!lr.LeftClick(0.75f, 0.5f, 10, &result) && result == -2,
        "stale click must not activate a different hit region");
  lr.PointerMotion(0.75f, 0.5f, 10);
  Check(lr.StoredSelection() == 1,
        "mouse motion must update stored selection");
  Check(lr.LeftClick(0.75f, 0.5f, 10, &result) && result == 1,
        "left click must activate only hit==stored selection");

  result = -2;
  lr.ResetSelection();
  lr.SeedPointer(0.25f, 0.5f, 10);
  lr.PointerMotion(1.1f, 0.5f, 10);
  Check(lr.StoredSelection() == -1,
        "leaving the logical canvas must clear stored selection");
  Check(!lr.LeftClick(0.25f, 0.5f, 10, &result) && result == -2,
        "re-entry without motion must not accept a stale click");
  lr.PointerMotion(0.25f, 0.5f, 10);
  Check(lr.StoredSelection() == 0,
        "re-enter motion must restore the current selection");
  Check(lr.LeftClick(0.25f, 0.5f, 10, &result) && result == 0,
        "click after re-enter motion must activate the stored hit");

  SchoolDaysPromptInteraction timeout;
  timeout.Configure(Policy(SchoolDaysPromptOrientation::LeftRight), 2);
  result = -2;
  Check(!timeout.InputOpenAtTick(9), "input must stay closed before start tick");
  Check(!timeout.VisibleAtTick(9) && timeout.VisibleAtTick(10) &&
            timeout.VisibleAtTick(19) && !timeout.VisibleAtTick(20),
        "choice visual tick interval must be exact [start,end)");
  Check(timeout.InputOpenAtTick(18),
        "input must remain open while endTick > currentTick + 1");
  Check(!timeout.AdvanceToTick(18, &result) && result == -2,
        "pending sentinel must survive the last open input tick");
  Check(timeout.AdvanceToTick(19, &result) && result == -1,
        "endTick == currentTick + 1 must publish native timeout sentinel -1");

  FixedRandom autoRandom{2u, 0};
  SchoolDaysPromptInteraction autoTimeout;
  autoTimeout.Configure(Policy(SchoolDaysPromptOrientation::LeftRight), 2);
  autoTimeout.SetAutoChoiceRandomSource(&NextFixedRandom, &autoRandom);
  result = -2;
  Check(!autoTimeout.AdvanceToTick(18, &result) && result == -2,
        "AUTO test must remain pending before timeout boundary");
  autoTimeout.SetAutoMode(true);
  Check(autoTimeout.AdvanceToTick(19, &result) && result == 1 &&
            autoRandom.calls == 1,
        "AUTO toggle on with an open prompt must use rand%(span+1)-1");

  FixedRandom preconfiguredRandom{1u, 0};
  SchoolDaysPromptInteraction preconfiguredAuto;
  preconfiguredAuto.SetAutoMode(true);
  preconfiguredAuto.SetAutoChoiceRandomSource(&NextFixedRandom,
                                               &preconfiguredRandom);
  preconfiguredAuto.Configure(Policy(), 1);
  result = -2;
  Check(preconfiguredAuto.AutoMode() &&
            preconfiguredAuto.AdvanceToTick(19, &result) && result == 0 &&
            preconfiguredRandom.calls == 1,
        "AUTO state set before prompt policy configure must survive configure");

  FixedRandom disabledRandom{1u, 0};
  SchoolDaysPromptInteraction autoDisabled;
  autoDisabled.Configure(Policy(SchoolDaysPromptOrientation::LeftRight), 2);
  autoDisabled.SetAutoChoiceRandomSource(&NextFixedRandom, &disabledRandom);
  autoDisabled.SetAutoMode(true);
  autoDisabled.SetAutoMode(false);
  result = -2;
  Check(autoDisabled.AdvanceToTick(19, &result) && result == -1 &&
            disabledRandom.calls == 0,
        "AUTO off at timeout must publish -1 without consuming RNG");

  FixedRandom oneChoiceRandom{1u, 0};
  SchoolDaysPromptInteraction oneChoiceAuto;
  oneChoiceAuto.Configure(Policy(), 1);
  oneChoiceAuto.SetAutoChoiceRandomSource(&NextFixedRandom, &oneChoiceRandom);
  oneChoiceAuto.SetAutoMode(true);
  result = -2;
  Check(oneChoiceAuto.AdvanceToTick(19, &result) && result == 0 &&
            oneChoiceRandom.calls == 1,
        "one-choice AUTO span must resolve rand%2-1 exactly");

  Kotonoha::GameplayPauseIntent pauseIntent;
  Check(!pauseIntent.Loading() && !pauseIntent.UserPaused() &&
            !pauseIntent.EffectivePaused(),
        "user pause intent must initialize resumed");
  pauseIntent.BeginLoading(false);
  Check(pauseIntent.Loading() && !pauseIntent.UserPaused() &&
            pauseIntent.EffectivePaused(),
        "loading must force effective pause without changing user intent");

  Kotonoha::GameplayPauseIntent nextSceneIntent;
  nextSceneIntent.SetUserPaused(pauseIntent.UserPaused());
  Check(!nextSceneIntent.UserPaused() && !nextSceneIntent.EffectivePaused(),
        "scene handoff must transfer user intent, not loading-forced pause");

  pauseIntent.SetUserPaused(true);
  pauseIntent.EndLoading();
  Check(!pauseIntent.Loading() && pauseIntent.UserPaused() &&
            pauseIntent.EffectivePaused(),
        "pause requested during loading must survive loading completion");

  Kotonoha::GameplayPauseIntent resumeIntent;
  resumeIntent.BeginLoading(true);
  resumeIntent.SetUserPaused(false);
  Check(resumeIntent.Loading() && !resumeIntent.UserPaused() &&
            resumeIntent.EffectivePaused(),
        "resume requested during loading must keep loading pause effective");
  resumeIntent.BeginLoading(true);
  Check(!resumeIntent.UserPaused(),
        "repeated loading notification must not overwrite changed user intent");
  resumeIntent.EndLoading();
  Check(!resumeIntent.EffectivePaused(),
        "resume intent must take effect when loading pause releases");

  Check(SchoolDaysPromptInteraction::AcceptsEventType(SDL_EVENT_MOUSE_MOTION),
        "School Days policy must accept mouse motion");
  Check(SchoolDaysPromptInteraction::AcceptsEventType(
            SDL_EVENT_MOUSE_BUTTON_DOWN),
        "School Days policy must accept mouse button down");
  Check(!SchoolDaysPromptInteraction::AcceptsEventType(SDL_EVENT_FINGER_DOWN),
        "finger input must be ignored in School Days mode");
  Check(!SchoolDaysPromptInteraction::AcceptsEventType(SDL_EVENT_KEY_DOWN),
        "keyboard input must be ignored in School Days mode");
  Check(!SchoolDaysPromptInteraction::AcceptsEventType(
            SDL_EVENT_GAMEPAD_BUTTON_DOWN),
        "gamepad input must be ignored in School Days mode");

  Check(Kotonoha::SchoolDaysInputPolicy::AcceptsGameplayEventType(
            SDL_EVENT_MOUSE_MOTION) &&
            Kotonoha::SchoolDaysInputPolicy::AcceptsGameplayEventType(
                SDL_EVENT_MOUSE_BUTTON_DOWN) &&
            Kotonoha::SchoolDaysInputPolicy::AcceptsGameplayEventType(
                SDL_EVENT_WINDOW_RESIZED) &&
            !Kotonoha::SchoolDaysInputPolicy::AcceptsGameplayEventType(
                SDL_EVENT_KEY_DOWN) &&
            !Kotonoha::SchoolDaysInputPolicy::AcceptsGameplayEventType(
                SDL_EVENT_GAMEPAD_BUTTON_DOWN) &&
            !Kotonoha::SchoolDaysInputPolicy::AcceptsGameplayEventType(
                SDL_EVENT_GAMEPAD_AXIS_MOTION) &&
            !Kotonoha::SchoolDaysInputPolicy::AcceptsGameplayEventType(
                SDL_EVENT_FINGER_DOWN),
        "gameplay boundary accepted unauthenticated physical input");

  Check(SchoolDaysPromptInteraction::NormalArgb == 0xFFF0F0F0u,
        "native normal choice color must be exact");
  Check(SchoolDaysPromptInteraction::SelectedArgb == 0xFFFE4A1Fu,
        "native selected choice color must be exact");

  std::cout << "SCHOOL DAYS PROMPT POLICY PASS\n"
            << "one_choice_full_canvas=PASS\n"
            << "two_choice_left_right=PASS\n"
            << "two_choice_top_bottom=PASS\n"
            << "unknown_orientation_fail_closed=PASS\n"
            << "stored_selection_and_stale_click=PASS\n"
            << "leave_reenter=PASS\n"
            << "native_timeout_boundary=PASS\n"
            << "auto_timeout_runtime_toggle=PASS\n"
            << "auto_timeout_rng_formula=PASS\n"
            << "loading_user_pause_intent=PASS\n"
            << "mouse_only=PASS\n"
            << "gameplay_input_boundary_mouse_window_only=PASS\n"
            << "exact_colors=PASS\n"
            << "generic_opt_in_regression=PASS\n";
  return 0;
}
