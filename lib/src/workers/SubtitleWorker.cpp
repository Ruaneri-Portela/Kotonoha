#include <Kotonoha/workers/SubtitleWorker.hpp>

#include <SDL3/SDL.h>
#include <ass/ass.h>

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Kotonoha {
namespace workers {
namespace {

// The only place that calls libass rasterization. Everything it returns is
// owned memory; ASS_Image pointers expire when this function returns.
class AssRasterBackend final : public RasterBackend {
public:
  AssRasterBackend(ASS_Renderer *renderer, ASS_Track *track)
      : renderer_(renderer), track_(track) {}

  void setTrack(ASS_Track *track) override { track_ = track; }

  RasterResult rasterize(const RasterRequest &request) override {
    RasterResult result;
    if (!renderer_ || !track_)
      return result;
    int changed = 0;
    ASS_Image *images =
        ass_render_frame(renderer_, track_, request.timeMs, &changed);
    result.changed = changed;
    size_t totalPixels = 0;
    for (ASS_Image *image = images; image; image = image->next) {
      if (!image->bitmap || image->w <= 0 || image->h <= 0 || image->w > 8192 ||
          image->h > 8192 || image->stride < image->w)
        continue;
      const size_t pixels = size_t(image->w) * size_t(image->h);
      if (pixels > 32ull * 1024 * 1024 ||
          totalPixels > 64ull * 1024 * 1024 - pixels ||
          result.images.size() >= 4096)
        break;
      totalPixels += pixels;
      OwnedImage owned;
      owned.width = image->w;
      owned.height = image->h;
      owned.color = image->color;
      owned.coverage.resize(pixels);
      for (int y = 0; y < image->h; ++y)
        std::memcpy(owned.coverage.data() + size_t(y) * image->w,
                    image->bitmap + size_t(y) * image->stride,
                    size_t(image->w));
      owned.destination = {
          image->dst_x * request.scaleX, image->dst_y * request.scaleY,
          image->w * request.scaleX, image->h * request.scaleY};
      result.images.push_back(std::move(owned));
    }
    return result;
  }

private:
  ASS_Renderer *renderer_;
  ASS_Track *track_;
};

} // namespace

std::unique_ptr<RasterBackend> makeAssRasterBackend(ASS_Renderer *renderer,
                                                    ASS_Track *track) {
  return std::unique_ptr<RasterBackend>(new AssRasterBackend(renderer, track));
}

struct SubtitleWorker::Impl {
  std::unique_ptr<RasterBackend> backend;
  FrameSink *sink;
  SDL_Thread *thread = nullptr;
  SDL_Mutex *mutex = nullptr;
  SDL_Condition *work = nullptr;
  RasterRequest request;
  RasterResult result;
  bool pending = false;
  bool delivered = false;
  bool stopping = false;
  std::string error;

  Impl(std::unique_ptr<RasterBackend> rasterBackend, FrameSink &frameSink)
      : backend(std::move(rasterBackend)), sink(&frameSink) {
    mutex = SDL_CreateMutex();
    work = SDL_CreateCondition();
    if (!mutex || !work)
      throw std::runtime_error(SDL_GetError());
  }

  ~Impl() {
    SDL_DestroyCondition(work);
    SDL_DestroyMutex(mutex);
  }

  static int SDLCALL entry(void *data) {
    static_cast<Impl *>(data)->loop();
    return 0;
  }

  // The one frame handler. Runs on the main thread via SDL_RunOnMainThread in
  // the threaded path, and directly on the main thread on Emscripten.
  static void SDLCALL deliver(void *data) {
    auto *self = static_cast<Impl *>(data);
    SDL_LockMutex(self->mutex);
    std::string failure;
    try {
      if (self->result.error.empty())
        self->sink->present(self->result);
      else
        failure = self->result.error;
    } catch (const std::exception &error) {
      failure = error.what();
    } catch (...) {
      failure = "Subtitle frame presentation failed";
    }
    self->error = std::move(failure);
    self->delivered = true;
    SDL_UnlockMutex(self->mutex);
  }

  void loop() {
    for (;;) {
      SDL_LockMutex(mutex);
      while (!pending && !stopping)
        SDL_WaitCondition(work, mutex);
      if (stopping && !pending) {
        SDL_UnlockMutex(mutex);
        break;
      }
      RasterRequest frame = request;
      pending = false;
      delivered = false;
      error.clear();
      SDL_UnlockMutex(mutex);

      RasterResult next;
      try {
        next = backend->rasterize(frame);
      } catch (const std::exception &rasterError) {
        next.error = rasterError.what();
      } catch (...) {
        next.error = "libass rasterization failed";
      }

      SDL_LockMutex(mutex);
      result = std::move(next);
      SDL_UnlockMutex(mutex);

      // wait_complete keeps the owned frame alive on this stack until the
      // main thread has uploaded and composited it.
      if (!SDL_RunOnMainThread(&Impl::deliver, this, true)) {
        SDL_LockMutex(mutex);
        if (error.empty())
          error = SDL_GetError();
        delivered = true;
        SDL_UnlockMutex(mutex);
      }
    }
  }
};

SubtitleWorker::SubtitleWorker(std::unique_ptr<RasterBackend> backend,
                               FrameSink &sink)
    : impl_(std::make_unique<Impl>(std::move(backend), sink)) {
#if !defined(__EMSCRIPTEN__)
  SDL_Thread *worker =
      SDL_CreateThread(&Impl::entry, "KotonohaASS", impl_.get());
  if (!worker)
    throw std::runtime_error(SDL_GetError());
  impl_->thread = worker;
#endif
}

SubtitleWorker::~SubtitleWorker() {
#if !defined(__EMSCRIPTEN__)
  if (!impl_->thread)
    return;
  SDL_LockMutex(impl_->mutex);
  impl_->stopping = true;
  SDL_BroadcastCondition(impl_->work);
  SDL_UnlockMutex(impl_->mutex);
  SDL_WaitThread(impl_->thread, nullptr);
  impl_->thread = nullptr;
#endif
}

bool SubtitleWorker::hasWorkerThread() const {
#if defined(__EMSCRIPTEN__)
  return false;
#else
  return impl_->thread != nullptr;
#endif
}

void SubtitleWorker::setTrack(ASS_Track *track) {
  impl_->backend->setTrack(track);
}

void SubtitleWorker::renderFrame(const RasterRequest &request) {
#if defined(__EMSCRIPTEN__)
  renderFrameSynchronous(request);
#else
  Impl &self = *impl_;
  SDL_LockMutex(self.mutex);
  self.request = request;
  self.pending = true;
  self.delivered = false;
  self.error.clear();
  SDL_SignalCondition(self.work);
  SDL_UnlockMutex(self.mutex);

  // SDL drains the SDL_RunOnMainThread queue from SDL_PumpEvents(). Keep
  // pumping until the delivery callback has uploaded/blitted this frame.
  for (;;) {
    SDL_LockMutex(self.mutex);
    const bool done = self.delivered;
    SDL_UnlockMutex(self.mutex);
    if (done)
      break;
    SDL_PumpEvents();
  }
  SDL_LockMutex(self.mutex);
  std::string failure = std::move(self.error);
  SDL_UnlockMutex(self.mutex);
  if (!failure.empty())
    throw std::runtime_error(failure);
#endif
}

void SubtitleWorker::renderFrameSynchronous(const RasterRequest &request) {
  Impl &self = *impl_;
  RasterResult next;
  try {
    next = self.backend->rasterize(request);
  } catch (const std::exception &rasterError) {
    next.error = rasterError.what();
  } catch (...) {
    next.error = "libass rasterization failed";
  }
  SDL_LockMutex(self.mutex);
  self.result = std::move(next);
  self.delivered = false;
  self.error.clear();
  SDL_UnlockMutex(self.mutex);

  Impl::deliver(&self);
  if (!self.error.empty())
    throw std::runtime_error(self.error);
}

} // namespace workers
} // namespace Kotonoha
