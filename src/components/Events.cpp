#include <Kotonoha/components/Events.hpp>
#include <Kotonoha/SchoolDaysBgmAssetResolver.hpp>
#include <Kotonoha/SchoolDaysVoicePcm.hpp>
#include <Kotonoha/SchoolDaysMovieAssetResolver.hpp>
#include <Kotonoha/Gameplay.hpp>
#include <SDL3/SDL.h>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace Kotonoha {

	namespace {
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


	} // namespace

	int Event::EventManager(void* data) {
		void** parms = static_cast<void**>(data);
		if (parms == nullptr) {
			return -1;
		}

		auto* gameplay = static_cast<Gameplay*>(parms[0]);
		auto* gameCtx = static_cast<struct Kotonoha_Game*>(parms[1]);
		auto* classUp = static_cast<Event*>(parms[2]);
		if (gameplay == nullptr || gameCtx == nullptr || classUp == nullptr ||
			classUp->eventMutex == nullptr) {
			return -1;
		}

		bool useExtension = (gameCtx->assetsPath != nullptr);
		const char* assetsPath = useExtension ? gameCtx->assetsPath : "";

		SDL_LockMutex(classUp->eventMutex);
		if (gameplay->tm == nullptr || !gameplay->tm->started) {
			SDL_UnlockMutex(classUp->eventMutex);
			return 0;
		}

		const Uint64 actualTime = Kotonoha_timeGet(gameplay->tm);
		const Kotonoha_SceneTick currentTick =
			Kotonoha_MillisecondsToSceneTick(actualTime);
		/* Registration may prepare media early; semantic dispatch stays on the
		   real scene tick. The old runtime used a 10-second preparation window. */
		Kotonoha_SceneTick preparationLimit =
			Kotonoha_MillisecondsToSceneTick(actualTime + 10000);
		const SchoolDaysOptionalTick nextTick = classUp->timeline.NextTick();
		if (nextTick && preparationLimit > *nextTick)
			preparationLimit = *nextTick;
		for (auto* event = classUp->eventsFromScript.data;
			event != nullptr && event->startTick <= preparationLimit;
			event = event->next) {
			if (event->eventPrepared) continue;
			event->eventPrepared = true;
			const Uint64 startMs =
				Kotonoha_SceneTickToMillisecondsCeil(event->startTick);
			const Uint64 endMs =
				Kotonoha_SceneTickToMillisecondsCeil(event->endTick);

			if (endMs < actualTime) continue;

			switch (event->command) {
			case PLAY_VOICE: {
				if (event->data.play_voice->path != nullptr &&
					SDL_strlen(event->data.play_voice->path) > 0) {
					const std::string voicePath = BuildString(
						event->data.play_voice->path, assetsPath,
						useExtension ? ".OGG" : "");
					classUp->voiceMedia[event] = gameplay->audio->AddMedia(
						voicePath.c_str(),
						startMs,
						endMs + 1000,
						false,
						"Voice", false);
					if (classUp->voiceMedia[event] != nullptr &&
						*SchoolDaysVoiceAnimationKey(event) != '\0') {
						std::vector<int16_t> pcm;
						if (DecodeSchoolDaysVoicePcm(voicePath, pcm))
							classUp->voiceActivity[event] = AnalyzeSchoolDaysPcm(pcm);
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
					classUp->seMedia[event] = gameplay->audio->AddMedia(
						BuildString(event->data.play_se->path,
							assetsPath,
							useExtension ? ".OGG" : "")
						.c_str(),
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
					const std::string intro = ResolveSchoolDaysBgmAsset(assetsPath, resources.intro);
					const std::string loop = ResolveSchoolDaysBgmAsset(assetsPath, resources.loop);
					classUp->normalBgmMedia[event] = gameplay->audio->AddIntroLoopMedia(
						intro.c_str(), loop.c_str(), startMs, endMs);
				}
				break;

			case END_BGM:
				if (event->data.path_end->path != nullptr &&
					SDL_strlen(event->data.path_end->path) > 0) {
					const auto resources = SchoolDaysBgmResources::Ending(
						event->data.path_end->path);
					const std::string path = ResolveSchoolDaysBgmAsset(assetsPath, resources.oneShot);
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
					const std::string resolved = ResolveSchoolDaysMovieAsset(
						assetsPath, state.resource);
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
					gameplay->image->Register(
						BuildString(event->data.create_bg->path,
							assetsPath,
							useExtension ? ".PNG" : "")
						.c_str(),
						startMs,
						endMs,
						0);
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
		}
		for (auto* event : classUp->timeline.AdvanceTo(currentTick)) {
			if (event->command == PLAY_VOICE) {
				const bool allowed = SchoolDaysVoiceAllowed(
					event->data.play_voice->a,
					Kotonoha_IsMenVoiceEnabled(gameCtx));
				auto found = classUp->voiceMedia.find(event);
				if (found == classUp->voiceMedia.end() || found->second == nullptr)
					continue; /* Missing media never blocks the timeline. */
				if (allowed) {
					gameplay->audio->SetMediaEnabled(found->second, true);
					const std::string key = AbcKey(SchoolDaysVoiceAnimationKey(event));
					if (!key.empty()) {
						auto* group = gameplay->image->FindAbcGroup(key,
							Kotonoha_SceneTickToMillisecondsCeil(event->startTick));
						if (group != nullptr) {
							auto previous = classUp->abcBindings.find(key);
							if (previous != classUp->abcBindings.end())
								gameplay->image->SelectAbcState(previous->second.group, 0);
							classUp->abcBindings[key] = {event, group, {}, 0, false};
						}
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
		for (auto& item : classUp->movieStates) {
			auto& state = item.second;
			if (!state.eof && gameplay->video->IsEof(item.first))
				state.MarkEof();
			if (currentTick >= state.endTick && !state.completed) {
				gameplay->video->Remove(item.first);
				state.AdvanceTo(currentTick);
			}
		}
		for (auto it = classUp->abcBindings.begin();
			it != classUp->abcBindings.end();) {
			auto& binding = it->second;
			if (currentTick >= binding.voice->endTick) {
				gameplay->image->SelectAbcState(binding.group, 0);
				it = classUp->abcBindings.erase(it);
				continue;
			}
			const auto activity = classUp->voiceActivity.find(binding.voice);
			const std::vector<SchoolDaysActivityRecord> empty;
			const auto& records = activity == classUp->voiceActivity.end()
				? empty : activity->second;
			const Kotonoha_SceneTick first = binding.initialized
				? binding.lastTick + 1 : binding.voice->startTick;
			for (Kotonoha_SceneTick tick = first; tick <= currentTick; ++tick) {
				const Uint8 before = binding.state.activeIndex;
				AdvanceSchoolDaysAbcState(binding.state, records,
					binding.voice->startTick, tick);
				if (before != binding.state.activeIndex)
					SDL_LogDebug(SDL_LOG_CATEGORY_RENDER,
						"[SD-ABC] key=%s tick=%llu activity=%d state=%c",
						it->first.c_str(), static_cast<unsigned long long>(tick),
						SchoolDaysActivityAt(records, tick - binding.voice->startTick) ? 1 : 0,
						'A' + binding.state.activeIndex);
			}
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

		SDL_UnlockMutex(classUp->eventMutex);
		return 0;
	}

	void Event::Reset(void* gameplay) {
		auto* gp = static_cast<Gameplay*>(gameplay);
		if (gp == nullptr || eventMutex == nullptr) {
			return;
		}

		SDL_LockMutex(eventMutex);

		abcBindings.clear();
		voiceActivity.clear();
		gp->video->Reset();
		movieStates.clear();
		gp->image->Reset();
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

		SDL_UnlockMutex(eventMutex);
	}

	Event::Event(const char* orsPath, void* gameplay, struct Kotonoha_Game* gameCtx)
		: eventMutex(nullptr), lastTime(0) {
		eventsFromScript = Kotonoha_OrsParser(orsPath);

		if (eventsFromScript.size == 0) {
			throw std::runtime_error("Ors invalid");
		}
		timeline.Bind(&eventsFromScript);
		if (timeline.NextTick()) {
			lastTime = Kotonoha_SceneTickToMillisecondsCeil(*timeline.NextTick());
		}

		auto* gp = static_cast<Gameplay*>(gameplay);
		if (gp == nullptr || gameCtx == nullptr) {
			Kotonoha_OrsClean(&eventsFromScript);
			throw std::runtime_error("Invalid gameplay context");
		}

		std::stringstream subSs;
		subSs << "[Script Info]\nTitle:" << orsPath
			<< "\nScriptType: v4.00+\nWrapStyle: yes\nScaledBorderAndShadow: yes\n"
			<< "YCbCr Matrix: None\n\n"
			<< (gameCtx->styleStr == nullptr ? "" : gameCtx->styleStr) << std::endl;

		gp->sb->track = ass_new_track(gp->sb->ass_library);

		ass_process_data(gp->sb->track,
			const_cast<char*>(subSs.str().c_str()),
			static_cast<int>(subSs.str().size()));

		for (auto* event = eventsFromScript.data; event != nullptr;
			event = event->next) {
			switch (event->command) {
			case PRINT_TEXT: {
				ass_alloc_event(gp->sb->track);
				ASS_Event* subtitleEvent = gp->sb->track->events + (gp->sb->track->n_events - 1);
				const Uint64 startMs = Kotonoha_SceneTickToMillisecondsCeil(event->startTick);
				const Uint64 endMs = Kotonoha_SceneTickToMillisecondsCeil(event->endTick);
				subtitleEvent->Start = startMs;
				subtitleEvent->Duration = endMs - startMs;
				subtitleEvent->Text =
					SDL_strdup(BuildString(event->data.print_text->text).c_str());

				for (int i = 0; i < gp->sb->track->n_styles; ++i) {
					ASS_Style* style = gp->sb->track->styles + i;

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

			case SetSELECT: {
				std::vector<std::string> options;
				for (char** it = event->data.set_select->options; *it != nullptr; ++it) {
					options.push_back(BuildString(*it));
				}

				gp->prompt = new Prompt(
					options, &gp->promptId,
					Kotonoha_SceneTickToMillisecondsCeil(event->startTick),
					Kotonoha_SceneTickToMillisecondsCeil(event->endTick), gp->tm);
				gp->putPrompt = true;
				break;
			}

			default:
				break;
			}
		}

		eventMutex = SDL_CreateMutex();
		if (eventMutex == nullptr) {
			Kotonoha_OrsClean(&eventsFromScript);
			throw std::runtime_error("Failed to create event mutex");
		}

		void** parms = static_cast<void**>(SDL_malloc(sizeof(void*) * 3));
		if (parms == nullptr) {
			Kotonoha_OrsClean(&eventsFromScript);
			SDL_DestroyMutex(eventMutex);
			eventMutex = nullptr;
			throw std::runtime_error("Failed to allocate EventManager params");
		}

		parms[0] = gameplay;
		parms[1] = gameCtx;
		parms[2] = this;

		SDL_LockMutex(gameCtx->taskLock);
		auto* tasks =
			static_cast<std::vector<std::tuple<SDL_ThreadFunction, void*>>*>(
				gameCtx->processPoolTasks);

		EventManager(parms);
		tasks->emplace_back(EventManager, parms);
		SDL_UnlockMutex(gameCtx->taskLock);
	}

	bool Event::CheckEnd(void* gameplay) {
		auto* gp = static_cast<Gameplay*>(gameplay);
		if (gp == nullptr || gp->tm == nullptr) {
			return false;
		}
		if (eventMutex == nullptr) return false;
		SDL_LockMutex(eventMutex);
		const bool done = timeline.ReadyToEnd(Kotonoha_MillisecondsToSceneTick(
			Kotonoha_timeGet(gp->tm)));
		SDL_UnlockMutex(eventMutex);
		return done;
	}

	Event::~Event() {
		Kotonoha_OrsClean(&eventsFromScript);
		if (eventMutex != nullptr) {
			SDL_DestroyMutex(eventMutex);
			eventMutex = nullptr;
		}
	}

} // namespace Kotonoha
