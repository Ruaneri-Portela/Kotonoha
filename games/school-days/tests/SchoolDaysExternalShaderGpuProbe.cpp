// Opt-in real-GPU smoke for external MPV/GLSL files. Not a default CTest:
// a graphical OpenGL driver is required. Uses only synthetic pixels and does
// not touch the commercial School Days install or the player's save profile.
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(bool value, const char *what) {
  if (!value) throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
}

std::uint64_t Fingerprint(SDL_Renderer *renderer, SDL_Texture *texture) {
  SDL_Texture *old = SDL_GetRenderTarget(renderer);
  Require(SDL_SetRenderTarget(renderer, texture), "Set probe render target");
  Kotonoha::SurfacePtr pixels(SDL_RenderReadPixels(renderer, nullptr));
  (void)SDL_SetRenderTarget(renderer, old);
  Require(static_cast<bool>(pixels), "Read probe pixels");
  const auto *bytes = static_cast<const unsigned char *>(pixels->pixels);
  std::uint64_t hash = 14695981039346656037ull;
  for (int y = 0; y < pixels->h; ++y)
    for (int x = 0; x < pixels->pitch; ++x) {
      hash ^= bytes[y * pixels->pitch + x];
      hash *= 1099511628211ull;
    }
  return hash;
}

int Run(const char *shaderPath, float factor) {
  Require(std::filesystem::is_regular_file(shaderPath), "Input shader must be a file");
  Require(std::isfinite(factor) && factor >= 1.5f && factor <= 4.0f,
          "Invalid scale factor");
  Require(SDL_Init(SDL_INIT_VIDEO), "Init SDL video");
  try {
    Kotonoha::WindowPtr window(SDL_CreateWindow(
        "Kotonoha external shader probe", 240, 180,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN));
    Require(static_cast<bool>(window), "Create hidden window");
    Kotonoha::RendererPtr renderer(SDL_CreateRenderer(window.get(), "opengl"));
    Require(static_cast<bool>(renderer), "Create OpenGL renderer");
    Require(std::string(SDL_GetRendererName(renderer.get())) == "opengl",
            "Renderer must be OpenGL");
    Kotonoha::Upscaling upscaler(*renderer);
    Require(upscaler.supports(Kotonoha::UpscaleBackend::MPV),
            "OpenGL MPV pipeline unavailable");

    constexpr int w = 48, h = 40;
    std::vector<unsigned char> input(w * h * 4);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const int i = 4 * (y * w + x);
        const int checker = ((x / 3) + (y / 3)) & 1;
        input[i] = static_cast<unsigned char>((x * 5 + checker * 77) % 256);
        input[i + 1] = static_cast<unsigned char>((y * 7 + checker * 112) % 256);
        input[i + 2] = static_cast<unsigned char>(((x + y) * 3 + checker * 44) % 256);
        input[i + 3] = 255;
      }
    Kotonoha::TexturePtr source(SDL_CreateTexture(
        renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h));
    Require(static_cast<bool>(source), "Create probe source texture");
    Require(SDL_UpdateTexture(source.get(), nullptr, input.data(), 4 * w),
            "Upload synthetic input pixels");

    Kotonoha::UpscaleSettings custom;
    custom.backend = Kotonoha::UpscaleBackend::MPV;
    custom.preset = Kotonoha::UpscalePreset::ArtCNN;
    custom.factor = factor;
    custom.shaderFiles = shaderPath;
    upscaler.configure(custom);
    const auto shaderOutput = upscaler.scaled(*source, factor);
    Require(static_cast<bool>(shaderOutput), "Run custom shader upscale");
    const auto customHash = Fingerprint(renderer.get(), shaderOutput.get());

    custom.backend = Kotonoha::UpscaleBackend::Linear;
    custom.shaderFiles.clear();
    upscaler.configure(custom);
    const auto linearOutput = upscaler.scaled(*source, factor);
    Require(static_cast<bool>(linearOutput), "Run reference Linear upscale");
    const auto linearHash = Fingerprint(renderer.get(), linearOutput.get());
    std::cout << "SHADER_GPU_PROBE path=" << shaderPath
              << " factor=" << factor << " customHash=" << customHash
              << " linearHash=" << linearHash << '\n';
    Require(customHash != linearHash,
            "Shader output equals Linear (possible fallback or no effect)");
  } catch (...) {
    SDL_Quit();
    throw;
  }
  SDL_Quit();
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
  try {
    if (argc != 3)
      throw std::invalid_argument("Usage: SchoolDaysExternalShaderGpuProbe <full-file.glsl> <factor>");
    const int rc = Run(argv[1], std::stof(argv[2]));
    std::cout << "SHADER_GPU_PROBE_PASS\n";
    return rc;
  } catch (const std::exception &e) {
    std::cerr << "SHADER_GPU_PROBE_FAIL " << e.what() << '\n';
    return 1;
  }
}
