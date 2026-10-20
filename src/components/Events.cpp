#include <Kotonoha/Gameplay.hpp>
#include <Kotonoha/components/Events.hpp>
#include <Kotonoha/utils/Log.h>
#include <Kotonoha/utils/OrsTime.h>
#include <Kotonoha/utils/VoicePcm.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Kotonoha {

namespace {
Event *activeSubtitleTrackOwner = nullptr;
constexpr Uint64 kMissingAssetRetryIntervalMs = 1000;

static std::string BuildString(const char *str, const std::string &prefix = "",
                               const std::string &suffix = "") {
  if (str == nullptr) {
    return "";
  }

  std::string result = prefix + std::string(str) + suffix;

  size_t lastSlash = result.find_last_of('/');
  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    ++lastSlash;
  }

  if (result.size() - lastSlash >= 4 &&
      result.compare(lastSlash, 4, "UNC_") == 0) {
    result = result.substr(0, lastSlash) + result.substr(lastSlash + 4);
  }

  return result;
}

static std::string ToUpper(const std::string &str) {
  std::string upperStr;
  upperStr.reserve(str.size());

  for (unsigned char c : str) {
    upperStr += static_cast<char>(std::toupper(c));
  }

  return upperStr;
}

static bool FileExists(const std::string &path) {
  SDL_IOStream *file = SDL_IOFromFile(path.c_str(), "rb");
  if (file == nullptr) {
    return false;
  }

  SDL_CloseIO(file);
  return true;
}

static std::string ResolveAssetPath(const char *assetsPath,
                                    const std::string &logicalPath) {
  std::string current =
      assetsPath != nullptr && *assetsPath != '\0' ? assetsPath : ".";
  size_t position = 0;
  while (position < logicalPath.size()) {
    const size_t separator = logicalPath.find('/', position);
    const std::string component =
        logicalPath.substr(position, separator - position);
    struct Match {
      const std::string *wanted;
      std::string exact;
      std::string folded;
    } match{&component, "", ""};

    SDL_EnumerateDirectory(
        current.c_str(),
        [](void *opaque, const char *, const char *name) {
          auto *match = static_cast<Match *>(opaque);
          if (match->wanted->compare(name) == 0) {
            match->exact = name;
            return SDL_ENUM_SUCCESS;
          }
          if (match->folded.empty() &&
              SDL_strcasecmp(name, match->wanted->c_str()) == 0) {
            match->folded = name;
          }
          return SDL_ENUM_CONTINUE;
        },
        &match);

    const std::string &resolved = !match.exact.empty()    ? match.exact
                                  : !match.folded.empty() ? match.folded
                                                          : component;
    if (!current.empty() && current.back() != '/' && current.back() != '\\') {
      current += '/';
    }
    current += resolved;

    if (separator == std::string::npos) {
      break;
    }
    position = separator + 1;
  }
  return current;
}

static const char *EventName(int command) {
  static const char *const names[] = {
      "CreateBG", "PlaySe",    "PlayMovie", "WhiteFade", "BlackFade",
      "PlayBgm",  "PrintText", "PlayVoice", "SkipFRAME", "SetSELECT",
      "EndBgm",   "EndRoll",   "Next",      "Unknown"};
  return command >= 0 && command < 14 ? names[command] : "Unknown";
}

static void DestroyEventManagerParams(void **parms) {
  if (parms == nullptr) {
    return;
  }
  SDL_free(parms);
}
} // namespace

int Event::EventManager(void *data) {
  void **parms = static_cast<void **>(data);
  if (parms == nullptr) {
    return -1;
  }

  auto *gameplay = static_cast<Gameplay *>(parms[0]);
  auto *gameCtx = static_cast<struct Kotonoha_Game *>(parms[1]);
  auto *classUp = static_cast<Event *>(parms[2]);

  if (gameplay == nullptr || gameCtx == nullptr || classUp == nullptr ||
      classUp->eventMutex == nullptr) {
    DestroyEventManagerParams(parms);
    return -1;
  }

  const Uint64 kMediaPrefetchHorizonMs =
      static_cast<Uint64>(gameCtx->config.prefetchHorizonMs);
  bool useExtension = (gameCtx->config.assetsPath != nullptr);
  const char *assetsPath = useExtension ? gameCtx->config.assetsPath : "";

  SDL_LockMutex(classUp->eventMutex);
  if (!gameplay->IsMediaActive() || gameplay->tm == nullptr ||
      !KotonohaTimeIsStarted(gameplay->tm)) {
    SDL_UnlockMutex(classUp->eventMutex);
    return 0;
  }

  auto clearPendingAssetLoad = [&](const Kotonoha_OrsEvent *event) {
    auto pending = classUp->pendingAssetLoads.find(event);
    if (pending != classUp->pendingAssetLoads.end()) {
      if (pending->second.loadingSignaled) {
        KotonohaDemuxerSetExternalWaiter(gameplay->demuxer, false);
      }
      classUp->pendingAssetLoads.erase(pending);
    }
  };

  auto updatePendingAssetLoad = [&](const Kotonoha_OrsEvent *event, bool failed,
                                    bool signalLoading) {
    if (!gameplay->demuxer || !gameplay->demuxer->retryMissingAssets ||
        !failed) {
      clearPendingAssetLoad(event);
      return false;
    }

    const auto pendingResult =
        classUp->pendingAssetLoads.emplace(event, Event::PendingAssetLoad{});
    auto &pending = pendingResult.first->second;
    pending.retryAfterTicks = SDL_GetTicks() + kMissingAssetRetryIntervalMs;
    if (signalLoading && !pending.loadingSignaled) {
      pending.loadingSignaled = true;
      KotonohaDemuxerSetExternalWaiter(gameplay->demuxer, true);
    } else if (!signalLoading && pending.loadingSignaled) {
      pending.loadingSignaled = false;
      KotonohaDemuxerSetExternalWaiter(gameplay->demuxer, false);
    }
    return true;
  };

  {
    const Uint64 now = KotonohaTimeGet(gameplay->tm);
    for (auto it = classUp->liveEvents.begin();
         it != classUp->liveEvents.end();) {
      Uint64 liveEnd = 0;
      if (!KotonohaOrsTimeToMilliseconds((*it)->end, false, &liveEnd) ||
          liveEnd < now) {
        KLOG_DEBUG(SCRIPT, "Event ended: %s (end=%llu ms, now=%llu ms)",
                   EventName((*it)->command),
                   static_cast<unsigned long long>(liveEnd),
                   static_cast<unsigned long long>(now));
        it = classUp->liveEvents.erase(it);
      } else {
        ++it;
      }
    }
  }

  for (auto *event = classUp->eventsFromScript.data; event != nullptr;
       event = event->next) {
    const Uint64 actualTime = KotonohaTimeGet(gameplay->tm);
    Uint64 startMs = 0;
    Uint64 endMs = 0;

    if (event->eventTouched) {
      continue;
    }

    const auto pending = classUp->pendingAssetLoads.find(event);
    if (pending != classUp->pendingAssetLoads.end() &&
        SDL_GetTicks() < pending->second.retryAfterTicks) {
      continue;
    }

    if (!KotonohaOrsTimeToMilliseconds(event->start, true, &startMs) ||
        !KotonohaOrsTimeToMilliseconds(event->end, false, &endMs)) {
      KLOG_WARN(SCRIPT,
                "Skipping ORS event with invalid 24-fps timestamp "
                "start=%llu end=%llu",
                static_cast<unsigned long long>(event->start),
                static_cast<unsigned long long>(event->end));
      clearPendingAssetLoad(event);
      event->eventTouched = true;
      continue;
    }

    if (!gameplay->ShouldPrefetchAllMedia() && startMs > actualTime &&
        startMs - actualTime > kMediaPrefetchHorizonMs) {
      continue;
    }

    const bool oneShotAudio =
        startMs == 0 && endMs == 0 &&
        (event->command == PLAY_VOICE || event->command == PLAY_SE);
    if (endMs < actualTime && !oneShotAudio) {
      clearPendingAssetLoad(event);
      event->eventTouched = true;
      continue;
    }

    bool hasMediaAttachment = false;
    bool mediaAttachmentFailed = false;
    switch (event->command) {
    case PLAY_VOICE: {
      if (event->data.play_voice->path != nullptr &&
          SDL_strlen(event->data.play_voice->path) > 0) {
        hasMediaAttachment = true;
        const std::string voiceLogical = BuildString(
            event->data.play_voice->path, "", useExtension ? ".OGG" : "");
        const std::string voicePath =
            useExtension ? ResolveAssetPath(assetsPath, voiceLogical)
                         : voiceLogical;
        mediaAttachmentFailed =
            gameplay->audio->AddMedia(voicePath.c_str(), startMs,
                                      (startMs == 0 && endMs == 0) ? 0
                                      : endMs > SDL_MAX_UINT64 - 1000
                                          ? SDL_MAX_UINT64
                                          : endMs + 1000,
                                      false, "Voice") == nullptr;

        const char *key = event->data.play_voice->character_short;
        if (key != nullptr && *key != '\0') {
          int16_t *decodedPcm = nullptr;
          size_t decodedSampleCount = 0;
          if (KotonohaPcmDecodeVoice(voicePath.c_str(), &decodedPcm,
                                     &decodedSampleCount)) {
            std::unique_ptr<int16_t, decltype(&SDL_free)> pcmOwner(decodedPcm,
                                                                   SDL_free);
            const std::vector<int16_t> pcm(pcmOwner.get(),
                                           pcmOwner.get() + decodedSampleCount);
            VoiceAnimation animation{};
            animation.activity = AnalyzeVoiceActivity(pcm);
            KotonohaOrsTimeToZeroBasedFrame(event->start, true,
                                            &animation.startTick);
            classUp->voiceAnimations[event] = std::move(animation);
          }
        }
      }
      break;
    }

    case PLAY_SE:
      if (event->data.play_se->path != nullptr &&
          SDL_strlen(event->data.play_se->path) > 0) {
        hasMediaAttachment = true;
        const std::string seLogical = BuildString(event->data.play_se->path, "",
                                                  useExtension ? ".OGG" : "");
        const std::string sePath =
            useExtension ? ResolveAssetPath(assetsPath, seLogical) : seLogical;
        mediaAttachmentFailed =
            gameplay->audio->AddMedia(sePath.c_str(), startMs, endMs, true,
                                      "Se") == nullptr;
      }
      break;

    case PLAY_BGM:
      if (event->data.path_end->path != nullptr &&
          SDL_strlen(event->data.path_end->path) > 0) {
        hasMediaAttachment = true;
        std::string str = ToUpper(event->data.path_end->path);
        if (useExtension) {
          const std::string introPath = ResolveAssetPath(
              assetsPath, BuildString(str.c_str(), "", "_INT.OGG"));
          const std::string loopPath = ResolveAssetPath(
              assetsPath, BuildString(str.c_str(), "", "_LOOP.OGG"));
          if (FileExists(introPath) && FileExists(loopPath)) {
            mediaAttachmentFailed = gameplay->audio->AddIntroLoopMedia(
                                        introPath.c_str(), loopPath.c_str(),
                                        startMs, endMs, "BGM") == nullptr;
          } else {
            mediaAttachmentFailed =
                gameplay->audio->AddMedia(loopPath.c_str(), startMs, endMs,
                                          true, "BGM") == nullptr;
          }
        } else {
          mediaAttachmentFailed =
              gameplay->audio->AddMedia(event->data.path_end->path, startMs,
                                        endMs, true, "BGM") == nullptr;
        }
      }
      break;

    case END_BGM:
      if (event->data.path_end->path != nullptr &&
          SDL_strlen(event->data.path_end->path) > 0) {
        hasMediaAttachment = true;
        std::string str = ToUpper(event->data.path_end->path);
        const std::string bgmLogical =
            BuildString(useExtension ? str.c_str() : event->data.path_end->path,
                        "", useExtension ? ".OGG" : "");
        const std::string bgmPath =
            useExtension ? ResolveAssetPath(assetsPath, bgmLogical)
                         : bgmLogical;
        mediaAttachmentFailed =
            gameplay->audio->AddMedia(bgmPath.c_str(), startMs, endMs, true,
                                      "BGM") == nullptr;
      }
      break;

    case END_ROLL:
      if (event->data.path_end->path != nullptr &&
          SDL_strlen(event->data.path_end->path) > 0) {
        hasMediaAttachment = true;
        const std::string rollLogical = BuildString(
            event->data.path_end->path, "", useExtension ? ".WMV" : "");
        const std::string rollPath =
            useExtension ? ResolveAssetPath(assetsPath, rollLogical)
                         : rollLogical;
        mediaAttachmentFailed =
            !gameplay->video->Register(rollPath.c_str(), startMs, endMs);
      }
      break;

    case PLAY_MOVIE:
      if (event->data.play_movie->path != nullptr &&
          SDL_strlen(event->data.play_movie->path) > 0) {
        hasMediaAttachment = true;
        const std::string movieLogical = BuildString(
            event->data.play_movie->path, "", useExtension ? ".WMV" : "");
        const std::string moviePath =
            useExtension ? ResolveAssetPath(assetsPath, movieLogical)
                         : movieLogical;

        Uint64 startFrame = 0;
        Uint64 endFrame = 0;
        const bool startMapped =
            KotonohaOrsTimeToTick(event->start, true, &startFrame);
        const bool endMapped =
            KotonohaOrsTimeToTick(event->end, false, &endFrame);

        if (startMapped && endMapped && endFrame >= startFrame) {
          mediaAttachmentFailed = !gameplay->video->Register(
              moviePath.c_str(), startMs, endMs, true, endFrame);
        } else {
          mediaAttachmentFailed = !gameplay->video->Register(
              moviePath.c_str(), startMs,
              endMs > SDL_MAX_UINT64 - 50 ? SDL_MAX_UINT64 : endMs + 50);
        }
      }
      break;

    case CREATE_BG:
      if (event->data.create_bg->path != nullptr &&
          SDL_strlen(event->data.create_bg->path) > 0) {
        hasMediaAttachment = true;
        const std::string bgLogical = BuildString(
            event->data.create_bg->path, "", useExtension ? ".PNG" : "");
        const std::string bgPath =
            useExtension ? ResolveAssetPath(assetsPath, bgLogical) : bgLogical;
        mediaAttachmentFailed =
            gameplay->image->Register(bgPath.c_str(), startMs, endMs, 0) == 0;
      }
      break;

    case BLACK_FADE:
    case WHITE_FADE:
      if (event->data.fade != nullptr && gameplay->fade != nullptr) {
        const FadeColor color =
            event->command == WHITE_FADE ? FadeColor::White : FadeColor::Black;
        const FadeDirection direction =
            event->data.fade->a ? FadeDirection::In : FadeDirection::Out;
        gameplay->fade->Register(event->start, event->end, color, direction);
      }
      break;

    default:
      break;
    }

    const bool signalLoading = startMs <= actualTime ||
                               startMs - actualTime <= kMediaPrefetchHorizonMs;
    event->eventTouched =
        !hasMediaAttachment ||
        !updatePendingAssetLoad(event, mediaAttachmentFailed, signalLoading);
    if (event->eventTouched && classUp->liveEvents.insert(event).second) {
      KLOG_DEBUG(SCRIPT,
                 "Event inserted: %s (start=%llu ms, end=%llu ms, now=%llu ms)",
                 EventName(event->command),
                 static_cast<unsigned long long>(startMs),
                 static_cast<unsigned long long>(endMs),
                 static_cast<unsigned long long>(actualTime));
    }
  }

  const Uint64 currentTime = KotonohaTimeGet(gameplay->tm);
  Uint64 currentTick = Kotonoha_MillisecondsToOrsTick(currentTime) - 1;
  for (auto it = classUp->voiceAnimations.begin();
       it != classUp->voiceAnimations.end();) {
    const Kotonoha_OrsEvent *event = it->first;
    Uint64 startMs = 0;
    Uint64 endMs = 0;
    if (event == nullptr || event->data.play_voice == nullptr ||
        !KotonohaOrsTimeToMilliseconds(event->start, true, &startMs) ||
        !KotonohaOrsTimeToMilliseconds(event->end, false, &endMs) ||
        currentTime >= endMs) {
      it = classUp->voiceAnimations.erase(it);
      continue;
    }
    if (currentTime < startMs) {
      ++it;
      continue;
    }

    auto &animation = it->second;
    if (currentTick < animation.startTick) {
      ++it;
      continue;
    }
    if (animation.state.initialized && currentTick < animation.state.lastTick) {
      animation.state = MouthAnimationState{};
    }
    const Uint64 firstTick = animation.state.initialized
                                 ? animation.state.lastTick + 1
                                 : animation.startTick;
    for (Uint64 tick = firstTick; tick <= currentTick; ++tick) {
      AdvanceMouthAnimation(animation.state, animation.activity,
                            animation.startTick, tick);
    }
    animation.state.lastTick = currentTick;
    animation.state.initialized = true;
    gameplay->image->SetAbcState(event->data.play_voice->character_short,
                                 animation.state.index, startMs, endMs);
    ++it;
  }

  SDL_UnlockMutex(classUp->eventMutex);
  return 0;
}

void Event::SetSubtitlesActive(bool active) {
  if (subtitleTrack == nullptr) {
    return;
  }

  for (const SubtitleEventState &state : subtitleEvents) {
    if (state.index < 0 || state.index >= subtitleTrack->n_events) {
      continue;
    }

    ASS_Event *event = &subtitleTrack->events[state.index];
    if (active) {
      event->Start = state.start;
      event->Duration = state.duration;
    } else {
      event->Duration = 0;
    }
  }
}

void Event::ActivateSubtitles() {
  if (activeSubtitleTrackOwner == this) {
    return;
  }
  if (activeSubtitleTrackOwner != nullptr) {
    activeSubtitleTrackOwner->SetSubtitlesActive(false);
  }
  SetSubtitlesActive(true);
  activeSubtitleTrackOwner = this;
}

void Event::Reset(void *gameplay) {
  auto *gp = static_cast<Gameplay *>(gameplay);
  if (gp == nullptr || eventMutex == nullptr) {
    return;
  }

  SDL_LockMutex(eventMutex);

  for (auto &pending : pendingAssetLoads) {
    if (pending.second.loadingSignaled) {
      KotonohaDemuxerSetExternalWaiter(gp->demuxer, false);
    }
  }

  gp->video->Reset();
  gp->image->Reset();
  gp->audio->RemoveMedia(nullptr);

  pendingAssetLoads.clear();
  liveEvents.clear();
  voiceAnimations.clear();
  Uint64 actualTime = KotonohaTimeGet(gp->tm);

  for (auto *event = this->eventsFromScript.data; event != nullptr;
       event = event->next) {
    Uint64 endMs = 0;
    if (!KotonohaOrsTimeToMilliseconds(event->end, false, &endMs)) {
      KLOG_WARN(SCRIPT,
                "Skipping reset state for invalid ORS end timestamp %llu",
                static_cast<unsigned long long>(event->end));
      event->eventTouched = true;
      continue;
    }
    if (endMs > actualTime)
      event->eventTouched = false;
  }

  SDL_UnlockMutex(eventMutex);
}

Event::Event(const char *orsPath, void *gameplay, struct Kotonoha_Game *gameCtx,
             TTF_Font *font)
    : eventMutex(nullptr), lastTime(0) {
  eventsFromScript = KotonohaOrsParser(orsPath);

  if (eventsFromScript.size == 0) {
    throw std::runtime_error("Ors invalid");
  }

  auto *gp = static_cast<Gameplay *>(gameplay);
  if (gp == nullptr || gameCtx == nullptr) {
    KotonohaOrsClean(&eventsFromScript);
    throw std::runtime_error("Invalid gameplay context");
  }

  std::stringstream subSs;

  if (gameCtx->config.assStylesData != nullptr &&
      gameCtx->config.assStylesData[0] != '\0') {
    subSs << gameCtx->config.assStylesData << std::endl;
  } else {
    subSs << "[Script Info]\n"
          << "Title: " << orsPath << "\n"
          << "ScriptType: v4.00+\n"
          << "PlayResX: 1280\n"
          << "PlayResY: 720\n"
          << "WrapStyle: 0\n"
          << "ScaledBorderAndShadow: yes\n"
          << "YCbCr Matrix: None\n\n"
          << std::endl;
  }

  if (gp->sb->track == nullptr) {
    gp->sb->track = ass_new_track(gp->sb->ass_library);
  } else {
    for (int i = 0; i < gp->sb->track->n_events; ++i) {
      if (gp->sb->track->events[i].Text != nullptr) {
        SDL_free(gp->sb->track->events[i].Text);
        gp->sb->track->events[i].Text = nullptr;
      }
    }
    gp->sb->track->n_events = 0;
    gp->sb->track->n_styles = 0;
  }

  ass_process_data(gp->sb->track, const_cast<char *>(subSs.str().c_str()),
                   static_cast<int>(subSs.str().size()));

  for (auto *event = eventsFromScript.data; event != nullptr;
       event = event->next) {
    switch (event->command) {
    case PRINT_TEXT: {
      Uint64 startMs = 0;
      Uint64 endMs = 0;
      if (!KotonohaOrsTimeToMilliseconds(event->start, true, &startMs) ||
          !KotonohaOrsTimeToMilliseconds(event->end, false, &endMs) ||
          endMs < startMs) {
        KLOG_ERROR(SCRIPT, "Invalid subtitle timestamp range");
      }
      const int eventId = ass_alloc_event(gp->sb->track);
      if (eventId < 0) {
        throw std::runtime_error("Failed to allocate subtitle event");
      }
      ASS_Event *subtitleEvent = &gp->sb->track->events[eventId];
      subtitleEvent->Start = static_cast<long long>(startMs);
      subtitleEvent->Duration = static_cast<long long>(endMs - startMs);

      for (int i = 0; i < eventId; ++i) {
        ASS_Event *previous = &gp->sb->track->events[i];
        if (previous->Start > static_cast<long long>(startMs)) {
          continue;
        }

        const long long elapsed =
            static_cast<long long>(startMs) - previous->Start;
        if (previous->Duration > elapsed) {
          previous->Duration = elapsed;
        }
      }

      subtitleEvent->Text =
          SDL_strdup(BuildString(event->data.print_text->text).c_str());

      for (int i = 0; i < gp->sb->track->n_styles; ++i) {
        ASS_Style *style = gp->sb->track->styles + i;

        if (SDL_strcmp(style->Name, "Default") == 0) {
          subtitleEvent->Style = i;
        }

        const std::string charName =
            BuildString(event->data.print_text->character);
        if (!charName.empty() &&
            SDL_strcmp(style->Name, charName.c_str()) == 0) {
          subtitleEvent->Style = i;
          break;
        }
      }
      break;
    }

    case SetSELECT: {
      Uint64 startMs = 0;
      Uint64 endMs = 0;
      if (!KotonohaOrsTimeToMilliseconds(event->start, true, &startMs) ||
          !KotonohaOrsTimeToMilliseconds(event->end, false, &endMs)) {
        KLOG_ERROR(SCRIPT, "Invalid choice prompt timestamp range");
        throw std::runtime_error("Invalid choice prompt timestamps");
      }
      std::vector<std::string> options;
      for (char **it = event->data.set_select->options; *it != nullptr; ++it) {
        options.push_back(BuildString(*it));
      }

      if (gp->prompt != nullptr) {
        delete gp->prompt;
        gp->prompt = nullptr;
      }

      gp->prompt =
          new Prompt(options, &gp->promptId, startMs, endMs, gp->tm, font,
                     gameCtx->config.promptSelectSound != nullptr
                         ? gameCtx->config.promptSelectSound
                         : "assets/SESELECT.OGG",
                     gameCtx->config.promptViewSound != nullptr
                         ? gameCtx->config.promptViewSound
                         : "assets/SEVIEW.OGG");
      gp->prompt->SetAudio(gp->audio);
      gp->putPrompt = true;
      break;
    }

    case SkipFRAME:
    case Next: {
      if (!KotonohaOrsTimeToMilliseconds(event->start, true, &lastTime)) {
        KLOG_ERROR(SCRIPT, "Invalid scene end timestamp");
        throw std::runtime_error("Invalid scene end timestamp");
      }
      break;
    }

    default:
      break;
    }
  }

  eventMutex = SDL_CreateMutex();
  if (eventMutex == nullptr) {
    KotonohaOrsClean(&eventsFromScript);
    throw std::runtime_error("Failed to create event mutex");
  }

  void **parms = static_cast<void **>(SDL_malloc(sizeof(void *) * 3));
  if (parms == nullptr) {
    KotonohaOrsClean(&eventsFromScript);
    SDL_DestroyMutex(eventMutex);
    eventMutex = nullptr;
    throw std::runtime_error("Failed to allocate EventManager params");
  }

  parms[0] = gameplay;
  parms[1] = gameCtx;
  parms[2] = this;
  SDL_LockMutex(gameCtx->taskLock);
  auto *tasks =
      static_cast<std::vector<std::tuple<SDL_ThreadFunction, void *>> *>(
          gameCtx->processPoolTasks);

  EventManager(parms);
  tasks->emplace_back(EventManager, parms);
  SDL_UnlockMutex(gameCtx->taskLock);
}

bool Event::CheckEnd(void *gameplay) {
  auto *gp = static_cast<Gameplay *>(gameplay);
  if (gp == nullptr || gp->tm == nullptr) {
    return true;
  }

  return KotonohaTimeGet(gp->tm) > lastTime;
}

Event::~Event() {
  SetSubtitlesActive(false);
  if (activeSubtitleTrackOwner == this) {
    activeSubtitleTrackOwner = nullptr;
  }

  pendingAssetLoads.clear();
  KotonohaOrsClean(&eventsFromScript);
  if (eventMutex != nullptr) {
    SDL_DestroyMutex(eventMutex);
    eventMutex = nullptr;
  }
}

} // namespace Kotonoha