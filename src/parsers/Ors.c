#include <Kotonoha/parsers/Ors.h>

static wchar_t* Kotonoha_ORS_wchar_jump_tab(wchar_t* line)
{
	for (; *line != '\t' && *line; line++)
	{
		if (*line == L'\0')
		{
			return NULL;
		}
	}
	for (; *line == '\t'; line++)
	{
		if (*line == L'\0')
		{
			return NULL;
		}
	}
	return line;
}

static int Kotonoha_ORS_swap_tab_null(wchar_t* line, bool* transformed)
{
	int size = 0;
	for (;; line++)
	{
		size++;
		if (*line == L'\t' && !*transformed)
		{
			*line = L'\0';
			*transformed = true;
			break;
		}
		if (*line == L'\0')
		{
			if (*transformed)
			{
				*line = L'\t';
				*transformed = false;
				break;
			}
			break;
		}
	}
	return size;
}

static Uint64 Kotonoha_ORS_parse_timestamp(wchar_t* line)
{
	Uint64 time[3] = { 0 }; // Armazena horas, minutos e segundos
	for (int i = 0; i < 3; i++)
	{
		wchar_t* digit = wcschr(line, L':');
		if (digit != NULL)
		{
			*digit = L'\0';
			digit -= 2;
			time[i] = wcstoul(line, NULL, 10); // Converte parte do timestamp para inteiro
			digit += 2;
			*digit = L':';
			line = digit + 1;
		}
		else
		{
			time[i] = wcstoul(line, NULL, 10);
			break;
		}
	}

	return time[0] * 60000 + time[1] * 1000 + time[2] * 10; // Retorna o timestamp em milissegundos
}

static void Kotonoha_ORS_doDelete(struct Kotonoha_orsEvent* target)
{
	switch (target->command)
	{
	case CREATE_BG:
		SDL_free(target->data.create_bg->a);
		SDL_free(target->data.create_bg->path);
		SDL_free(target->data.create_bg);
		break;
	case PLAY_SE:
		SDL_free(target->data.play_se->path);
		SDL_free(target->data.play_se);
		break;
	case PLAY_MOVIE:
		SDL_free(target->data.play_movie->path);
		SDL_free(target->data.play_movie);
		break;
	case BLACK_FADE:
	case WHITE_FADE:
		SDL_free(target->data.fade);
		break;
	case PLAY_BGM:
	case END_BGM:
	case END_ROLL:
		SDL_free(target->data.path_end->path);
		SDL_free(target->data.path_end);
		break;
	case PRINT_TEXT:
		SDL_free(target->data.print_text->character);
		SDL_free(target->data.print_text->text);
		SDL_free(target->data.print_text);
		break;
	case PLAY_VOICE:
		SDL_free(target->data.play_voice->path);
		if (target->data.play_voice->character_short != NULL)
		{
			SDL_free(target->data.play_voice->character_short);
		}
		SDL_free(target->data.play_voice);
		break;
	case SetSELECT:
		for (wchar_t** it = target->data.set_select->options; *it != NULL; it++)
		{
			SDL_free(*it);
		}
		SDL_free(target->data.set_select->options);
		SDL_free(target->data.set_select);
		break;
	case UNKNOWN:
		SDL_free(target->data.unknown->line);
		SDL_free(target->data.unknown);
		break;
	case SkipFRAME:
	case Next:
		break;
	default:
		SDL_LogError(0, "clean");
		exit(1);
		break;
	}
}

static struct Kotonoha_orsEvent* Kotonoha_ORS_parse_line(wchar_t* line)
{
	if (!(line[0] == L'['))
	{
		return NULL;
	}
	struct Kotonoha_orsEvent* event = SDL_malloc(sizeof(struct Kotonoha_orsEvent));
	if (event == NULL)
	{
		SDL_LogError(0, "malloc");
		exit(1);
	}
	wchar_t* commandStart = wcschr(line, L'[') + 1;
	if (commandStart == NULL)
	{
		return NULL;
	}
	wchar_t* commandEnd = wcschr(commandStart, L']');
	if (commandEnd == NULL)
	{
		return NULL;
	}
	size_t commandLength = commandEnd - commandStart;

	if (SDL_wcsncmp(commandStart, L"CreateBG", commandLength) == 0)
	{
		event->command = CREATE_BG;
	}
	else if (SDL_wcsncmp(commandStart, L"PlaySe", commandLength) == 0)
	{
		event->command = PLAY_SE;
	}
	else if (SDL_wcsncmp(commandStart, L"PlayMovie", commandLength) == 0)
	{
		event->command = PLAY_MOVIE;
	}
	else if (SDL_wcsncmp(commandStart, L"BlackFade", commandLength) == 0)
	{
		event->command = BLACK_FADE;
	}
	else if (SDL_wcsncmp(commandStart, L"WhiteFade", commandLength) == 0)
	{
		event->command = WHITE_FADE;
	}
	else if (SDL_wcsncmp(commandStart, L"PlayBgm", commandLength) == 0)
	{
		event->command = PLAY_BGM;
	}
	else if (SDL_wcsncmp(commandStart, L"PrintText", commandLength) == 0)
	{
		event->command = PRINT_TEXT;
	}
	else if (SDL_wcsncmp(commandStart, L"PlayVoice", commandLength) == 0)
	{
		event->command = PLAY_VOICE;
	}
	else if (SDL_wcsncmp(commandStart, L"SkipFRAME", commandLength) == 0)
	{
		event->command = SkipFRAME;
	}
	else if (SDL_wcsncmp(commandStart, L"EndBGM", commandLength) == 0)
	{
		event->command = END_BGM;
	}
	else if (SDL_wcsncmp(commandStart, L"EndRoll", commandLength) == 0)
	{
		event->command = END_ROLL;
	}
	else if (SDL_wcsncmp(commandStart, L"Next", commandLength) == 0)
	{
		event->command = Next;
	}
	else if (SDL_wcsncmp(commandStart, L"SetSELECT", commandLength) == 0)
	{
		event->command = SetSELECT;
	}
	else
	{
		event->command = UNKNOWN;
		event->data.unknown = SDL_malloc(sizeof(struct Kotonoha_orsTypeUnknown));
		if (event->data.unknown == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		event->data.unknown->line = SDL_malloc((SDL_wcslen(line) + 1) * sizeof(wchar_t));
		if (event->data.unknown->line == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		SDL_memcpy(event->data.unknown->line, line, SDL_wcslen(line) * sizeof(wchar_t));
		event->data.unknown->line[SDL_wcslen(line)] = L'\0';
		return event;
	}
	line = wcschr(line, L'=') + 1;
	wchar_t* end = wcschr(line, L';');
	if (end == NULL)
	{
		return NULL;
	}
	*end = L'\0';

	event->start = Kotonoha_ORS_parse_timestamp(line);

	line = Kotonoha_ORS_wchar_jump_tab(line);
	bool transformed = false;
	int genericAction = 0;
	switch (event->command)
	{
	case Next:
	case SkipFRAME:
		break;
	case PLAY_SE:
	{
		event->data.play_se = SDL_malloc(sizeof(struct Kotonoha_orsTypePlaySe));
		if (event->data.play_se == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		};
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.play_se->a = wcstoul(line, NULL, 10);
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		int sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.play_se->path = SDL_malloc((sizeFind + 1) * sizeof(wchar_t));
		if (event->data.play_se->path == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		SDL_memcpy(event->data.play_se->path, line, sizeFind * sizeof(wchar_t));
		event->data.play_se->path[sizeFind] = L'\0';
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		event->data.play_se->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}
	case PLAY_MOVIE:
	{
		event->data.play_movie = SDL_malloc(sizeof(struct Kotonoha_orsTypePlayMovie));
		if (event->data.play_movie == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		int sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.play_movie->path = SDL_malloc((sizeFind + 1) * sizeof(wchar_t));
		if (event->data.play_movie->path == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		SDL_memcpy(event->data.play_movie->path, line, sizeFind * sizeof(wchar_t));
		event->data.play_movie->path[sizeFind] = L'\0';
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.play_movie->a = wcstoul(line, NULL, 10);
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		event->data.play_movie->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}
	case WHITE_FADE:
		genericAction = WHITE_FADE;
		goto fade;
	case BLACK_FADE:
		genericAction = BLACK_FADE;
		goto fade;
		{
		fade:
			event->data.fade = SDL_malloc(sizeof(struct Kotonoha_orsTypeFade));
			if (event->data.fade == NULL)
			{
				SDL_LogError(0, "malloc");
				exit(1);
			}
			event->data.fade->color = genericAction;
			Kotonoha_ORS_swap_tab_null(line, &transformed);
			event->data.fade->a = SDL_wcscmp(line, L"IN") == 0 ? true : false;
			Kotonoha_ORS_swap_tab_null(line, &transformed);
			line = Kotonoha_ORS_wchar_jump_tab(line);
			event->data.fade->end = Kotonoha_ORS_parse_timestamp(line);
			break;
		}
	case PRINT_TEXT:
	{
		event->data.print_text = SDL_malloc(sizeof(struct Kotonoha_orsTypePrintText));
		if (event->data.print_text == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		int sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.print_text->character = SDL_malloc((sizeFind + 1) * sizeof(wchar_t));
		if (event->data.print_text->character == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		SDL_memcpy(event->data.print_text->character, line, sizeFind * sizeof(wchar_t));
		event->data.print_text->character[sizeFind] = L'\0';
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.print_text->text = SDL_malloc((sizeFind + 1) * sizeof(wchar_t));
		if (event->data.print_text->text == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		SDL_memcpy(event->data.print_text->text, line, sizeFind * sizeof(wchar_t));
		event->data.print_text->text[sizeFind] = L'\0';
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		event->data.print_text->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}
	case PLAY_VOICE:
	{
		event->data.play_voice = SDL_malloc(sizeof(struct Kotonoha_orsTypePlayVoice));
		if (event->data.play_voice == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		int sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.play_voice->path = SDL_malloc((sizeFind + 1) * sizeof(wchar_t));
		if (event->data.play_voice->path == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		SDL_memcpy(event->data.play_voice->path, line, sizeFind * sizeof(wchar_t));
		event->data.play_voice->path[sizeFind] = L'\0';
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.play_voice->a = wcstoul(line, NULL, 10);
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		if (wcscmp(line, L"xxx") == 0)
		{
			event->data.play_voice->character_short = NULL;
		}
		else
		{
			event->data.play_voice->character_short = SDL_malloc((sizeFind + 1) * sizeof(wchar_t));
			if (event->data.play_voice->character_short == NULL)
			{
				SDL_LogError(0, "malloc");
				exit(1);
			}
			SDL_memcpy(event->data.play_voice->character_short, line, sizeFind * sizeof(wchar_t));
			event->data.play_voice->character_short[sizeFind] = L'\0';
		}
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		event->data.play_voice->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}
	case CREATE_BG:
	{
		event->data.create_bg = SDL_malloc(sizeof(struct Kotonoha_orsTypeCreateBg));
		if (event->data.create_bg == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		int sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.create_bg->a = SDL_malloc((sizeFind + 1) * sizeof(wchar_t));
		if (event->data.create_bg->a == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		SDL_memcpy(event->data.create_bg->a, line, sizeFind * sizeof(wchar_t));
		event->data.create_bg->a[sizeFind] = L'\0';
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.create_bg->path = SDL_malloc((sizeFind + 1) * sizeof(wchar_t));
		if (event->data.create_bg->path == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		SDL_memcpy(event->data.create_bg->path, line, sizeFind * sizeof(wchar_t));
		event->data.create_bg->path[sizeFind] = L'\0';
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		event->data.create_bg->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}
	case SetSELECT:
	{
		event->data.set_select = SDL_malloc(sizeof(struct Kotonoha_orsSetSELECT));
		if (event->data.set_select == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		event->data.set_select->size = 0;
		event->data.set_select->options = SDL_malloc(sizeof(wchar_t*));
		if (event->data.set_select->options == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		while (true)
		{
			int sizeStr = Kotonoha_ORS_swap_tab_null(line, &transformed);
			if (sizeStr > 0 && !transformed)
				break;
			event->data.set_select->options[event->data.set_select->size] = SDL_malloc(sizeStr * sizeof(wchar_t));
			if (event->data.set_select->options[event->data.set_select->size] == NULL)
			{
				SDL_LogError(0, "malloc");
				exit(1);
			}
			SDL_memcpy(event->data.set_select->options[event->data.set_select->size], line, sizeStr * sizeof(wchar_t));
			Kotonoha_ORS_swap_tab_null(line, &transformed);
			line = Kotonoha_ORS_wchar_jump_tab(line);
			event->data.set_select->size++;
			event->data.set_select->options = SDL_realloc(event->data.set_select->options, (size_t)(event->data.set_select->size + 1) * sizeof(wchar_t*));
			if (event->data.set_select->options == NULL)
			{
				SDL_LogError(0, "realloc");
				exit(1);
			}
			event->data.set_select->options[event->data.set_select->size] = NULL;
		}
		event->data.set_select->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}
	case PLAY_BGM:
	case END_BGM:
	case END_ROLL:
	{
		event->data.path_end = SDL_malloc(sizeof(struct Kotonoha_orsTypePathEnd));
		if (event->data.path_end == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		int sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.path_end->path = SDL_malloc((sizeFind + 1) * sizeof(wchar_t));
		if (event->data.path_end->path == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}
		SDL_memcpy(event->data.path_end->path, line, sizeFind * sizeof(wchar_t));
		event->data.path_end->path[sizeFind] = L'\0';
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_wchar_jump_tab(line);
		event->data.path_end->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}
	default:
		event->command = UNKNOWN;
		if (line != NULL)
		{
			event->data.unknown = SDL_malloc(sizeof(struct Kotonoha_orsTypeUnknown));
			if (event->data.unknown == NULL)
			{
				SDL_LogError(0, "malloc");
				exit(1);
			}
			event->data.unknown->line = SDL_malloc((SDL_wcslen(line) + 1) * sizeof(wchar_t));
			if (event->data.unknown->line == NULL)
			{
				SDL_LogError(0, "malloc");
				exit(1);
			}
			SDL_memcpy(event->data.unknown->line, line, SDL_wcslen(line) * sizeof(wchar_t));
			event->data.unknown->line[SDL_wcslen(line)] = L'\0';
			break;
		}
		event->data.unknown = NULL;
		break;
	}
	event->eventTouched = false;
	return event;
}

static void Kotonoha_ORS_storage(struct Kotonoha_orsData* target, struct Kotonoha_orsEvent* event)
{
	if (event == NULL || target == NULL)
	{
		return;
	}

	event->next = NULL;
	event->prev = NULL;

	if (target->size == 0)
	{
		// Se a lista estiver vazia, o primeiro evento é o "data" e "last"
		target->data = event;
		target->last = event;
		target->size++;
		return;
	}

	target->size++;

	struct Kotonoha_orsEvent* replaceTarget = target->last;

	// Encontre o local para inserir o evento
	while (replaceTarget != NULL && replaceTarget->start > event->start)
	{
		replaceTarget = replaceTarget->prev;
	}

	if (replaceTarget == NULL)
	{
		// Caso o evento deva ser inserido no início
		event->next = target->data;
		target->data->prev = event;
		target->data = event;
	}
	else
	{
		// Caso o evento deva ser inserido entre dois eventos
		event->next = replaceTarget->next;
		event->prev = replaceTarget;

		if (replaceTarget->next != NULL)
		{
			replaceTarget->next->prev = event;
		}

		replaceTarget->next = event;

		// Caso o evento seja o último, atualize target->last
		if (event->next == NULL)
		{
			target->last = event;
		}
	}
}

struct Kotonoha_orsData Kotonoha_OrsParser(const char* input)
{
	struct Kotonoha_orsData result = { NULL, NULL, 0 };
	FILE* file = fopen(input, "r, ccs=UTF-8");
	if (file == NULL)
	{
		SDL_LogError(0, "fopen");
		exit(1);
	}
	bool not_end_of_file = true;
	while (not_end_of_file)
	{
		int c = MIMUMUM_LINE_SIZE;
		wchar_t* line = SDL_malloc(c * sizeof(wchar_t));
		if (line == NULL)
		{
			SDL_LogError(0, "malloc");
			exit(1);
		}

		int i = 0;
		while (true)
		{
			wchar_t wc = fgetwc(file);
			if (wc == L'\n' || wc == WEOF)
			{
				not_end_of_file = (wc != WEOF);
				break;
			}

			if (i >= c - 1)
			{
				c *= 2;
				line = SDL_realloc(line, c * sizeof(wchar_t));
				if (line == NULL)
				{
					SDL_LogError(0, "realloc");
					exit(1);
				}
			}
			line[i++] = wc;
		}
		line[i] = L'\0';
		Kotonoha_ORS_storage(&result, Kotonoha_ORS_parse_line(line));
		SDL_free(line);
	}
	fclose(file);
	return result;
}

void Kotonoka_OrsDelete(struct Kotonoha_orsData* events, struct Kotonoha_orsEvent* target)
{
	if (events == NULL || events->data == NULL || target == NULL)
		return;
	for (struct Kotonoha_orsEvent* here = events->data, *prv = NULL; here != NULL; prv = here, here = here->next)
	{
		if (here == target)
		{
			if (prv == NULL)
			{
				events->data = here->next;
				if (events->data != NULL)
					events->data->prev = NULL;
			}
			else
			{
				prv->next = here->next;
				if (here->next != NULL)
					here->next->prev = prv;
			}
			Kotonoha_ORS_doDelete(target);
			SDL_free(here);
			events->size--;
			break;
		}
	}
}

void Kotonoha_OrsClean(struct Kotonoha_orsData* events)
{
	if (events == NULL || events->data == NULL)
	{
		return;
	}
	struct Kotonoha_orsEvent* base = events->data;
	events->data = NULL;
	events->last = NULL;
	events->size = 0;
	for (struct Kotonoha_orsEvent* now = base; now != NULL; base = now, now = now->next, SDL_free(base))
	{
		Kotonoha_ORS_doDelete(now);
	}
}
