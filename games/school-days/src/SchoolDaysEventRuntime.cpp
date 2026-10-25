#include <Kotonoha/routing/SchoolDaysEventRuntime.hpp>
#include <Kotonoha/SchoolDaysBgmAssetResolver.hpp>
#include <Kotonoha/audio/VoicePcm.hpp>
#include <Kotonoha/SchoolDaysMovieAssetResolver.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/Gameplay.hpp>
#include <Kotonoha/routing/SchoolDaysPromptBehavior.hpp>
#include <SDL3/SDL.h>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Kotonoha {

	namespace {
			class ScopedSdlMutex {
			public:
				explicit ScopedSdlMutex(SDL_Mutex* mutex) : mutex_(mutex) {
					locked_ = mutex_ != nullptr;
					if (locked_) SDL_LockMutex(mutex_);
				}
				~ScopedSdlMutex() {
					if (locked_) SDL_UnlockMutex(mutex_);
				}
				ScopedSdlMutex(const ScopedSdlMutex&) = delete;
				ScopedSdlMutex& operator=(const ScopedSdlMutex&) = delete;
				bool Locked() const { return locked_; }

			private:
				SDL_Mutex* mutex_ = nullptr;
				bool locked_ = false;
			};

			class ScopedOrsData {
			public:
				explicit ScopedOrsData(Kotonoha_orsData* data) : data_(data) {}
				~ScopedOrsData() {
					if (data_ != nullptr) Kotonoha_OrsClean(data_);
				}
				ScopedOrsData(const ScopedOrsData&) = delete;
				ScopedOrsData& operator=(const ScopedOrsData&) = delete;
				void Release() { data_ = nullptr; }
			private:
				Kotonoha_orsData* data_ = nullptr;
			};

		static std::string BuildString(const char* str,
			const std::string& prefix = "",
			const std::string& suffix = "") {
			if (str == nullptr) {
				return "";
			}

			std::string result = prefix + std::string(str) + suffix;

			size_t lastSlash = result.find_last_of('/');
			if (lastSlash == std::string::npos) {
				lastSlash = 0;
			}
			else {
				++lastSlash;
			}

			if (result.size() - lastSlash >= 4 &&
				result.compare(lastSlash, 4, "UNC_") == 0) {
				result = result.substr(0, lastSlash) + result.substr(lastSlash + 4);
			}

			return result;
		}
		static std::string AbcKey(std::string key) {
			std::transform(key.begin(), key.end(), key.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return key;
		}
			static void PrepareChoicePrompt(Gameplay* gp,
				const Kotonoha_orsData& source, TTF_Font* font) {
			if (gp == nullptr) return;
			for (auto* event = source.data; event != nullptr; event = event->next) {
				if (event->command != SetSELECT || event->data.set_select == nullptr)
					continue;
				std::vector<std::string> options;
				for (char** it = event->data.set_select->options;
					it != nullptr && *it != nullptr; ++it)
					options.push_back(BuildString(*it));
					gp->ownedPrompt = std::make_unique<Prompt>(options, &gp->promptId,
					Kotonoha_SceneTickToMillisecondsCeil(event->startTick),
						Kotonoha_SceneTickToMillisecondsCeil(event->endTick), gp->tm,
						font);
					gp->prompt = gp->ownedPrompt.get();
						SchoolDaysPromptPolicy promptPolicy{};
					promptPolicy.enabled = true;
					promptPolicy.startTick = event->startTick;
					promptPolicy.endTick = event->endTick;
						// Shipped v1.02 uses UseEnglish=1 + SelectType=1. The native
						// selector chooses the *_H CMAP under that exact pair, so the
						// authenticated two-choice layout is top/bottom.
					promptPolicy.twoChoiceOrientation =
								SchoolDaysPromptOrientation::TopBottom;
						gp->prompt->SetBehavior(
								std::make_unique<SchoolDaysPromptBehavior>(promptPolicy));
					gp->prompt->SetAutoMode(gp->AutoChoiceMode());
			gp->putPrompt = true;
			return; // Corpus v1.02 has at most one SetSELECT per ORS.
			}
		}

		static bool ResolveNarrativeMedia(const char* assetRoot,
			const std::string& logical, std::string* physical) {
			if (physical == nullptr) return false;

			// Se o próprio logical já for uma URL completa http(s)://
			if (logical.size() >= 7 &&
				(logical.rfind("http://", 0) == 0 || logical.rfind("https://", 0) == 0 ||
				 logical.rfind("HTTP://", 0) == 0 || logical.rfind("HTTPS://", 0) == 0)) {
				*physical = logical;
				return true;
			}

			// Se o assetRoot for uma URL http(s):// (servidor remoto), junte assetRoot com logical garantindo a barra '/'
			if (assetRoot != nullptr && assetRoot[0] != '\0') {
				std::string root(assetRoot);
				if (root.size() >= 7 &&
					(root.rfind("http://", 0) == 0 || root.rfind("https://", 0) == 0 ||
					 root.rfind("HTTP://", 0) == 0 || root.rfind("HTTPS://", 0) == 0)) {
					std::string joined = root;
					if (!joined.empty() && joined.back() != '/' && logical.front() != '/') {
						joined += '/';
					} else if (!joined.empty() && joined.back() == '/' && logical.front() == '/') {
						joined.pop_back();
					}
					joined += logical;
					*physical = joined;
					return true;
				}
			}

			// Generic Kotonoha historically accepts raw/cwd-relative media paths
			// when no asset root is configured.  The typed School Days resolver is
			// only meaningful once a dedicated or generic root exists.
			if (assetRoot == nullptr || *assetRoot == '\0') {
				*physical = logical;
				return !physical->empty();
			}
			physical->clear();
			const auto resolved = ResolveSchoolDaysMediaAsset(assetRoot, logical);
			if (resolved.status == SchoolDaysAssetResolutionStatus::Present ||
				resolved.status == SchoolDaysAssetResolutionStatus::Missing) {
				*physical = resolved.physical;
				return !physical->empty();
			}
			SDL_Log("[SD-ASSET] Rejecting ambiguous/invalid narrative media path: %s",
				logical.c_str());
			return false;
		}


	} // namespace

	int SchoolDaysEventRuntime::EventManager(void* data) {
		void** parms = static_cast<void**>(data);
		if (parms == nullptr) {
			return -1;
		}

		auto* gameplay = static_cast<Gameplay*>(parms[0]);
		auto* gameCtx = static_cast<struct Kotonoha_Game*>(parms[1]);
		auto* classUp = static_cast<SchoolDaysEventRuntime*>(parms[2]);
		if (gameplay == nullptr || gameCtx == nullptr || classUp == nullptr ||
			classUp->eventMutex == nullptr) {
			return -1;
		}
			try {
			const auto cancelled = [gameplay]() {
				return gameplay->ShutdownRequested();
			};
			if (cancelled()) return -1;

			// KTRF has a dedicated authenticated School Days asset mount.  It is
			// deliberately independent from the generic Kotonoha -p/assetsPath; a
			// valid School Days launch must not lose narrative media just because
			// the generic root was not configured.
			const auto assetRoot = SelectSchoolDaysAssetRoot(
				gameCtx->profileAssetsRoot, gameCtx->config.assetsPath);
			const bool useExtension = assetRoot.available;
			const char* assetsPath = assetRoot.root;

			ScopedSdlMutex eventGuard(classUp->eventMutex);
			if (!eventGuard.Locked()) return -1;
			if (cancelled()) {
				return -1;
			}
		if (gameplay->tm == nullptr || !gameplay->tm->started) {
			return 0;
		}

		const Uint64 actualTime = Kotonoha_timeGet(gameplay->tm);
		const Kotonoha_SceneTick currentTick =
			Kotonoha_MillisecondsToSceneTick(actualTime);
			const bool suppressBackLogCapture =
				classUp->suppressBackLogCaptureOnce.exchange(
					false, std::memory_order_acq_rel);
		/* Registration may prepare media early; semantic dispatch stays on the
		   real scene tick. The old runtime used a 10-second preparation window. */
		Kotonoha_SceneTick preparationLimit =
			Kotonoha_MillisecondsToSceneTick(actualTime + 10000);
		const SchoolDaysOptionalTick nextTick = classUp->timeline.NextTick();
		if (nextTick && preparationLimit > *nextTick)
			preparationLimit = *nextTick;
			const auto prepareEvent = [&](Kotonoha_orsEvent* event) -> bool {
				if (cancelled()) return false;
				if (event == nullptr || event->eventPrepared) return true;
				event->eventPrepared = true;
			const Uint64 startMs =
				Kotonoha_SceneTickToMillisecondsCeil(event->startTick);
			const Uint64 endMs =
				Kotonoha_SceneTickToMillisecondsCeil(event->endTick);

					const Uint64 staleAtMs =
						event->command == PLAY_VOICE ? endMs + 1000 : endMs;
					if (staleAtMs < actualTime) return true;

			switch (event->command) {
			case PLAY_VOICE: {
				if (event->data.play_voice->path != nullptr &&
					SDL_strlen(event->data.play_voice->path) > 0) {
					const std::string voiceLogical = BuildString(
						event->data.play_voice->path, "",
						useExtension ? ".OGG" : "");
					std::string voicePath = voiceLogical;
					if (useExtension &&
						!ResolveNarrativeMedia(assetsPath, voiceLogical, &voicePath))
						break;
					classUp->voiceMedia[event] = gameplay->audio->AddMedia(
						voicePath.c_str(),
						startMs,
						endMs + 1000,
						false,
						"Voice", false);
					if (classUp->voiceMedia[event] != nullptr &&
						*SchoolDaysVoiceAnimationKey(event) != '\0') {
						std::vector<int16_t> pcm;
							if (DecodeVoicePcm(voicePath, pcm,
									gameplay->ShutdownFlag()))
							classUp->voiceActivity[event] = AnalyzePcmActivity(pcm);
					}
				}
				break;
			}

			case PLAY_SE:
				if (!SchoolDaysSeSlots::Valid(event->data.play_se->a)) {
					SDL_Log("[SD-AUDIO] Ignoring out-of-range SE slot %llu",
						static_cast<unsigned long long>(event->data.play_se->a));
					break;
				}
				if (event->data.play_se->path != nullptr &&
					SDL_strlen(event->data.play_se->path) > 0) {
					const std::string seLogical = BuildString(
						event->data.play_se->path, "",
						useExtension ? ".OGG" : "");
					std::string sePath = seLogical;
					if (useExtension &&
						!ResolveNarrativeMedia(assetsPath, seLogical, &sePath))
						break;
					classUp->seMedia[event] = gameplay->audio->AddMedia(
						sePath.c_str(),
						startMs,
						endMs,
						true,
						"Se", false);
				}
				break;

			case PLAY_BGM:
				if (event->data.path_end->path != nullptr &&
					SDL_strlen(event->data.path_end->path) > 0) {
					const auto resources = SchoolDaysBgmResources::Normal(
						event->data.path_end->path);
					std::string intro;
					std::string loop;
					if (!ResolveNarrativeMedia(assetsPath, resources.intro, &intro) ||
						!ResolveNarrativeMedia(assetsPath, resources.loop, &loop))
						break;
					classUp->normalBgmMedia[event] = gameplay->audio->AddIntroLoopMedia(
						intro.c_str(), loop.c_str(), startMs, endMs);
				}
				break;

			case END_BGM:
				if (event->data.path_end->path != nullptr &&
					SDL_strlen(event->data.path_end->path) > 0) {
					const auto resources = SchoolDaysBgmResources::Ending(
						event->data.path_end->path);
					std::string path;
					if (!ResolveNarrativeMedia(assetsPath, resources.oneShot, &path))
						break;
					classUp->endingBgmMedia[event] = gameplay->audio->AddMedia(
						path.c_str(), startMs, endMs, false, "BGM", false);
				}
				break;

			case END_ROLL:
			case PLAY_MOVIE: {
				const bool roll = event->command == END_ROLL;
				const char* logicalPath = roll ? event->data.path_end->path
					: event->data.play_movie->path;
				if (logicalPath == nullptr || *logicalPath == '\0') break;
				auto state = SchoolDaysMovieEventState::Create(
					roll ? SchoolDaysMovieKind::EndRoll : SchoolDaysMovieKind::PlayMovie,
					logicalPath, event->startTick, event->endTick,
					roll ? 0 : event->data.play_movie->a);
				bool ready = false;
				if (state.Supported()) {
					std::string resolved;
					if (ResolveNarrativeMedia(assetsPath, state.resource, &resolved))
						ready = gameplay->video->Prepare(event, resolved.c_str());
				} else {
					SDL_Log("[SD-MOVIE] Unsupported PlayMovie numeric=%llu path=%s",
						static_cast<unsigned long long>(state.numeric), logicalPath);
				}
				state.Prepare(ready);
				classUp->movieStates[event] = std::move(state);
				break;
			}

			case CREATE_BG:
				if (event->data.create_bg->path != nullptr &&
					SDL_strlen(event->data.create_bg->path) > 0) {
					if (!assetRoot.available) {
						const Uint64 baseGeneration = gameplay->image->Register(
							event->data.create_bg->path, startMs, endMs, 0);
						if (baseGeneration != 0)
							classUp->bgResources[event] = baseGeneration;
						break;
					}
					const auto resolved = ResolveSchoolDaysImageAsset(
						assetsPath, event->data.create_bg->path);
					if (resolved.present && !resolved.collision) {
						const Uint64 baseGeneration = gameplay->image->Register(
							resolved.physical.c_str(), startMs, endMs, 0);
						if (baseGeneration != 0)
							classUp->bgResources[event] = baseGeneration;
					} else {
						SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
							"[SD-IMAGE] %s CreateBG: %s",
							resolved.collision ? "Ambiguous" : "Missing",
							resolved.logical.c_str());
					}
				}
				break;

			case BLACK_FADE:
			case WHITE_FADE:
				if (event->data.fade != nullptr && gameplay->fade != nullptr) {
					const FadeColor color = event->command == WHITE_FADE
						? FadeColor::White
						: FadeColor::Black;
					const FadeDirection direction = event->data.fade->a
						? FadeDirection::In
						: FadeDirection::Out;
					gameplay->fade->Register(
						event->start,
						event->end,
						color,
						direction);
				}
				break;

			default:
				break;
			}
				return !cancelled();
			};
		classUp->activeVoiceEvents.erase(std::remove_if(
			classUp->activeVoiceEvents.begin(), classUp->activeVoiceEvents.end(),
			[currentTick](const Kotonoha_orsEvent* voice) {
				return currentTick >= voice->endTick;
			}), classUp->activeVoiceEvents.end());
		const auto bindVoiceToActiveBase =
			[&](const Kotonoha_orsEvent* voice) {
				const std::string key = AbcKey(SchoolDaysVoiceAnimationKey(voice));
				if (key.empty()) return;
				auto* group = gameplay->image->FindAbcGroup(key, actualTime);
				const Uint64 generation = gameplay->image->ActiveBaseGeneration();
				if (group == nullptr || group->ownerBaseGeneration != generation)
					return;
				auto previous = classUp->abcBindings.find(key);
				if (previous != classUp->abcBindings.end())
					gameplay->image->UnbindAbcGroup(previous->second.group);
				if (gameplay->image->BindAbcGroup(group, generation,
					Kotonoha_SceneTickToMillisecondsCeil(voice->startTick),
					Kotonoha_SceneTickToMillisecondsCeil(voice->endTick)))
					classUp->abcBindings[key] =
						{voice, group, generation, {}, 0, false};
			};
		for (auto* event : classUp->timeline.AdvanceTo(currentTick)) {
				if (cancelled()) {
					return -1;
				}
				if (!prepareEvent(event)) {
					return -1;
				}
					if (!suppressBackLogCapture && event->command == PRINT_TEXT &&
					SDL_GetAtomicInt(&gameCtx->textLogCaptureEnabled) != 0 &&
					gameCtx->textLogAppend != nullptr &&
					event->data.print_text != nullptr) {
						const std::string resolvedCharacter =
							BuildString(event->data.print_text->character);
					const std::string resolved = BuildString(event->data.print_text->text);
					gameCtx->textLogAppend(gameCtx->textLogUserdata,
							resolvedCharacter.c_str(),
						resolved.c_str());
				}
			if (event->command == CREATE_BG) {
				auto found = classUp->bgResources.find(event);
				if (found != classUp->bgResources.end() &&
					gameplay->image->ActivateBase(found->second)) {
					classUp->abcBindings.clear();
					for (const auto* voice : classUp->activeVoiceEvents)
						if (voice->startTick <= currentTick &&
							currentTick < voice->endTick)
							bindVoiceToActiveBase(voice);
				}
			}
			else if (event->command == MOVE_SOM) {
				// SOMCON is an optional original peripheral; no hardware on this port.
				classUp->moveSom.Start(event);
			}
			else if (event->command == SetSELECT) {
					// Prompt is prepared when the ORS is loaded, but native PlayData
					// consumes its restored tag4 only when SetSELECT is actually reached.
					classUp->setSelectReached.store(true, std::memory_order_release);
			}
			else if (event->command == PLAY_VOICE) {
				const bool allowed = SchoolDaysVoiceAllowed(
					event->data.play_voice->a,
					Kotonoha_IsMenuVoiceEnabled(gameCtx));
				auto found = classUp->voiceMedia.find(event);
				if (found == classUp->voiceMedia.end() || found->second == nullptr)
					continue; /* Missing media never blocks the timeline. */
				if (allowed) {
					gameplay->audio->SetMediaEnabled(found->second, true);
					const std::string key = AbcKey(SchoolDaysVoiceAnimationKey(event));
					if (!key.empty()) {
						classUp->activeVoiceEvents.push_back(event);
						if (currentTick < event->endTick)
							bindVoiceToActiveBase(event);
						}
					SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO,
						"[SD-AUDIO] Voice activate key=%s",
						SchoolDaysVoiceAnimationKey(event));
				}
				else {
					gameplay->audio->RemoveMedia(found->second);
					classUp->voiceMedia.erase(found);
					SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO,
						"[SD-AUDIO] Voice blocked MenVoice");
				}
			}
			else if (event->command == PLAY_SE) {
				const Uint64 slot = event->data.play_se->a;
				if (!SchoolDaysSeSlots::Valid(slot)) continue;
				auto found = classUp->seMedia.find(event);
				Kotonoha_audioDecode* media =
					found == classUp->seMedia.end() ? nullptr : found->second;
				SchoolDaysSeSlots::Slot previous;
				classUp->seSlots.Replace(slot, event, media, &previous);
				if (previous.media != nullptr) {
					gameplay->audio->RemoveMedia(previous.media);
					classUp->seMedia.erase(previous.event);
					SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO,
						"[SD-AUDIO] SE slot replace slot=%llu",
						static_cast<unsigned long long>(slot));
				}
				if (media != nullptr)
					gameplay->audio->SetMediaEnabled(media, true);
			}
			else if (event->command == PLAY_BGM || event->command == END_BGM) {
				const bool normal = event->command == PLAY_BGM;
				auto& mediaMap = normal ? classUp->normalBgmMedia : classUp->endingBgmMedia;
				const auto resources = normal
					? SchoolDaysBgmResources::Normal(event->data.path_end->path)
					: SchoolDaysBgmResources::Ending(event->data.path_end->path);
				const auto previous = normal
					? classUp->bgmSlots.ReplaceNormal(event, resources)
					: classUp->bgmSlots.ReplaceEnding(event, resources);
				if (previous.event != nullptr) {
					auto old = mediaMap.find(previous.event);
					if (old != mediaMap.end()) {
						gameplay->audio->RemoveMedia(old->second);
						mediaMap.erase(old);
					}
				}
				auto found = mediaMap.find(event);
				if (found != mediaMap.end() && found->second != nullptr)
					gameplay->audio->SetMediaEnabled(found->second, true);
			}
			else if (event->command == PLAY_MOVIE || event->command == END_ROLL) {
				auto found = classUp->movieStates.find(event);
				if (found != classUp->movieStates.end() &&
					found->second.Activate(currentTick))
					gameplay->video->Activate(event);
			}
		}
		classUp->moveSom.AdvanceTo(currentTick);
		for (auto& item : classUp->movieStates) {
			auto& state = item.second;
			if (!state.eof && gameplay->video->IsEof(item.first))
				state.MarkEof();
			if (currentTick >= state.endTick && !state.completed) {
					/* Natural ORS END belongs to VideoRenderProcess.  Leaving the decoder
					   alive through the boundary lets RenderCallback retain the terminal
					   texture while an adjacent WMV produces its first frame.  Reset/cancel
					   paths still remove decoders explicitly. */
				state.AdvanceTo(currentTick);
			}
		}
			/* Natural movie completion is retired by the render callback so the
			   terminal texture can bridge an adjacent WMV. Retired decoders own SDL
			   textures/upscale caches, so collection and destruction intentionally
			   remain on Video::RenderCallback's renderer thread. */
		for (auto it = classUp->abcBindings.begin();
			it != classUp->abcBindings.end();) {
				if (cancelled()) {
					return -1;
				}
			auto& binding = it->second;
			if (binding.ownerBaseGeneration !=
					gameplay->image->ActiveBaseGeneration() ||
				currentTick >= binding.voice->endTick ||
				!gameplay->image->IsActiveGroup(binding.group, actualTime)) {
				gameplay->image->UnbindAbcGroup(binding.group);
				it = classUp->abcBindings.erase(it);
				continue;
			}
			const auto activity = classUp->voiceActivity.find(binding.voice);
			const std::vector<AbcActivityRecord> empty;
			const auto& records = activity == classUp->voiceActivity.end()
				? empty : activity->second;
				const double playbackRate = KotonohaTimeGetRate(gameplay->tm);
				if (AbcAnimationUpdatesAtRate(playbackRate)) {
					const Kotonoha_SceneTick first = binding.initialized
						? binding.lastTick + 1 : binding.voice->startTick;
					for (Kotonoha_SceneTick tick = first; tick <= currentTick; ++tick) {
							if (cancelled()) {
								return -1;
							}
						const Uint8 before = binding.state.activeIndex;
						AdvanceAbcAnimation(binding.state, records,
							binding.voice->startTick, tick);
						if (before != binding.state.activeIndex)
							SDL_LogDebug(SDL_LOG_CATEGORY_RENDER,
								"[SD-ABC] key=%s tick=%llu activity=%d state=%c",
								it->first.c_str(), static_cast<unsigned long long>(tick),
								IsVoiceActiveAt(records, tick - binding.voice->startTick) ? 1 : 0,
								'A' + binding.state.activeIndex);
					}
				}
				// Retail does not replay the skipped >=12x ImageChar updates after a
				// later rate drop.  Advancing this cursor while the state is frozen
				// preserves that discontinuity exactly.
			binding.lastTick = currentTick;
			binding.initialized = true;
			gameplay->image->SelectAbcState(binding.group, binding.state.activeIndex);
			++it;
		}
		const auto ending = classUp->bgmSlots.Ending();
		if (ending.event != nullptr && currentTick >= ending.event->endTick) {
			auto found = classUp->endingBgmMedia.find(ending.event);
			if (found != classUp->endingBgmMedia.end()) {
				gameplay->audio->RemoveMedia(found->second);
				classUp->endingBgmMedia.erase(found);
			}
			classUp->bgmSlots.ClearEnding();
		}
		const auto normal = classUp->bgmSlots.Normal();
		if (normal.event != nullptr && currentTick >= normal.event->endTick) {
			auto found = classUp->normalBgmMedia.find(normal.event);
			if (found != classUp->normalBgmMedia.end()) {
				gameplay->audio->RemoveMedia(found->second);
				classUp->normalBgmMedia.erase(found);
			}
			classUp->bgmSlots.ClearNormal();
		}

			/* Keep the original 10-second PREPARE horizon without allowing a batch of
			   future FFmpeg opens to delay semantic START dispatch.  At most one future
			   event is warmed per worker pass; due events above always take priority. */
			for (auto* event = classUp->eventsFromScript.data;
				event != nullptr && event->startTick <= preparationLimit;
				event = event->next) {
				if (event->startTick <= currentTick || event->eventPrepared) continue;
				if (!prepareEvent(event)) {
					return -1;
				}
				break;
			}
			classUp->readyToEnd.store(classUp->timeline.ReadyToEnd(currentTick),
				std::memory_order_release);

			return 0;
			} catch (const std::exception& error) {
				classUp->fatalError.store(true, std::memory_order_release);
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				             "[SD-EVENT] worker exception: %s", error.what());
				return -1;
			} catch (...) {
				classUp->fatalError.store(true, std::memory_order_release);
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				             "[SD-EVENT] worker exception: unknown");
				return -1;
			}
	}

		void SchoolDaysEventRuntime::Reset(void* gameplay, bool suppressReplaySideEffects) {
		auto* gp = static_cast<Gameplay*>(gameplay);
		if (gp == nullptr || eventMutex == nullptr) {
			return;
		}
			const bool choiceAlreadyCommitted = gp->choiceCommitSubmitted;

			ScopedSdlMutex eventGuard(eventMutex);
			if (!eventGuard.Locked()) {
				fatalError.store(true, std::memory_order_release);
				return;
			}
			try {
			readyToEnd.store(false, std::memory_order_release);
				setSelectReached.store(false, std::memory_order_release);
				if (suppressReplaySideEffects) {
					suppressBackLogCaptureOnce.store(true, std::memory_order_release);
				}

		abcBindings.clear();
		activeVoiceEvents.clear();
		voiceActivity.clear();
		gp->video->Reset();
		movieStates.clear();
		bgResources.clear();
		moveSom.Reset();
		gp->image->Reset();
		if (gp->prompt != nullptr) {
				// The Canvas retains both the callback and its userdata.  Across the
				// desktop EXE/DLL split the address used for Prompt::Render is not a
				// stable ownership key, so remove the exact object before freeing it.
				// Otherwise the next Canvas frame can invoke Prompt::Render with the
				// just-freed Prompt (ASAN: heap-use-after-free on skip-to-choice).
				if (gp->drawCanvas != nullptr)
					gp->drawCanvas->UnregisterCanvaUserData(gp->prompt);
			gp->ownedPrompt.reset();
			gp->prompt = nullptr;
		}
		gp->putPrompt = false;
			if (!choiceAlreadyCommitted) {
				gp->promptId = -2;
				gp->choiceCommitSubmitted = false;
				PrepareChoicePrompt(gp, eventsFromScript, font);
			} else {
				// Restart/seek is a presentation operation.  Once the KTRF router has
				// committed this scene's choice, do not expose a second interactive
				// prompt that could disagree with the already-applied routing effects.
				gp->choiceCommitSubmitted = true;
			}
		gp->audio->RemoveMedia(nullptr);
		voiceMedia.clear();
		seMedia.clear();
		seSlots.Clear();
		normalBgmMedia.clear();
		endingBgmMedia.clear();
		bgmSlots.Clear();

		if (gp->tm != nullptr && Kotonoha_timeIsStarted(gp->tm)) {
			const Uint64 actualTime = Kotonoha_timeGet(gp->tm);
			const Kotonoha_SceneTick target =
				Kotonoha_MillisecondsToSceneTick(actualTime);
			timeline.RebuildActiveAt(target);
			const auto activeSe = SchoolDaysSeSlots::ActiveEventsAt(
				&eventsFromScript, actualTime);
			const auto activeNormal = SchoolDaysBgmSlots::ActiveAt(
				&eventsFromScript, target, PLAY_BGM);
			const auto activeEnding = SchoolDaysBgmSlots::ActiveAt(
				&eventsFromScript, target, END_BGM);
			for (auto* event = eventsFromScript.data; event != nullptr;
				event = event->next) {
					if (event->command == SetSELECT && event->startTick <= target) {
						setSelectReached.store(true, std::memory_order_release);
					}
				event->eventPrepared =
					Kotonoha_SceneTickToMillisecondsCeil(event->endTick) <= actualTime;
				if (event->command == PLAY_SE && event->data.play_se != nullptr &&
					SchoolDaysSeSlots::Valid(event->data.play_se->a) &&
					event->startTick <= target &&
					activeSe[static_cast<size_t>(event->data.play_se->a)] != event) {
					event->eventPrepared = true;
					event->eventTouched = true;
				}
				if ((event->command == PLAY_BGM && event != activeNormal) ||
					(event->command == END_BGM && event != activeEnding)) {
					event->eventPrepared = true;
					event->eventTouched = true;
				}
			}
		}
		else {
			timeline.Restart();
			for (auto* event = eventsFromScript.data; event != nullptr;
				event = event->next) event->eventPrepared = false;
		}

			} catch (const std::exception& error) {
				fatalError.store(true, std::memory_order_release);
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				             "[SD-EVENT] reset exception: %s", error.what());
			} catch (...) {
				fatalError.store(true, std::memory_order_release);
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				             "[SD-EVENT] reset exception: unknown");
			}
	}

		GameplayEventTarget SchoolDaysEventRuntime::ComputeSkipToEndTarget(void* gameplay) const {
			auto* gp = static_cast<Gameplay*>(gameplay);
			if (gp == nullptr || gp->tm == nullptr || !Kotonoha_timeIsStarted(gp->tm))
				return {};
			const Kotonoha_SceneTick currentTick =
				Kotonoha_MillisecondsToSceneTick(Kotonoha_timeGet(gp->tm));
			const SchoolDaysOptionalTick target =
				timeline.ComputeSkipToEndTarget(currentTick);
			return target ? GameplayEventTarget{true, *target} : GameplayEventTarget{};
		}

		SchoolDaysEventRuntime::SchoolDaysEventRuntime(const char* orsPath, void* gameplay,
			struct Kotonoha_Game* gameCtx, TTF_Font* font)
			: eventMutex(nullptr), font(font), lastTime(0) {
			if (orsPath != nullptr && SchoolDaysAssetReferenceIsVirtual(orsPath)) {
				std::vector<uint8_t> bytes;
				std::string assetError;
				if (!ReadSchoolDaysAssetReference(orsPath, &bytes, &assetError)) {
					throw std::runtime_error("ORS virtual asset read failed: " + assetError);
				}
				eventsFromScript = Kotonoha_OrsParserBytes(
					bytes.empty() ? nullptr : bytes.data(), bytes.size());
			} else {
				eventsFromScript = Kotonoha_OrsParser(orsPath);
			}
				ScopedOrsData orsGuard(&eventsFromScript);

		if (eventsFromScript.size == 0) {
			throw std::runtime_error("Ors invalid");
		}
		timeline.Bind(&eventsFromScript);
			nextBoundaryTick = timeline.NextTick();
			if (nextBoundaryTick) {
				lastTime = Kotonoha_SceneTickToMillisecondsCeil(*nextBoundaryTick);
		}

		auto* gp = static_cast<Gameplay*>(gameplay);
			if (gp == nullptr || gameCtx == nullptr || gp->sb == nullptr ||
				gp->sb->ass_library == nullptr) {
			throw std::runtime_error("Invalid gameplay context");
		}

		std::stringstream subSs;
		subSs << "[Script Info]\nTitle:" << orsPath
			<< "\nScriptType: v4.00+\nWrapStyle: yes\nScaledBorderAndShadow: yes\n"
			<< "YCbCr Matrix: None\n\n"
				<< (gameCtx->config.assStylesData == nullptr
					? "" : gameCtx->config.assStylesData) << std::endl;

			ASS_Track* subtitleTrack = ass_new_track(gp->sb->ass_library);
			if (subtitleTrack == nullptr) {
				throw std::runtime_error("Failed to allocate ASS subtitle track");
			}
			std::unique_ptr<ASS_Track, void (*)(ASS_Track*)> subtitleTrackGuard(
				subtitleTrack, ass_free_track);

			ass_process_data(subtitleTrack,
			const_cast<char*>(subSs.str().c_str()),
			static_cast<int>(subSs.str().size()));
			SDL_Log("[SD-SUBTITLES] track styles=%d", subtitleTrack->n_styles);

		for (auto* event = eventsFromScript.data; event != nullptr;
			event = event->next) {
			switch (event->command) {
			case PRINT_TEXT: {
					const int subtitleIndex = ass_alloc_event(subtitleTrack);
					if (subtitleIndex < 0) {
						throw std::runtime_error("Failed to allocate ASS subtitle event");
					}
					ASS_Event* subtitleEvent = subtitleTrack->events + subtitleIndex;
				const Uint64 startMs = Kotonoha_SceneTickToMillisecondsCeil(event->startTick);
				const Uint64 endMs = Kotonoha_SceneTickToMillisecondsCeil(event->endTick);
				subtitleEvent->Start = startMs;
				subtitleEvent->Duration = endMs - startMs;
				subtitleEvent->Text =
					SDL_strdup(BuildString(event->data.print_text->text).c_str());

					for (int i = 0; i < subtitleTrack->n_styles; ++i) {
						ASS_Style* style = subtitleTrack->styles + i;

					if (SDL_strcmp(style->Name, "Default") == 0) {
						subtitleEvent->Style = i;
					}

					if (SDL_strcmp(style->Name,
						BuildString(event->data.print_text->character).c_str()) == 0) {
						subtitleEvent->Style = i;
						break;
					}
				}
				break;
			}

			default:
				break;
			}
		}
			SDL_Log("[SD-SUBTITLES] track events=%d", subtitleTrack->n_events);
		// One subtitle at a time: a line ends when the next one begins, so libass
		// never stacks overlapping voice lines.
		{
				ASS_Track* track = subtitleTrack;
			for (int i = 0; i < track->n_events; ++i) {
				ASS_Event* current = track->events + i;
				long long nextStart = -1;
				for (int j = 0; j < track->n_events; ++j) {
					const ASS_Event* other = track->events + j;
					if (j == i || other->Start < current->Start ||
						(other->Start == current->Start && j < i)) {
						continue;
					}
					if (nextStart < 0 || other->Start < nextStart) {
						nextStart = other->Start;
					}
				}
				if (nextStart > current->Start &&
					current->Start + current->Duration > nextStart) {
					current->Duration = nextStart - current->Start;
				}
			}
		}

			PrepareChoicePrompt(gp, eventsFromScript, font);

		eventMutex = SDL_CreateMutex();
		if (eventMutex == nullptr) {
			throw std::runtime_error("Failed to create event mutex");
		}

		void** parms = static_cast<void**>(SDL_malloc(sizeof(void*) * 3));
		if (parms == nullptr) {
			SDL_DestroyMutex(eventMutex);
			eventMutex = nullptr;
			throw std::runtime_error("Failed to allocate EventManager params");
		}

		parms[0] = gameplay;
		parms[1] = gameCtx;
		parms[2] = this;

				try {
					ScopedSdlMutex taskGuard(gameCtx->taskLock);
					if (!taskGuard.Locked()) {
						throw std::runtime_error("Failed to lock School Days task pool");
					}
					auto* tasks =
						static_cast<std::vector<std::tuple<SDL_ThreadFunction, void*>>*>(
							gameCtx->processPoolTasks);
					const int initialEventResult = EventManager(parms);
				if (initialEventResult < 0 || tasks == nullptr) {
					throw std::runtime_error("Initial School Days event processing failed");
				}
				tasks->emplace_back(EventManager, parms);
			} catch (...) {
				SDL_free(parms);
				if (eventMutex != nullptr) {
					SDL_DestroyMutex(eventMutex);
					eventMutex = nullptr;
				}
				throw;
			}

			// Publish the fully-built subtitle track only after every constructor
			// step that can fail has succeeded.  The runtime owns it for exactly the
			// lifetime of this CurrentScene.
			gp->sb->track = subtitleTrack;
			subtitleOwner_ = gp->sb;
			ownedSubtitleTrack_ = subtitleTrackGuard.release();
				orsGuard.Release();
	}

			bool SchoolDaysEventRuntime::ProcessNow(void* gameplay, struct Kotonoha_Game* gameCtx) {
				if (gameplay == nullptr || gameCtx == nullptr ||
					gameCtx->taskLock == nullptr || eventMutex == nullptr) {
					return false;
				}

				// The background task invokes this same EventManager while holding
				// taskLock.  Serialize the first-frame prime with that worker and use
				// stack parameters so task ownership/lifetime remains unchanged.
				void* parms[3] = {gameplay, gameCtx, this};
					ScopedSdlMutex taskGuard(gameCtx->taskLock);
					if (!taskGuard.Locked()) return false;
					const int result = EventManager(parms);
					return result >= 0 && !HasFatalError();
			}

	bool SchoolDaysEventRuntime::CheckEnd(void* gameplay) {
		auto* gp = static_cast<Gameplay*>(gameplay);
		if (gp == nullptr || gp->tm == nullptr) {
			return false;
		}
			/* EventManager owns the mutable timeline cursor.  Publishing readiness
			   after AdvanceTo lets routing stay non-blocking while preserving the
			   worker/timeline as the only authority that the scene may end. */
			return readyToEnd.load(std::memory_order_acquire);
	}

		bool SchoolDaysEventRuntime::ReachedNext(void* gameplay) const {
			auto* gp = static_cast<Gameplay*>(gameplay);
			if (gp == nullptr || gp->tm == nullptr) return false;
				return nextBoundaryTick && Kotonoha_MillisecondsToSceneTick(
					Kotonoha_timeGet(gp->tm)) >= *nextBoundaryTick;
		}


std::vector<MediaBufferInfo>
SchoolDaysEventRuntime::GetMediaBufferInfo(const Audio *audio, const Video *video,
                          const Image *image) const {
  std::vector<MediaBufferInfo> result;
  if (eventMutex == nullptr) return result;
  ScopedSdlMutex eventGuard(eventMutex);
  if (!eventGuard.Locked()) return result;
  try {
    if (audio != nullptr) {
      auto buffers = audio->GetBufferInfo();
      result.insert(result.end(), std::make_move_iterator(buffers.begin()),
                    std::make_move_iterator(buffers.end()));
    }
    if (video != nullptr) {
      auto buffers = video->GetBufferInfo();
      result.insert(result.end(), std::make_move_iterator(buffers.begin()),
                    std::make_move_iterator(buffers.end()));
    }
    if (image != nullptr) {
      auto buffers = image->GetBufferInfo();
      result.insert(result.end(), std::make_move_iterator(buffers.begin()),
                    std::make_move_iterator(buffers.end()));
    }
  } catch (...) {
    fatalError.store(true, std::memory_order_release);
    result.clear();
  }
  return result;
}

bool SchoolDaysEventRuntime::PlayMediaNow(Audio *audio, const std::string &path,
                         const std::string &channel) const {
  if (eventMutex == nullptr || audio == nullptr) return false;
  ScopedSdlMutex eventGuard(eventMutex);
  if (!eventGuard.Locked()) return false;
  try {
    return audio->PlayNow(path, channel);
  } catch (...) {
    fatalError.store(true, std::memory_order_release);
    return false;
  }
}

void SchoolDaysEventRuntime::StopMediaNow(Audio *audio) const {
  if (eventMutex == nullptr || audio == nullptr) return;
  ScopedSdlMutex eventGuard(eventMutex);
  if (!eventGuard.Locked()) return;
  try {
    audio->StopPreview();
  } catch (...) {
    fatalError.store(true, std::memory_order_release);
  }
}

std::string SchoolDaysEventRuntime::GetMediaNowPath(Audio *audio) const {
  if (eventMutex == nullptr || audio == nullptr) return {};
  ScopedSdlMutex eventGuard(eventMutex);
  if (!eventGuard.Locked()) return {};
  try {
    return audio->GetPreviewPath();
  } catch (...) {
    fatalError.store(true, std::memory_order_release);
    return {};
  }
}

	SchoolDaysEventRuntime::~SchoolDaysEventRuntime() {
			if (subtitleOwner_ != nullptr &&
				subtitleOwner_->track == ownedSubtitleTrack_) {
				subtitleOwner_->track = nullptr;
			}
			if (ownedSubtitleTrack_ != nullptr) {
				ass_free_track(ownedSubtitleTrack_);
				ownedSubtitleTrack_ = nullptr;
			}
			subtitleOwner_ = nullptr;
		Kotonoha_OrsClean(&eventsFromScript);
		if (eventMutex != nullptr) {
			SDL_DestroyMutex(eventMutex);
			eventMutex = nullptr;
		}
	}

namespace {

GameplayEventRuntime *CreateSchoolDaysEventRuntime(
    const char *scriptPath, void *gameplay, Kotonoha_Game *gameCtx,
    TTF_Font *font, void *) {
  try {
    return new SchoolDaysEventRuntime(scriptPath, gameplay, gameCtx, font);
  } catch (...) {
    return nullptr;
  }
}

void DestroySchoolDaysEventRuntime(GameplayEventRuntime *runtime, void *) {
  delete static_cast<SchoolDaysEventRuntime *>(runtime);
}

} // namespace

GameplayEventRuntimeFactory MakeSchoolDaysEventRuntimeFactory() {
  GameplayEventRuntimeFactory factory;
  factory.create = &CreateSchoolDaysEventRuntime;
  factory.destroy = &DestroySchoolDaysEventRuntime;
  return factory;
}

} // namespace Kotonoha
