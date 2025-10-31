#include <codecvt>
#include <iomanip>
#include <Kotonoha/components/Events.hpp>
#include <Kotonoha/Gameplay.hpp>
#include <locale>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace Kotonoha
{

	// Função para converter wchar_t* para string UTF-8 com prefixo e sufixo opcionais
	static std::string ConvertWCharToChar(const wchar_t *wideStr, const std::string &prefix = "", const std::string &suffix = "")
	{
		if (wideStr == nullptr)
			return "";

		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		std::string buffer = converter.to_bytes(wideStr);

		return prefix + buffer + suffix;
	}

	// Função para converter uma string para maiúsculas
	std::wstring toUpper(const std::wstring &str)
	{
		std::wstring upperStr;
		upperStr.reserve(str.size());

		for (wchar_t c : str)
		{
			upperStr += SDL_toupper(c);
		}

		return upperStr;
	}

	// Gerenciador de eventos em thread
	int Event::EventManager(void *data)
	{
		void **parms = static_cast<void **>(data);
		auto *gameplay = static_cast<Gameplay *>(parms[0]);
		auto *gameCtx = static_cast<struct Kotonoha_Game *>(parms[1]);
		auto *classUp = static_cast<Event *>(parms[2]);
		auto *object = static_cast<std::vector<std::tuple<std::wstring, int>> *>(parms[3]);
		auto *lastCreateBg = static_cast<std::wstring *>(parms[4]);

		std::wstring prevLastCreateBg = *lastCreateBg;
		bool useExtension = true;
		char *assetsPath = gameCtx->assetsPath;
		if (gameCtx->assetsPath == NULL)
		{
			useExtension = false;
			assetsPath = (char *)"";
		}

		if (!classUp->inExit)
		{
			for (auto *event = classUp->eventsFromScript.data; event != nullptr; event = event->next)
			{
				if (Kotonoha_timeGet(gameplay->tm) + 10000 < event->start || event->eventTouched)
					continue;

				event->eventTouched = true;

				if (*lastCreateBg != prevLastCreateBg)
				{
					object->clear();
					prevLastCreateBg = *lastCreateBg;
				}

				switch (event->command)
				{
				case PLAY_VOICE:
				{
					if (event->data.play_voice->path != nullptr && SDL_wcslen(event->data.play_voice->path) > 0)
					{
						gameplay->audio->AddMedia(
							ConvertWCharToChar(event->data.play_voice->path, assetsPath,
											   useExtension ? ".OPUS" : "")
								.c_str(),
							event->start, event->data.play_voice->end, false, "Voice");
						if (!lastCreateBg->empty() && event->data.play_voice->character_short != nullptr)
						{
							std::wstring character = toUpper(event->data.play_voice->character_short);
							int searchImgId = 0;
							bool found = false;

							// Procura se o character já está no objeto
							for (auto &it : *object)
							{
								if (std::get<0>(it) == character)
								{
									std::get<1>(it)++;
									searchImgId = std::get<1>(it);
									found = true;
									break;
								}
							}

							if (!found)
							{
								object->emplace_back(character, 0);
							}

							wchar_t a = 'A' + searchImgId;
							std::wstring pathImg = *lastCreateBg + character + L"." + a;
							std::string path = ConvertWCharToChar(pathImg.c_str(), assetsPath, useExtension ? ".PNG" : "");
							SDL_IOStream *file = SDL_IOFromFile(path.c_str(), "rb");
							if (file != nullptr)
							{
								SDL_CloseIO(file);
								gameplay->image->Register(path.c_str(), event->start, event->data.play_voice->end, 1);
							}
						}
					}
					break;
				}
				case PLAY_SE:
					if (event->data.play_se->path != nullptr && SDL_wcslen(event->data.play_se->path) > 0)
					{
						gameplay->audio->AddMedia(
							ConvertWCharToChar(event->data.play_se->path, assetsPath,
											   useExtension ? ".OPUS" : "")
								.c_str(),
							event->start, event->data.play_se->end, true, "Se");
					}
					break;
				case PLAY_BGM:
					if (event->data.path_end->path != nullptr && SDL_wcslen(event->data.path_end->path) > 0)
					{
						std::wstring str = toUpper(event->data.path_end->path);
						gameplay->audio->AddMedia(ConvertWCharToChar(
													  useExtension ? str.c_str() : event->data.path_end->path, assetsPath,
													  useExtension ? "_LOOP.OPUS" : "")
													  .c_str(),
												  event->start,
												  event->data.path_end->end, true, "BGM");
					}
					break;
				case END_BGM:
					if (event->data.path_end->path != nullptr && SDL_wcslen(event->data.path_end->path) > 0)
					{
						std::wstring str = toUpper(event->data.path_end->path);
						gameplay->audio->AddMedia(ConvertWCharToChar(
													  useExtension ? str.c_str() : event->data.path_end->path, assetsPath,
													  useExtension ? ".OPUS" : "")
													  .c_str(),
												  event->start,
												  event->data.path_end->end, true, "BGM");
					}
					break;
				case END_ROLL:
					if (event->data.path_end->path != nullptr && SDL_wcslen(event->data.path_end->path) > 0)
						gameplay->video->Register(
							ConvertWCharToChar(event->data.path_end->path, assetsPath,
											   useExtension ? ".MP4" : "")
								.c_str(),
							event->start, event->data.path_end->end);
				case PLAY_MOVIE:
					if (event->data.play_movie->path != nullptr && SDL_wcslen(event->data.play_movie->path) > 0)
						gameplay->video->Register(
							ConvertWCharToChar(event->data.play_movie->path, assetsPath,
											   useExtension ? ".MP4" : "")
								.c_str(),
							event->start, event->data.play_movie->end);
					break;

				case CREATE_BG:
					if (event->data.create_bg->path != nullptr && SDL_wcslen(event->data.create_bg->path) > 0)
					{
						*lastCreateBg = event->data.create_bg->path;
						gameplay->image->Register(ConvertWCharToChar(event->data.create_bg->path, assetsPath, useExtension ? ".PNG" : "").c_str(), event->start, event->data.create_bg->end, 0);
					}
					break;

				default:
					break;
				}
			}
			return 0;
		}

		delete static_cast<std::vector<std::tuple<std::wstring, int>> *>(parms[3]);
		delete static_cast<std::wstring *>(parms[4]);
		classUp->closed = true;
		return -1;
	}

	// Construtor da classe Event
	Event::Event(const char *orsPath, void *gameplay, struct Kotonoha_Game *gameCtx)
	{
		eventsFromScript = Kotonoha_OrsParser(orsPath);

		std::stringstream subSs;
		subSs << "[Script Info]\nTitle:" << orsPath << "\nScriptType: v4.00+\nWrapStyle: 0\nScaledBorderAndShadow: yes\n"
			  << "YCbCr Matrix: None\n\n"
			  << (gameCtx->styleStr == nullptr ? "" : gameCtx->styleStr) << std::endl;

		static_cast<Gameplay *>(gameplay)->sb->track = ass_new_track(static_cast<Gameplay *>(gameplay)->sb->ass_library);
		ass_process_data(static_cast<Gameplay *>(gameplay)->sb->track, (char *)(subSs.str().c_str()),
						 static_cast<int>(subSs.str().size()));

		for (auto *event = eventsFromScript.data; event != nullptr; event = event->next)
		{
			switch (event->command)
			{
			case PRINT_TEXT:
			{
				ass_alloc_event(static_cast<Gameplay *>(gameplay)->sb->track);
				ASS_Event *subtitleEvent = static_cast<Gameplay *>(gameplay)->sb->track->events;
				subtitleEvent += static_cast<Gameplay *>(gameplay)->sb->track->n_events - 1;
				subtitleEvent->Start = event->start;
				subtitleEvent->Duration = event->data.print_text->end - event->start;
				subtitleEvent->Text = SDL_strdup(ConvertWCharToChar(event->data.print_text->text).c_str());
				for (int i = 0; i < static_cast<Gameplay *>(gameplay)->sb->track->n_styles; i++)
				{
					ASS_Style *style = static_cast<Gameplay *>(gameplay)->sb->track->styles + i;
					if (SDL_strcmp(style->Name, "Default") == 0)
						subtitleEvent->Style = i;
					if (SDL_strcmp(style->Name,
								   ConvertWCharToChar(event->data.print_text->character).c_str()) ==
						0)
					{
						subtitleEvent->Style = i;
						break;
					}
				}
				break;
			}
			case SetSELECT:
			{
				std::vector<std::string> options;
				for (wchar_t **it = event->data.set_select->options; *it != nullptr; ++it)
				{
					options.push_back(ConvertWCharToChar(*it));
				}
				static_cast<Gameplay *>(gameplay)->prompt = new Prompt(options, &static_cast<Gameplay *>(gameplay)->promptId, event->start, event->data.set_select->end, static_cast<Gameplay *>(gameplay)->tm);
				static_cast<Gameplay *>(gameplay)->putPrompt = true;
				break;
			}

			case SkipFRAME:
			case Next:
				lastTime = event->start;
				break;

			default:
				break;
			}
		}

		SDL_LockMutex(gameCtx->taskLock);
		auto *tasks = static_cast<std::vector<std::tuple<SDL_ThreadFunction, void *>> *>(gameCtx->processPoolTasks);
		void **parms = static_cast<void **>(SDL_malloc(sizeof(void *) * 5));
		parms[0] = gameplay;
		parms[1] = gameCtx;
		parms[2] = this;
		parms[3] = new std::vector<std::tuple<std::wstring, int>>();
		parms[4] = new std::wstring();
		if (parms[3] == nullptr || parms[4] == nullptr)
		{
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for EventManager");
			SDL_UnlockMutex(gameCtx->taskLock);
			return;
		}
		EventManager(parms);
		tasks->emplace_back(EventManager, parms);
		SDL_UnlockMutex(gameCtx->taskLock);
	}

	// Verifica o final dos eventos
	bool Event::CheckEnd(void *gameplay)
	{
		return Kotonoha_timeGet(static_cast<Gameplay *>(gameplay)->tm) > lastTime;
	}

	// Destrutor da classe Event
	Event::~Event()
	{
		inExit = true;
		while (!closed)
			continue;
		Kotonoha_OrsClean(&eventsFromScript);
	}
}