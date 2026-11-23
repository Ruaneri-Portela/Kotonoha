#pragma once

#include <SDL3/SDL_rect.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Match libass' own opaque typedefs so including ass/ass.h later stays valid.
typedef struct ass_renderer ASS_Renderer;
typedef struct ass_track ASS_Track;

namespace Kotonoha {
namespace workers {

// Owned copy of one libass image. Coverage is packed row-major with exactly
// `width` bytes per row, so no ASS_Image pointer ever crosses a thread
// boundary. `destination` is already expressed in scene-logical coordinates.
struct OwnedImage {
  int width = 0;
  int height = 0;
  uint32_t color = 0;
  std::vector<Uint8> coverage;
  SDL_FRect destination{};
};

struct RasterRequest {
  long long timeMs = 0;
  // Scene-logical width divided by the libass frame width (and likewise for
  // height). Used to convert libass image positions into scene coordinates.
  float scaleX = 1.0f;
  float scaleY = 1.0f;
};

struct RasterResult {
  std::vector<OwnedImage> images;
  int changed = 0;
  std::string error;
};

// The only libass-aware component. Its implementation lives in the library so
// public headers stay free of libass includes. Implemented by AssRasterBackend.
class RasterBackend {
public:
  virtual ~RasterBackend() = default;
  virtual void setTrack(ASS_Track *track) = 0;
  virtual RasterResult rasterize(const RasterRequest &request) = 0;
};

std::unique_ptr<RasterBackend> makeAssRasterBackend(ASS_Renderer *renderer,
                                                    ASS_Track *track);

// Called on the SDL main thread through SDL_RunOnMainThread. Implementations
// upload textures and blit the finished frame; they never touch libass.
class FrameSink {
public:
  virtual ~FrameSink() = default;
  virtual void present(const RasterResult &result) = 0;
};

// Dedicated SDL worker for libass rasterization.
//
// Pacing: the worker is a job worker, not a free-running timer. It sleeps on a
// SDL_Condition and rasterizes exactly one frame per submitted RasterRequest.
// Kotonoha's frame cadence is Engine::Iterate() (SDL_AppIterate): each pass
// advances the engine clock and calls SubtitleScene::render() via
// Runtime::renderScene()/TimelinePlayer::renderSubtitles(). That call submits
// one request after the engine has advanced position/time for the frame, so
// subtitle work is tied to the same presentation tick. Repeated render()
// calls for an unchanged clock tick are coalesced by SubtitleScene.
//
// Backpressure: at most one job is in flight. renderFrame() blocks the calling
// main thread until SDL_RunOnMainThread has delivered the frame, so a slow
// rasterizer stalls the current presentation instead of accumulating a queue
// of stale frames.
//
// Emscripten: no SDL worker thread is created; renderFrameSynchronous() runs
// the same backend and the same frame handler on the main thread.
class SubtitleWorker {
public:
  SubtitleWorker(std::unique_ptr<RasterBackend> backend, FrameSink &sink);
  ~SubtitleWorker();

  SubtitleWorker(const SubtitleWorker &) = delete;
  SubtitleWorker &operator=(const SubtitleWorker &) = delete;

  // Safe only while no render job is in flight (called from the main thread).
  void setTrack(ASS_Track *track);

  // Submit a frame and pump SDL events until delivery has completed.
  void renderFrame(const RasterRequest &request);

  // Rasterize and deliver on the calling thread. Used on Emscripten and by
  // headless tests; the delivery handler is identical to the threaded path.
  void renderFrameSynchronous(const RasterRequest &request);

  bool hasWorkerThread() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace workers
} // namespace Kotonoha
