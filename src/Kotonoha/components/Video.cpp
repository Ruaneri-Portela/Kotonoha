#include <Kotonoha/components/Video.hpp>

namespace Kotonoha {

static void ShutdownVideoDecoder(Kotonoha_videoData **video) {
  if (video == nullptr || *video == nullptr) {
    return;
  }
  KotonohaUpscalingReleaseTexture(&(*video)->upscaled);
  KotonohaVideoRenderShutdown(video);
}

Video::Video(Kotonoha_time *timeManager) : timeManager(timeManager) {
  lock = SDL_CreateMutex();
  if (lock == nullptr)
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to create video mutex: %s", SDL_GetError());
}

Video::Video(Kotonoha_time *timeManager, Kotonoha_Demuxer *demuxer)
    : Video(timeManager) {
  this->demuxer = demuxer;
}

Kotonoha_videoData *Video::CreateDecode(const Kotonoha_orsEvent *event,
                                        const char *path) {
  if (event == nullptr)
    return nullptr;

  const Uint64 startMs =
      Kotonoha_SceneTickToMillisecondsCeil(event->startTick);
  const Uint64 endMs = Kotonoha_SceneTickToMillisecondsCeil(event->endTick);

  if (demuxer != nullptr) {
    return KotonohaVideoRenderInit(path, timeManager, startMs, endMs,
                                   event->endTick, demuxer);
  }
  return Kotonoha_VideoRenderInit(path, timeManager, startMs, endMs,
                                  event->endTick);
}

static SDL_Texture *GetDisplayTexture(SDL_Renderer *renderer,
                                      Kotonoha_videoData *video,
                                      bool *textureChanged) {
  if (textureChanged != nullptr) {
    *textureChanged = false;
  }
  if (video == nullptr || video->texture == nullptr) {
    return nullptr;
  }

  const Uint64 revision = KotonohaUpscalingGetRevision();
  bool hasUpscaledTexture = false;
  if (!video->upscaled.initialized ||
      video->upscaled.sourceSerial != video->textureSerial ||
      video->upscaled.settingsRevision != revision) {
    hasUpscaledTexture = KotonohaUpscalingUpdateTexture(
        renderer, video->texture, video->textureSerial, &video->upscaled,
        textureChanged);
  } else {
    hasUpscaledTexture = video->upscaled.output != nullptr;
  }

  return hasUpscaledTexture ? video->upscaled.output : video->texture;
}

bool Video::Prepare(const Kotonoha_orsEvent *event, const char *path) {
  if (timeManager == nullptr || event == nullptr || path == nullptr ||
      *path == '\0' || lock == nullptr)
    return false;

  SDL_LockMutex(lock);
  for (const auto &item : videos) {
    if (item.event == event) {
      const bool ready = item.decoder != nullptr;
      SDL_UnlockMutex(lock);
      return ready;
    }
  }
  SDL_UnlockMutex(lock);

  Kotonoha_videoData *decode = CreateDecode(event, path);
  if (decode == nullptr)
    return false;

  /* Decode the opening frame while the item is still invisible.  The shared
   * demuxer has already opened the stream and can usually provide the first
   * packet immediately; a bounded wait keeps PREPARE from turning into a
   * startup stall when storage is slow. */
  KotonohaVideoRenderPrime(decode, 50);

  SDL_LockMutex(lock);
  videos.push_back({event, decode, false});
  SDL_UnlockMutex(lock);
  return true;
}

bool Video::Activate(const Kotonoha_orsEvent *event) {
  if (event == nullptr || lock == nullptr)
    return false;

  SDL_LockMutex(lock);
  for (auto &item : videos) {
    if (item.event == event) {
      item.active = item.decoder != nullptr;
      const bool active = item.active;
      SDL_UnlockMutex(lock);
      return active;
    }
  }
  SDL_UnlockMutex(lock);
  return false;
}

bool Video::IsEof(const Kotonoha_orsEvent *event) {
  if (event == nullptr || lock == nullptr)
    return false;

  SDL_LockMutex(lock);
  for (const auto &item : videos) {
    if (item.event == event) {
      const bool eof = item.decoder != nullptr && item.decoder->decoderEof;
      SDL_UnlockMutex(lock);
      return eof;
    }
  }
  SDL_UnlockMutex(lock);
  return false;
}

void Video::Remove(const Kotonoha_orsEvent *event) {
  if (event == nullptr || lock == nullptr)
    return;

  Kotonoha_videoData *removed = nullptr;
  SDL_LockMutex(lock);
  for (auto it = videos.begin(); it != videos.end(); ++it) {
    if (it->event == event) {
      removed = it->decoder;
      videos.erase(it);
      break;
    }
  }
  SDL_UnlockMutex(lock);

  ShutdownVideoDecoder(&removed);
}

Kotonoha_Scene_Status Video::RenderCallback(KOTONOHA_SCENE_CALL) {
  (void)window;
  (void)target;

  auto *here = static_cast<Video *>(userData);
  if (here == nullptr || here->lock == nullptr)
    return KOTONOHA_SCENE_COMPLETE;

  // SDL textures are renderer-thread resources.  Retired decoders may be
  // discovered by the events worker, but their texture/upscaler teardown must
  // happen here on the render thread (SDL_DestroyTexture is main-thread only).
  here->CollectRetired();

  Kotonoha_Scene_Status returnStatus = KOTONOHA_SCENE_NULL;

  /* Removal/reset also take this lock, so a decoder cannot be freed while the
   * render thread is consuming its queue. Queue access itself is nonblocking. */
  SDL_LockMutex(here->lock);
  for (auto it = here->videos.begin(); it != here->videos.end();) {
    Kotonoha_videoData *video = it->decoder;
    if (video == nullptr) {
      it = here->videos.erase(it);
      continue;
    }

    if (!it->active) {
      // PREPARE is intentionally visually silent.  Do not spend GPU time
      // upscaling a cached opening texture before the authored START; the first
      // active frame will populate the upscale cache on demand.
      ++it; /* PREPARE stays physically cached but visually silent. */
      continue;
    }

    const Uint64 textureSerialBefore = video->textureSerial;
    const Kotonoha_Scene_Status status =
        KotonohaVideoRenderProcess(video, render);
    bool upscaledTextureChanged = false;
    SDL_Texture *displayTexture =
        GetDisplayTexture(render, video, &upscaledTextureChanged);
    switch (status) {
    case KOTONOHA_SCENE_DRAW:
      // VideoRender may report DRAW simply because a previously uploaded
      // texture is still valid (EOF, future-frame wait, short starvation).
      // When no decoder frame and no upscaler output changed, let Canvas reuse
      // its retained dirtyFrame instead of redrawing and snapshotting the same
      // pixels every display vsync.
      if (!video->decoderEof && video->textureSerial == textureSerialBefore &&
          !upscaledTextureChanged) {
        if (returnStatus != KOTONOHA_SCENE_DRAW &&
            returnStatus != KOTONOHA_SCENE_DRAW_LAST)
          returnStatus = KOTONOHA_SCENE_WAITING;
        ++it;
        break;
      }
      if (displayTexture != nullptr)
        SDL_RenderTexture(render, displayTexture, nullptr, nullptr);
      returnStatus = KOTONOHA_SCENE_DRAW;
      ++it;
      break;

    case KOTONOHA_SCENE_COMPLETE:
      /* Present the retained terminal frame once at ORS END; Canvas then owns
       * the DRAW_LAST composited texture. Decoder teardown is queued and
       * collected at the beginning of a later render-thread callback so GPU
       * resources are never destroyed by the events worker. */
      if (returnStatus != KOTONOHA_SCENE_DRAW && displayTexture != nullptr) {
        SDL_RenderTexture(render, displayTexture, nullptr, nullptr);
        returnStatus = KOTONOHA_SCENE_DRAW_LAST;
      }
      here->retired.push_back(video);
      it = here->videos.erase(it);
      break;

    case KOTONOHA_SCENE_WAITING:
      if (upscaledTextureChanged && displayTexture != nullptr) {
        SDL_RenderTexture(render, displayTexture, nullptr, nullptr);
        returnStatus = KOTONOHA_SCENE_DRAW;
        ++it;
        break;
      }
      if (returnStatus != KOTONOHA_SCENE_DRAW &&
          returnStatus != KOTONOHA_SCENE_DRAW_LAST)
        returnStatus = KOTONOHA_SCENE_WAITING;
      ++it;
      break;

    case KOTONOHA_SCENE_FATAL_ERROR:
      // Fatal is terminal for the entire video layer.  Continuing the loop
      // lets a later decoder overwrite returnStatus with DRAW/WAITING and hides
      // the failure from Canvas, leaving the scene running on corrupt state.
      SDL_UnlockMutex(here->lock);
      return KOTONOHA_SCENE_FATAL_ERROR;

    default:
      ++it;
      break;
    }
  }

  const bool empty = here->videos.empty();
  bool hasActive = false;
  bool hasDueInactive = false;
  const Kotonoha_SceneTick currentTick =
      here->timeManager != nullptr
          ? Kotonoha_MillisecondsToSceneTick(KotonohaTimeGet(here->timeManager))
          : 0;
  for (const auto &item : here->videos) {
    if (item.active) {
      hasActive = true;
      continue;
    }
    if (item.event != nullptr && item.event->startTick <= currentTick &&
        currentTick < item.event->endTick) {
      /*
       * The authored next movie is already due but the event worker has not
       * activated it yet.  Preserve the previous terminal frame while that
       * same-tick handoff catches up; once the pending movie becomes active,
       * its normal WAITING/DRAW result owns retention.
       */
      hasDueInactive = true;
    }
  }
  SDL_UnlockMutex(here->lock);

  if (empty && returnStatus != KOTONOHA_SCENE_DRAW_LAST &&
      returnStatus != KOTONOHA_SCENE_FATAL_ERROR)
    return KOTONOHA_SCENE_COMPLETE;
  if (!hasActive && returnStatus == KOTONOHA_SCENE_NULL) {
    /*
     * PREPARE intentionally keeps future decoders in `videos`.  Their mere
     * presence must not keep Canvas' DRAW_LAST frame alive across an authored
     * gap (for example WMV -> CreateBG -> later WMV).  That stale video layer
     * otherwise covers the image layer until the future movie starts, making
     * the scene look frozen while audio/text continue.
     *
     * Only a movie whose authored window is already due may bridge the old
     * terminal frame while activation catches up.
     */
    return hasDueInactive ? KOTONOHA_SCENE_WAITING
                          : KOTONOHA_SCENE_COMPLETE;
  }
  return returnStatus;
}

void Video::CollectRetired() {
  std::vector<Kotonoha_videoData *> pending;
  if (lock != nullptr)
    SDL_LockMutex(lock);
  pending.swap(retired);
  if (lock != nullptr)
    SDL_UnlockMutex(lock);

  for (auto *video : pending)
    ShutdownVideoDecoder(&video);
}

void Video::Reset() {
  std::vector<Item> old;
  std::vector<Kotonoha_videoData *> oldRetired;
  if (lock != nullptr)
    SDL_LockMutex(lock);
  old.swap(videos);
  oldRetired.swap(retired);
  if (lock != nullptr)
    SDL_UnlockMutex(lock);

  for (auto &item : old)
    ShutdownVideoDecoder(&item.decoder);
  for (auto *video : oldRetired)
    ShutdownVideoDecoder(&video);
}

std::vector<MediaBufferInfo> Video::GetBufferInfo() const {
  std::vector<MediaBufferInfo> result;
  if (lock == nullptr) return result;
  SDL_LockMutex(lock);
  const auto append = [&result](const Kotonoha_videoData *video) {
    if (video == nullptr) return;
    MediaBufferInfo info;
    info.id = reinterpret_cast<Uint64>(video);
    info.type = "Video";
    if (video->stream != nullptr && video->stream->filename != nullptr)
      info.path = video->stream->filename;
    info.playedMs = video->videoTime;
    info.texture = video->upscaled.output != nullptr
                       ? video->upscaled.output
                       : video->texture;
    if (video->stream != nullptr) {
      info.totalMs = static_cast<Uint64>(SDL_GetAtomicInt(
          const_cast<SDL_AtomicInt *>(&video->stream->totalDurationMs)));
      KotonohaPacketQueueGetBufferStats(&video->stream->videoQueue,
                                        &info.cachedMs, &info.eof);
    }
    result.push_back(std::move(info));
  };
  for (const auto &item : videos) append(item.decoder);
  for (const auto *video : retired) append(video);
  SDL_UnlockMutex(lock);
  return result;
}

Video::~Video() {
  Reset();
  if (lock != nullptr) {
    SDL_DestroyMutex(lock);
    lock = nullptr;
  }
}

} // namespace Kotonoha
