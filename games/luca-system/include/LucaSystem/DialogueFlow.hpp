// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

namespace LucaSystem {
// Scripted dialogue advances in two stages: reveal pending glyphs, then
// execute the next VM instruction. Never synthesize a second message from
// the list of all extracted texts; that skips intervening game commands.
enum class DialogueAdvance {
  None,
  Reveal,
  ResumeScript
};
inline DialogueAdvance nextDialogueAction(bool visible, bool complete) {
  if (!visible) return DialogueAdvance::None;
  return complete ? DialogueAdvance::ResumeScript
                  : DialogueAdvance::Reveal;
}
} // namespace LucaSystem
