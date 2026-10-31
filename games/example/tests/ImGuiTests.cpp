#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void testDensity(Kotonoha::Engine &engine, float density) {
  auto &renderer = engine.GetRenderer();
  auto &debug = engine.GetDebug();
  const auto size = engine.screenSize();
  const int width = static_cast<int>(std::lround(size.x * density));
  const int height = static_cast<int>(std::lround(size.y * density));
  Kotonoha::TexturePtr target{
      SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA8888,
                        SDL_TEXTUREACCESS_TARGET, width, height)};
  require(static_cast<bool>(target), "Create simulated HiDPI framebuffer");
  require(SDL_SetRenderTarget(&renderer, target.get()),
          "Bind HiDPI framebuffer");
  auto frame = [&] {
    require(SDL_SetRenderLogicalPresentation(
                &renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX),
            "Start from the novel presentation");
    require(SDL_SetRenderScale(&renderer, 1, 1), "Reset novel scale");
    require(SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255) &&
                SDL_RenderClear(&renderer),
            "Clear test framebuffer");
    debug.renderMenu(renderer);
  };
  frame();
  frame();
  const auto *data = ImGui::GetDrawData();
  require(data && data->CmdListsCount > 0, "Inspector generates geometry");
  require(std::abs(data->FramebufferScale.x - density) < 0.001f &&
              std::abs(data->FramebufferScale.y - density) < 0.001f,
          "ImGui uses physical framebuffer density, not display content scale");
  float sx = 0, sy = 0;
  require(SDL_GetRenderScale(&renderer, &sx, &sy) &&
              std::abs(sx - density) < 0.001f &&
              std::abs(sy - density) < 0.001f,
          "Vertices and clipping use the same framebuffer scale");
  int logicalWidth = 0, logicalHeight = 0;
  SDL_RendererLogicalPresentation presentation;
  require(SDL_GetRenderLogicalPresentation(&renderer, &logicalWidth,
                                           &logicalHeight, &presentation) &&
              presentation == SDL_LOGICAL_PRESENTATION_DISABLED,
          "ImGui does not inherit novel letterboxing");

  auto *window = ImGui::FindWindowByName("Kotonoha Debug [F10]");
  require(window != nullptr, "Find rendered inspector");
  const float logicalRight = window->Pos.x + window->Size.x;
  const float logicalBottom = window->Pos.y + window->Size.y;
  Kotonoha::SurfacePtr image{SDL_RenderReadPixels(&renderer, nullptr)};
  require(static_cast<bool>(image), "Read back HiDPI inspector");
  int lastX = -1, lastY = -1;
  for (int y = 0; y < image->h; ++y)
    for (int x = 0; x < image->w; ++x) {
      Uint8 r = 0, g = 0, b = 0, a = 0;
      require(SDL_ReadSurfacePixel(image.get(), x, y, &r, &g, &b, &a),
              "Read inspector pixel");
      if (r || g || b) {
        lastX = std::max(lastX, x);
        lastY = std::max(lastY, y);
      }
    }
  require(std::abs(lastX + 1 -
                   std::min(logicalRight * density,
                            static_cast<float>(width))) <= 3 &&
              std::abs(lastY + 1 -
                       std::min(logicalBottom * density,
                                static_cast<float>(height))) <= 3,
          "Visible inspector bounds match logical bounds at 1x/1.5x/2x");
  // The FPS checkbox is the first item after the window title and padding.
  const auto &style = ImGui::GetStyle();
  const float x = window->Pos.x + style.WindowPadding.x + 5;
  const float y =
      window->Pos.y + window->TitleBarHeight + style.WindowPadding.y + 5;
  SDL_Event motion{};
  motion.type = SDL_EVENT_MOUSE_MOTION;
  motion.motion.windowID = SDL_GetWindowID(&engine.GetWindow());
  motion.motion.x = x;
  motion.motion.y = y;
  debug.handleEvent(motion);
  frame();
  const bool before = debug.getSettings().showFPS;
  SDL_Event button{};
  button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  button.button.windowID = motion.motion.windowID;
  button.button.button = SDL_BUTTON_LEFT;
  button.button.x = x;
  button.button.y = y;
  debug.handleEvent(button);
  frame();
  button.type = SDL_EVENT_MOUSE_BUTTON_UP;
  debug.handleEvent(button);
  frame();
  require(debug.getSettings().showFPS != before,
          "Window-coordinate click activates the visually aligned checkbox");
  require(SDL_SetRenderTarget(&renderer, nullptr),
          "Restore window framebuffer");
}
} // namespace

int main() {
  try {
    Kotonoha::Runtime runtime("ImGui tests", "1", "org.kotonoha.imgui.tests");
    Kotonoha::Engine engine(runtime);
    engine.GetDebug().setVisible(true);
    for (float density : {1.0f, 1.5f, 2.0f, 1.0f})
      testDensity(engine, density);
    require(SDL_SetWindowSize(&engine.GetWindow(), 900, 600), "Resize window");
    SDL_PumpEvents();
    testDensity(engine, 2.0f);
    std::cout << "ImGui HiDPI geometry, clipping and mouse tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
