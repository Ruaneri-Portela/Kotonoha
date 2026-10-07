#include <Kotonoha/parsers/Ors.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* Kotonoha_ORS_char_jump_tab(char* line) {
	for (; *line != '\t' && *line; line++) {
		if (*line == '\0') {
			return NULL;
		}
	}
	for (; *line == '\t'; line++) {
		if (*line == '\0') {
			return NULL;
		}
	}
	return line;
}

static int Kotonoha_ORS_swap_tab_null(char* line, bool* transformed) {
	int size = 0;
	for (;; line++) {
		size++;
		if (*line == '\t' && !*transformed) {
			*line = '\0';
			*transformed = true;
			break;
		}
		if (*line == '\0') {
			if (*transformed) {
				*line = '\t';
				*transformed = false;
				break;
			}
			break;
		}
	}
	return size;
}

static Uint64 Kotonoha_ORS_parse_timestamp(char* line) {
	Uint64 time[3] = { 0 };
	for (int i = 0; i < 3; i++) {
		char* digit = strchr(line, ':');
		if (digit != NULL) {
			*digit = '\0';
			time[i] = strtoul(line, NULL, 10);
			*digit = ':';
			line = digit + 1;
		}
		else {
			time[i] = strtoul(line, NULL, 10);
			break;
		}
	}

	return time[0] * 60000 + time[1] * 1000 + time[2] * 10;
}

static bool Kotonoha_ORS_parse_tick(const char* text,
	Kotonoha_SceneTick* result) {
	unsigned long long minutes = 0, seconds = 0, frame = 0;
	char extra = '\0';
	const int parsed = sscanf(text, "%llu:%llu:%llu%c",
		&minutes, &seconds, &frame, &extra);
	if (result == NULL || (parsed != 3 && !(parsed == 4 && extra == '\t'))) {
		return false;
	}
	*result = ((Kotonoha_SceneTick)minutes * 60 + seconds) * 24 + frame;
	return true;
}

static char* Kotonoha_ORS_copy_field(const char* start, size_t length) {
	char* copy = SDL_malloc(length + 1);
	if (copy != NULL) {
		memcpy(copy, start, length);
		copy[length] = '\0';
	}
	return copy;
}

/* The distributed English ORS reader separates fields only at TABs. */
static size_t Kotonoha_ORS_field_count(const char* payload) {
	size_t count = 1;
	for (const char* p = payload; *p != '\0'; ++p) {
		if (*p == '\t') {
			++count;
		}
	}
	return count;
}

static size_t Kotonoha_ORS_expected_fields(enum Kotonoha_orsType command) {
	switch (command) {
	case Next:
	case SkipFRAME:
		return 1;
	case PLAY_VOICE:
		return 5;
	case CREATE_BG:
	case PLAY_SE:
	case PLAY_MOVIE:
	case PRINT_TEXT:
	case SetSELECT:
		return 4;
	case BLACK_FADE:
	case WHITE_FADE:
	case PLAY_BGM:
	case END_BGM:
	case END_ROLL:
	case MOVE_SOM:
		return 3;
	default:
		return 0;
	}
}

static bool Kotonoha_ORS_command_is(const char* start, size_t length,
	const char* expected) {
	return strlen(expected) == length && memcmp(start, expected, length) == 0;
}

static void Kotonoha_ORS_doDelete(struct Kotonoha_orsEvent* target) {
	switch (target->command) {
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
		if (target->data.play_voice->character_short != NULL) {
			SDL_free(target->data.play_voice->character_short);
		}
		SDL_free(target->data.play_voice);
		break;
	case MOVE_SOM:
		SDL_free(target->data.move_som);
		break;
	case SetSELECT:
		for (char** it = target->data.set_select->options; *it != NULL; it++) {
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

static struct Kotonoha_orsEvent* Kotonoha_ORS_parse_line(char* line) {
	if (!(line[0] == '[')) {
		return NULL;
	}

	struct Kotonoha_orsEvent* event =
		SDL_calloc(1, sizeof(struct Kotonoha_orsEvent));
	if (event == NULL) {
		SDL_LogError(0, "malloc");
		exit(1);
	}

	char* open = strchr(line, '[');
	if (open == NULL) {
		SDL_free(event);
		return NULL;
	}

	char* commandStart = open + 1;
	char* commandEnd = strchr(commandStart, ']');
	if (commandEnd == NULL) {
		SDL_free(event);
		return NULL;
	}

	size_t commandLength = (size_t)(commandEnd - commandStart);

	if (Kotonoha_ORS_command_is(commandStart, commandLength, "CreateBG")) {
		event->command = CREATE_BG;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "PlaySe")) {
		event->command = PLAY_SE;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "PlayMovie")) {
		event->command = PLAY_MOVIE;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "BlackFade")) {
		event->command = BLACK_FADE;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "WhiteFade")) {
		event->command = WHITE_FADE;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "PlayBgm")) {
		event->command = PLAY_BGM;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "PrintText")) {
		event->command = PRINT_TEXT;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "PlayVoice")) {
		event->command = PLAY_VOICE;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "SkipFRAME")) {
		event->command = SkipFRAME;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "EndBGM")) {
		event->command = END_BGM;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "EndRoll")) {
		event->command = END_ROLL;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "Next")) {
		event->command = Next;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "SetSELECT")) {
		event->command = SetSELECT;
	}
	else if (Kotonoha_ORS_command_is(commandStart, commandLength, "MoveSom")) {
		event->command = MOVE_SOM;
	}
	else {
		event->command = UNKNOWN;
		event->data.unknown = SDL_malloc(sizeof(struct Kotonoha_orsTypeUnknown));
		if (event->data.unknown == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		event->data.unknown->line = SDL_malloc((strlen(line) + 1) * sizeof(char));
		if (event->data.unknown->line == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		memcpy(event->data.unknown->line, line, strlen(line) + 1);
		return event;
	}

	char* eq = strchr(line, '=');
	if (eq == NULL) {
		SDL_free(event);
		return NULL;
	}
	line = eq + 1;

	/* Dialogue may contain a literal ';'; the final one terminates the line. */
	char* end = strrchr(line, ';');
	if (end == NULL) {
		SDL_free(event);
		return NULL;
	}
	*end = '\0';
	if (Kotonoha_ORS_field_count(line) !=
		Kotonoha_ORS_expected_fields(event->command)) {
		SDL_free(event);
		return NULL;
	}
	if (!Kotonoha_ORS_parse_tick(line, &event->startTick)) {
		SDL_free(event);
		return NULL;
	}
	char* finalField = strrchr(line, '\t');
	if (finalField == NULL) {
		event->endTick = event->startTick;
	}
	else if (!Kotonoha_ORS_parse_tick(finalField + 1, &event->endTick)) {
		SDL_free(event);
		return NULL;
	}

	event->start = Kotonoha_ORS_parse_timestamp(line);

	line = Kotonoha_ORS_char_jump_tab(line);
	bool transformed = false;
	int genericAction = 0;

	switch (event->command) {
	case Next:
	case SkipFRAME:
		event->end = event->start;
		break;

	case MOVE_SOM: {
		event->data.move_som = SDL_malloc(sizeof(struct Kotonoha_orsTypeMoveSom));
		if (event->data.move_som == NULL) {
			SDL_free(event);
			return NULL;
		}
		char* numeric = line;
		char* end_tick = strchr(numeric, '\t');
		if (end_tick == NULL) {
			SDL_free(event->data.move_som);
			SDL_free(event);
			return NULL;
		}
		*end_tick++ = '\0';
		event->data.move_som->numeric = strtoull(numeric, NULL, 10);
		event->end = Kotonoha_ORS_parse_timestamp(end_tick);
		break;
	}

	case PLAY_SE: {
		event->data.play_se = SDL_malloc(sizeof(struct Kotonoha_orsTypePlaySe));
		if (event->data.play_se == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.play_se->a = strtoul(line, NULL, 10);
		Kotonoha_ORS_swap_tab_null(line, &transformed);

		line = Kotonoha_ORS_char_jump_tab(line);
		int sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);

		event->data.play_se->path = SDL_malloc((sizeFind + 1) * sizeof(char));
		if (event->data.play_se->path == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		memcpy(event->data.play_se->path, line, sizeFind * sizeof(char));
		event->data.play_se->path[sizeFind] = '\0';

		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_char_jump_tab(line);
		event->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}

	case PLAY_MOVIE: {
		event->data.play_movie =
			SDL_malloc(sizeof(struct Kotonoha_orsTypePlayMovie));
		if (event->data.play_movie == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		int sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.play_movie->path = SDL_malloc((sizeFind + 1) * sizeof(char));
		if (event->data.play_movie->path == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		memcpy(event->data.play_movie->path, line, sizeFind * sizeof(char));
		event->data.play_movie->path[sizeFind] = '\0';

		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_char_jump_tab(line);

		Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.play_movie->a = strtoul(line, NULL, 10);
		Kotonoha_ORS_swap_tab_null(line, &transformed);

		line = Kotonoha_ORS_char_jump_tab(line);
		event->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}

	case WHITE_FADE:
		genericAction = WHITE_FADE;
		goto fade;
	case BLACK_FADE:
		genericAction = BLACK_FADE;
		goto fade;
	fade: {
		event->data.fade = SDL_malloc(sizeof(struct Kotonoha_orsTypeFade));
		if (event->data.fade == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		event->data.fade->color = genericAction;
		Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.fade->a = (strcmp(line, "IN") == 0) ? true : false;
		Kotonoha_ORS_swap_tab_null(line, &transformed);

		line = Kotonoha_ORS_char_jump_tab(line);
		event->end = Kotonoha_ORS_parse_timestamp(line);
		break;
		}

	case PRINT_TEXT: {
		event->data.print_text =
			SDL_malloc(sizeof(struct Kotonoha_orsTypePrintText));
		if (event->data.print_text == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		int sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.print_text->character =
			SDL_malloc((sizeFind + 1) * sizeof(char));
		if (event->data.print_text->character == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		memcpy(event->data.print_text->character, line, sizeFind * sizeof(char));
		event->data.print_text->character[sizeFind] = '\0';

		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_char_jump_tab(line);

		sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.print_text->text = SDL_malloc((sizeFind + 1) * sizeof(char));
		if (event->data.print_text->text == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		memcpy(event->data.print_text->text, line, sizeFind * sizeof(char));
		event->data.print_text->text[sizeFind] = '\0';

		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_char_jump_tab(line);
		event->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}

	case PLAY_VOICE: {
		/* Keep adjacent TABs: empty NUMERIC and KEY still occupy fields. */
		char* fields[4];
		fields[0] = line;
		for (size_t i = 1; i < 4; ++i) {
			char* separator = strchr(fields[i - 1], '\t');
			*separator = '\0'; /* Exact arity was checked above. */
			fields[i] = separator + 1;
		}
		event->data.play_voice =
			SDL_calloc(1, sizeof(struct Kotonoha_orsTypePlayVoice));
		if (event->data.play_voice == NULL) {
			SDL_free(event);
			return NULL;
		}
		event->data.play_voice->path =
			Kotonoha_ORS_copy_field(fields[0], strlen(fields[0]));
		event->data.play_voice->a = strtoull(fields[1], NULL, 10);
		if (strcmp(fields[2], "xxx") != 0) {
			event->data.play_voice->character_short =
				Kotonoha_ORS_copy_field(fields[2], strlen(fields[2]));
		}
		if (event->data.play_voice->path == NULL ||
			(strcmp(fields[2], "xxx") != 0 &&
			 event->data.play_voice->character_short == NULL)) {
			SDL_free(event->data.play_voice->path);
			SDL_free(event->data.play_voice->character_short);
			SDL_free(event->data.play_voice);
			SDL_free(event);
			return NULL;
		}
		event->end = Kotonoha_ORS_parse_timestamp(fields[3]);
		break;
	}

	case CREATE_BG: {
		event->data.create_bg = SDL_malloc(sizeof(struct Kotonoha_orsTypeCreateBg));
		if (event->data.create_bg == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		int sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.create_bg->a = SDL_malloc((sizeFind + 1) * sizeof(char));
		if (event->data.create_bg->a == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		memcpy(event->data.create_bg->a, line, sizeFind * sizeof(char));
		event->data.create_bg->a[sizeFind] = '\0';

		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_char_jump_tab(line);

		sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.create_bg->path = SDL_malloc((sizeFind + 1) * sizeof(char));
		if (event->data.create_bg->path == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		memcpy(event->data.create_bg->path, line, sizeFind * sizeof(char));
		event->data.create_bg->path[sizeFind] = '\0';

		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_char_jump_tab(line);
		event->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}

	case SetSELECT: {
		event->data.set_select = SDL_malloc(sizeof(struct Kotonoha_orsSetSELECT));
		if (event->data.set_select == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		event->data.set_select->size = 0;
		event->data.set_select->options = SDL_malloc(sizeof(char*));
		if (event->data.set_select->options == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		event->data.set_select->options[0] = NULL;

		while (true) {
			int sizeStr = Kotonoha_ORS_swap_tab_null(line, &transformed);
			if (sizeStr <= 0 || !transformed) {
				break;
			}

			/* null is special only in a SetSELECT option field. */
			const bool ignoreOption = line[0] == '\0' ||
				strcmp(line, "null") == 0 || strcmp(line, "NULL") == 0 ||
				strcmp(line, "\"\"") == 0;

			if (!ignoreOption) {
				char* optionText = SDL_malloc((size_t)sizeStr * sizeof(char));
				if (optionText == NULL) {
					SDL_LogError(0, "malloc");
					exit(1);
				}

				memcpy(optionText, line, (size_t)sizeStr * sizeof(char));

				char** newOptions = SDL_realloc(
					event->data.set_select->options,
					(size_t)(event->data.set_select->size + 2) * sizeof(char*));
				if (newOptions == NULL) {
					SDL_free(optionText);
					SDL_LogError(0, "realloc");
					exit(1);
				}

				event->data.set_select->options = newOptions;
				event->data.set_select->options[event->data.set_select->size] = optionText;
				event->data.set_select->size++;
				event->data.set_select->options[event->data.set_select->size] = NULL;
			}

			Kotonoha_ORS_swap_tab_null(line, &transformed);
			line = Kotonoha_ORS_char_jump_tab(line);
		}

		event->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}

	case PLAY_BGM:
	case END_BGM:
	case END_ROLL: {
		event->data.path_end = SDL_malloc(sizeof(struct Kotonoha_orsTypePathEnd));
		if (event->data.path_end == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		int sizeFind = Kotonoha_ORS_swap_tab_null(line, &transformed);
		event->data.path_end->path = SDL_malloc((sizeFind + 1) * sizeof(char));
		if (event->data.path_end->path == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		memcpy(event->data.path_end->path, line, sizeFind * sizeof(char));
		event->data.path_end->path[sizeFind] = '\0';

		Kotonoha_ORS_swap_tab_null(line, &transformed);
		line = Kotonoha_ORS_char_jump_tab(line);
		event->end = Kotonoha_ORS_parse_timestamp(line);
		break;
	}

	default:
		event->command = UNKNOWN;
		if (line != NULL) {
			event->data.unknown = SDL_malloc(sizeof(struct Kotonoha_orsTypeUnknown));
			if (event->data.unknown == NULL) {
				SDL_LogError(0, "malloc");
				exit(1);
			}

			event->data.unknown->line = SDL_malloc((strlen(line) + 1) * sizeof(char));
			if (event->data.unknown->line == NULL) {
				SDL_LogError(0, "malloc");
				exit(1);
			}

			memcpy(event->data.unknown->line, line, strlen(line) + 1);
			break;
		}

		event->data.unknown = NULL;
		break;
	}

	event->eventTouched = false;
	return event;
}

static void Kotonoha_ORS_storage(struct Kotonoha_orsData* target,
	struct Kotonoha_orsEvent* event) {
	if (event == NULL || target == NULL) {
		return;
	}

	event->next = NULL;
	event->prev = NULL;

	if (target->size == 0) {
		target->data = event;
		target->last = event;
		target->size++;
		return;
	}

	target->size++;

	struct Kotonoha_orsEvent* replaceTarget = target->last;

	/* Insert after earlier lines with the same start tick. */
	while (replaceTarget != NULL && replaceTarget->startTick > event->startTick) {
		replaceTarget = replaceTarget->prev;
	}

	if (replaceTarget == NULL) {
		event->next = target->data;
		target->data->prev = event;
		target->data = event;
	}
	else {
		event->next = replaceTarget->next;
		event->prev = replaceTarget;

		if (replaceTarget->next != NULL) {
			replaceTarget->next->prev = event;
		}

		replaceTarget->next = event;

		if (event->next == NULL) {
			target->last = event;
		}
	}
}

struct Kotonoha_orsData Kotonoha_OrsParser(const char* input) {
	struct Kotonoha_orsData result = { NULL, NULL, 0 };
	FILE* file = fopen(input, "r");
	if (file == NULL) {
		SDL_LogError(0, "fopen");
		return result;
	}

	bool not_end_of_file = true;

	while (not_end_of_file) {
		int c = MIMUMUM_LINE_SIZE;
		char* line = SDL_malloc(c * sizeof(char));
		if (line == NULL) {
			SDL_LogError(0, "malloc");
			exit(1);
		}

		int i = 0;
		while (true) {
			int ch = fgetc(file);
			if (ch == '\n' || ch == EOF) {
				not_end_of_file = (ch != EOF);
				break;
			}

			if (i >= c - 1) {
				c *= 2;
				line = SDL_realloc(line, c * sizeof(char));
				if (line == NULL) {
					SDL_LogError(0, "realloc");
					exit(1);
				}
			}
			line[i++] = (char)ch;
		}

		line[i] = '\0';

		struct Kotonoha_orsEvent* parsed_event = Kotonoha_ORS_parse_line(line);
		if (parsed_event) {
			Kotonoha_ORS_storage(&result, parsed_event);
		}
		SDL_free(line);
	}

	fclose(file);
	return result;
}

void Kotonoka_OrsDelete(struct Kotonoha_orsData* events,
	struct Kotonoha_orsEvent* target) {
	if (events == NULL || events->data == NULL || target == NULL)
		return;

	for (struct Kotonoha_orsEvent* here = events->data, *prv = NULL; here != NULL;
		prv = here, here = here->next) {
		if (here == target) {
			if (prv == NULL) {
				events->data = here->next;
				if (events->data != NULL)
					events->data->prev = NULL;
			}
			else {
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

void Kotonoha_OrsClean(struct Kotonoha_orsData* events) {
	if (events == NULL || events->data == NULL) {
		return;
	}

	struct Kotonoha_orsEvent* base = events->data;
	events->data = NULL;
	events->last = NULL;
	events->size = 0;

	for (struct Kotonoha_orsEvent* now = base; now != NULL;
		base = now, now = now->next, SDL_free(base)) {
		Kotonoha_ORS_doDelete(now);
	}
}
