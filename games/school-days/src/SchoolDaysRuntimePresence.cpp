#include "SchoolDaysRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>

namespace SchoolDays {
std::string_view Runtime::presenceDetails() const {
  PresenceState state;
  state.scene = presenceScene;
  const bool transitioning =
      frontendHandoff != FrontendHandoff::None || frontendTransition.IsActive() ||
      loadTransition.Active() ||
      transportFade.Active() || exitTransition.Active() ||
      systemPanelTransition.InputLocked() || pendingSeek || pendingBackCapture ||
      pendingSceneAdvance || lxhFade.InputLocked();
  state.protectedState = transitioning || temporaryPlaybackActive() ||
      frontendChild == FrontendChild::Replay || systemUi.IsReplayOpen() ||
      replayHSceneTransitionActive || pendingReplayIndex >= 0 ||
      currentState == Kotonoha::GameState::Splash;
  state.menu = currentState == Kotonoha::GameState::Menu &&
               frontendChild != FrontendChild::Load;
  state.options = systemUi.IsConfigOpen();
  state.routeMap = systemUi.IsRouteMapOpen();
  state.choice = choice && !committed &&
                 !choicePresentation.exitActive &&
                 choiceInteraction.InputOpenAtTick(tick);
  state.finalWindow = currentState == Kotonoha::GameState::Gameplay &&
                      systemUi.IsSaveFeelOpen() &&
                      handoffUiState == HandoffUiState::SaveFeel &&
                      presenceEnding >= 0;
  state.endingCode = presenceEnding;
  if (!state.menu && currentState != Kotonoha::GameState::Gameplay)
    state.protectedState = true;
  if (!state.finalWindow && currentState == Kotonoha::GameState::Gameplay) {
    if (bridge.HasPendingHandoff() ||
        (engine && engine->GetVisualTransition().active()) ||
        lxhFirstTranslationEvent)
      state.protectedState = true;
  }
  return resolvePresence(state, lxhSubtitleLocale);
}
} // namespace SchoolDays
