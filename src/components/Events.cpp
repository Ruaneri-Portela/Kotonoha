#include <cctype>
#include <Kotonoha/components/Events.hpp>
#include <Kotonoha/Gameplay.hpp>
#include <Kotonoha/utils/OrsTime.h>
#include <Kotonoha/utils/VoicePcm.h>
#include <SDL3/SDL.h>
#include <sstream>
#include <string>
#include <memory>
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

		static void ReplaceActiveSubtitlesAt(ASS_Track* track, int nextEvent,
			long long nextStart) {
			if (track == nullptr) {
				return;
			}

			for (int i = 0; i < nextEvent; ++i) {
				ASS_Event* previous = &track->events[i];
				if (previous->Start > nextStart) {
					continue;
				}

				const long long elapsed = nextStart - previous->Start;
				if (previous->Duration > elapsed) {
					previous->Duration = elapsed;
				}
			}
		}

		static std::string ToUpper(const std::string& str) {
			std::string upperStr;
			upperStr.reserve(str.size());

			for (unsigned char c : str) {
				upperStr += static_cast<char>(std::toupper(c));
			}

			return upperStr;
		}

		static bool FileExists(const std::string& path) {
			SDL_IOStream* file = SDL_IOFromFile(path.c_str(), "rb");
			if (file == nullptr) {
				return false;
			}

			SDL_CloseIO(file);
			return true;
		}

		static std::string ResolveAssetPath(
			const char* assetsPath, const std::string& logicalPath) {
			std::string current =
				assetsPath != nullptr && *assetsPath != '\0' ? assetsPath : ".";
			size_t position = 0;
			while (position < logicalPath.size()) {
				const size_t separator = logicalPath.find('/', position);
				const std::string component =
					logicalPath.substr(position, separator - position);
				struct Match {
					const std::string* wanted;
					std::string exact;
					std::string folded;
				} match{ &component, "", "" };

				SDL_EnumerateDirectory(current.c_str(),
					[](void* opaque, const char*, const char* name) {
						auto* match = static_cast<Match*>(opaque);
						if (match->wanted->compare(name) == 0) {
							match->exact = name;
							return SDL_ENUM_SUCCESS;
						}
						if (match->folded.empty() &&
							SDL_strcasecmp(name, match->wanted->c_str()) == 0) {
							match->folded = name;
						}
						return SDL_ENUM_CONTINUE;
					}, &match);

				const std::string& resolved = !match.exact.empty()
					? match.exact
					: !match.folded.empty() ? match.folded : component;
				if (!current.empty() && current.back() != '/' &&
					current.back() != '\\') {
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

		static void DestroyEventManagerParams(void** parms) {
			if (parms == nullptr) {
				return;
			}
			SDL_free(parms);
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
			DestroyEventManagerParams(parms);
			return -1;
		}

		bool useExtension = (gameCtx->assetsPath != nullptr);
		const char* assetsPath = useExtension ? gameCtx->assetsPath : "";

		SDL_LockMutex(classUp->eventMutex);
		if (gameplay->tm == nullptr || !gameplay->tm->started) {
			SDL_UnlockMutex(classUp->eventMutex);
			return 0;
		}

		for (auto* event = classUp->eventsFromScript.data; event != nullptr;
			event = event->next) {
			const Uint64 actualTime = Kotonoha_timeGet(gameplay->tm);
			Uint64 startMs = 0;
			Uint64 endMs = 0;

			if (event->eventTouched) {
				continue;
			}

			if (!Kotonoha_OrsTimeToMilliseconds(
					event->start, true, &startMs) ||
				!Kotonoha_OrsTimeToMilliseconds(
					event->end, false, &endMs)) {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Skipping ORS event with invalid 24-fps timestamp "
					"start=%llu end=%llu",
					static_cast<unsigned long long>(event->start),
					static_cast<unsigned long long>(event->end));
				event->eventTouched = true;
				continue;
			}

			if (startMs > actualTime && startMs - actualTime > 10000) {
				continue;
			}

			event->eventTouched = true;

			if (endMs < actualTime) {
				continue;
			}

			switch (event->command) {
			case PLAY_VOICE: {
				if (event->data.play_voice->path != nullptr &&
					SDL_strlen(event->data.play_voice->path) > 0) {
					const std::string voiceLogical = BuildString(
						event->data.play_voice->path, "",
						useExtension ? ".OGG" : "");
					const std::string voicePath = useExtension
						? ResolveAssetPath(assetsPath, voiceLogical)
						: voiceLogical;
					gameplay->audio->AddMedia(
						voicePath.c_str(),
						startMs,
						endMs > SDL_MAX_UINT64 - 1000
							? SDL_MAX_UINT64 : endMs + 1000,
						false,
						"Voice");

					const char* key =
						event->data.play_voice->character_short;
					if (key != nullptr && *key != '\0') {
						int16_t* decodedPcm = nullptr;
						size_t decodedSampleCount = 0;
						if (Kotonoha_DecodeVoicePcm(voicePath.c_str(),
								&decodedPcm, &decodedSampleCount)) {
							std::unique_ptr<int16_t, decltype(&SDL_free)>
								pcmOwner(decodedPcm, SDL_free);
							const std::vector<int16_t> pcm(
								pcmOwner.get(),
								pcmOwner.get() + decodedSampleCount);
							VoiceAnimation animation{};
							animation.activity = AnalyzeVoiceActivity(pcm);
							Kotonoha_OrsTimeToZeroBasedFrame(
								event->start, true, &animation.startTick);
							classUp->voiceAnimations[event] =
								std::move(animation);
						}
					}
				}
				break;
			}

			case PLAY_SE:
				if (event->data.play_se->path != nullptr &&
					SDL_strlen(event->data.play_se->path) > 0) {
					gameplay->audio->AddMedia(
						BuildString(event->data.play_se->path,
							assetsPath,
							useExtension ? ".OGG" : "")
						.c_str(),
						startMs,
						endMs,
						true,
						"Se");
				}
				break;

			case PLAY_BGM:
				if (event->data.path_end->path != nullptr &&
					SDL_strlen(event->data.path_end->path) > 0) {
					std::string str = ToUpper(event->data.path_end->path);
					if (useExtension) {
						const std::string introPath =
							ResolveAssetPath(assetsPath,
								BuildString(str.c_str(), "", "_INT.OGG"));
						const std::string loopPath =
							ResolveAssetPath(assetsPath,
								BuildString(str.c_str(), "", "_LOOP.OGG"));
						if (FileExists(introPath) && FileExists(loopPath)) {
							gameplay->audio->AddIntroLoopMedia(
								introPath.c_str(),
								loopPath.c_str(),
								startMs,
								endMs,
								"BGM");
						}
						else {
							gameplay->audio->AddMedia(
								loopPath.c_str(),
								startMs,
								endMs,
								true,
								"BGM");
						}
					}
					else {
						gameplay->audio->AddMedia(
							event->data.path_end->path,
							startMs,
							endMs,
							true,
							"BGM");
					}
				}
				break;

			case END_BGM:
				if (event->data.path_end->path != nullptr &&
					SDL_strlen(event->data.path_end->path) > 0) {
					std::string str = ToUpper(event->data.path_end->path);
					gameplay->audio->AddMedia(
						BuildString(useExtension ? str.c_str()
							: event->data.path_end->path,
							assetsPath,
							useExtension ? ".OGG" : "")
						.c_str(),
						startMs,
						endMs,
						true,
						"BGM");
				}
				break;

			case END_ROLL:
				if (event->data.path_end->path != nullptr &&
					SDL_strlen(event->data.path_end->path) > 0) {
					gameplay->video->Register(
						BuildString(event->data.path_end->path,
							assetsPath,
							useExtension ? ".WMV" : "")
						.c_str(),
						startMs,
						endMs);
				}
				break;

			case PLAY_MOVIE:
				if (event->data.play_movie->path != nullptr &&
					SDL_strlen(event->data.play_movie->path) > 0) {
					const std::string moviePath =
						BuildString(event->data.play_movie->path,
							assetsPath,
							useExtension ? ".WMV" : "");

					Uint64 startFrame = 0;
					Uint64 endFrame = 0;
					const bool startMapped = Kotonoha_OrsTimeToTick(
						event->start, true, &startFrame);
					const bool endMapped = Kotonoha_OrsTimeToTick(
						event->end, false, &endFrame);

					if (startMapped && endMapped && endFrame >= startFrame) {
						gameplay->video->Register(
							moviePath.c_str(),
							startMs,
							endMs,
							true,
							endFrame);
					}
					else {
						gameplay->video->Register(
							moviePath.c_str(),
							startMs,
							endMs > SDL_MAX_UINT64 - 50
								? SDL_MAX_UINT64 : endMs + 50);
					}
				}
				break;

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

		const Uint64 currentTime = Kotonoha_timeGet(gameplay->tm);
		Uint64 currentTick = Kotonoha_MillisecondsToOrsTick(currentTime) - 1;
		for (auto it = classUp->voiceAnimations.begin();
			it != classUp->voiceAnimations.end();) {
			const Kotonoha_orsEvent* event = it->first;
			Uint64 startMs = 0;
			Uint64 endMs = 0;
			if (event == nullptr || event->data.play_voice == nullptr ||
				!Kotonoha_OrsTimeToMilliseconds(
					event->start, true, &startMs) ||
				!Kotonoha_OrsTimeToMilliseconds(
					event->end, false, &endMs) ||
				currentTime >= endMs) {
				it = classUp->voiceAnimations.erase(it);
				continue;
			}
			if (currentTime < startMs) {
				++it;
				continue;
			}

			auto& animation = it->second;
			if (currentTick < animation.startTick) {
				++it;
				continue;
			}
			if (animation.state.initialized &&
				currentTick < animation.state.lastTick) {
				animation.state = MouthAnimationState{};
			}
			const Uint64 firstTick = animation.state.initialized
				? animation.state.lastTick + 1
				: animation.startTick;
			for (Uint64 tick = firstTick; tick <= currentTick; ++tick) {
				AdvanceMouthAnimation(
					animation.state, animation.activity,
					animation.startTick, tick);
			}
			animation.state.lastTick = currentTick;
			animation.state.initialized = true;
			gameplay->image->SetAbcState(
				event->data.play_voice->character_short,
				animation.state.index, startMs, endMs);
			++it;
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

		gp->video->Reset();
		gp->image->Reset();
		gp->audio->RemoveMedia(nullptr);
		voiceAnimations.clear();

		Uint64 actualTime = Kotonoha_timeGet(gp->tm);

		for (auto* event = this->eventsFromScript.data; event != nullptr;
			event = event->next) {
			Uint64 endMs = 0;
			if (!Kotonoha_OrsTimeToMilliseconds(
					event->end, false, &endMs)) {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
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

	Event::Event(const char* orsPath, void* gameplay, struct Kotonoha_Game* gameCtx)
		: eventMutex(nullptr), lastTime(0) {
		eventsFromScript = Kotonoha_OrsParser(orsPath);

		if (eventsFromScript.size == 0) {
			throw std::runtime_error("Ors invalid");
		}

		auto* gp = static_cast<Gameplay*>(gameplay);
		if (gp == nullptr || gameCtx == nullptr) {
			Kotonoha_OrsClean(&eventsFromScript);
			throw std::runtime_error("Invalid gameplay context");
		}

		std::stringstream subSs;
		subSs << "[Script Info]\nTitle:" << orsPath
			<< "\nScriptType: v4.00+\nWrapStyle: 0\nScaledBorderAndShadow: yes\n"
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
				Uint64 startMs = 0;
				Uint64 endMs = 0;
				if (!Kotonoha_OrsTimeToMilliseconds(
						event->start, true, &startMs) ||
					!Kotonoha_OrsTimeToMilliseconds(
						event->end, false, &endMs) ||
					endMs < startMs) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Invalid subtitle timestamp range");
					throw std::runtime_error("Invalid subtitle timestamp range");
				}
				const int eventId = ass_alloc_event(gp->sb->track);
				if (eventId < 0) {
					throw std::runtime_error("Failed to allocate subtitle event");
				}
				ASS_Event* subtitleEvent = &gp->sb->track->events[eventId];
				subtitleEvent->Start = static_cast<long long>(startMs);
				subtitleEvent->Duration =
					static_cast<long long>(endMs - startMs);
				ReplaceActiveSubtitlesAt(gp->sb->track, eventId, startMs);
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
				Uint64 startMs = 0;
				Uint64 endMs = 0;
				if (!Kotonoha_OrsTimeToMilliseconds(
						event->start, true, &startMs) ||
					!Kotonoha_OrsTimeToMilliseconds(
						event->end, false, &endMs)) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Invalid choice prompt timestamp range");
					throw std::runtime_error("Invalid choice prompt timestamps");
				}
				std::vector<std::string> options;
				for (char** it = event->data.set_select->options; *it != nullptr; ++it) {
					options.push_back(BuildString(*it));
				}

				gp->prompt = new Prompt(
					options, &gp->promptId, startMs, endMs, gp->tm);
				gp->putPrompt = true;
				break;
			}

			case SkipFRAME:
			case Next: {
				if (!Kotonoha_OrsTimeToMilliseconds(
						event->start, true, &lastTime)) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Invalid scene end timestamp");
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
			return true;
		}

		return Kotonoha_timeGet(gp->tm) > lastTime;
	}

	Event::~Event() {
		Kotonoha_OrsClean(&eventsFromScript);
		if (eventMutex != nullptr) {
			SDL_DestroyMutex(eventMutex);
			eventMutex = nullptr;
		}
	}

} // namespace Kotonoha