#pragma once

#include <SDL3/SDL.h>

namespace Kotonoha::Debug {

// Captures every SDL_Log* message into a bounded in-memory ring buffer while
// still forwarding it to the previously installed SDL log output.
// Installing twice is a no-op.
void InstallLogCapture();

// Restores the original SDL log output function.
void UninstallLogCapture();

// Draws the "Log" window. Must be called between ImGui::NewFrame() and
// ImGui::Render(). Does nothing when `open` is null or false.
void DrawLogPanel(bool *open);

} // namespace Kotonoha::Debug
