#include <cctype>
#include <Kotonoha/components/Events.hpp>
#include <Kotonoha/Gameplay.hpp>
#include <SDL3/SDL.h>
#include <sstream>
#include <string>
#include <tuple>
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

		static std::string ToUpper(const std::string& str) {
			std::string upperStr;
			upperStr.reserve(str.size());

			for (unsigned char c : str) {
				upperStr += static_cast<char>(std::toupper(c));
			}

			return upperStr;
		}

		static void DestroyEventManagerParams(void** parms) {
			if (parms == nullptr) {
				return;
			}

			delete static_cast<std::vector<std::tuple<std::string, int>>*>(parms[3]);
			delete static_cast<std::string*>(parms[4]);
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
		auto* object =
			static_cast<std::vector<std::tuple<std::string, int>>*>(parms[3]);
		auto* lastCreateBg = static_cast<std::string*>(parms[4]);

		if (gameplay == nullptr || gameCtx == nullptr || classUp == nullptr ||
			object == nullptr || lastCreateBg == nullptr ||
			classUp->eventMutex == nullptr) {
			DestroyEventManagerParams(parms);
			return -1;
		}

		std::string prevLastCreateBg = *lastCreateBg;
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

			if (*lastCreateBg != prevLastCreateBg) {
				object->clear();
				prevLastCreateBg = *lastCreateBg;
			}
			if (endMs < actualTime) continue;

			switch (event->command) {
			case PLAY_VOICE: {
				if (event->data.play_voice->path != nullptr &&
					SDL_strlen(event->data.play_voice->path) > 0) {
					gameplay->audio->AddMedia(
						BuildString(event->data.play_voice->path,
							assetsPath,
							useExtension ? ".OGG" : "")
						.c_str(),
						startMs,
						endMs + 1000,
						false,
						"Voice");

					if (!lastCreateBg->empty() &&
						event->data.play_voice->character_short != nullptr) {
						std::string character =
							ToUpper(event->data.play_voice->character_short);
						int searchImgId = 0;
						bool found = false;

						for (auto& it : *object) {
							if (std::get<0>(it) == character) {
								++std::get<1>(it);
								searchImgId = std::get<1>(it);
								found = true;
								break;
							}
						}

						if (!found) {
							object->emplace_back(character, 0);
						}

						const char suffix = static_cast<char>('A' + searchImgId);
						const std::string pathImg = *lastCreateBg + character + "." + suffix;
						const std::string path =
							BuildString(pathImg.c_str(), assetsPath,
								useExtension ? ".PNG" : "");

						SDL_IOStream* file = SDL_IOFromFile(path.c_str(), "rb");
						if (file != nullptr) {
							SDL_CloseIO(file);
							gameplay->image->Register(path.c_str(),
								startMs,
								endMs,
								1);
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
					gameplay->audio->AddMedia(
						BuildString(useExtension ? str.c_str()
							: event->data.path_end->path,
							assetsPath,
							useExtension ? "_LOOP.OGG" : "")
						.c_str(),
						startMs,
						endMs,
						true,
						"BGM");
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

					gameplay->video->Register(
						moviePath.c_str(),
						startMs,
						endMs,
						true,
						event->endTick + 1);
				}
				break;

			case CREATE_BG:
				if (event->data.create_bg->path != nullptr &&
					SDL_strlen(event->data.create_bg->path) > 0) {
					*lastCreateBg = event->data.create_bg->path;
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
		classUp->timeline.AdvanceTo(currentTick);

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

		if (gp->tm != nullptr && Kotonoha_timeIsStarted(gp->tm)) {
			const Uint64 actualTime = Kotonoha_timeGet(gp->tm);
			const Kotonoha_SceneTick target =
				Kotonoha_MillisecondsToSceneTick(actualTime);
			timeline.RebuildActiveAt(target);
			for (auto* event = eventsFromScript.data; event != nullptr;
				event = event->next) {
				event->eventPrepared =
					Kotonoha_SceneTickToMillisecondsCeil(event->endTick) <= actualTime;
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

		void** parms = static_cast<void**>(SDL_malloc(sizeof(void*) * 5));
		if (parms == nullptr) {
			Kotonoha_OrsClean(&eventsFromScript);
			SDL_DestroyMutex(eventMutex);
			eventMutex = nullptr;
			throw std::runtime_error("Failed to allocate EventManager params");
		}

		parms[0] = gameplay;
		parms[1] = gameCtx;
		parms[2] = this;
		parms[3] = new std::vector<std::tuple<std::string, int>>();
		parms[4] = new std::string();

		if (parms[3] == nullptr || parms[4] == nullptr) {
			DestroyEventManagerParams(parms);
			Kotonoha_OrsClean(&eventsFromScript);
			SDL_DestroyMutex(eventMutex);
			eventMutex = nullptr;
			throw std::runtime_error("Failed to allocate EventManager state");
		}

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
