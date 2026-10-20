#include <Kotonoha/utils/Log.h>
#include <Kotonoha/parsers/Ors.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *KotonohaOrsCharJumpTab(char *line) {
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

static int KotonohaOrsSwapTabNull(char *line, bool *transformed) {
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

static bool KotonohaOrsParseTimestamp(const char *text, Uint64 *result) {
  if (text == NULL || result == NULL) {
    return false;
  }

  Uint64 fields[3] = {0, 0, 0};
  const char *cursor = text;
  for (size_t i = 0; i < SDL_arraysize(fields); ++i) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }

    do {
      const Uint64 digit = (Uint64)(*cursor - '0');
      if (fields[i] > (UINT64_MAX - digit) / 10) {
        return false;
      }
      fields[i] = fields[i] * 10 + digit;
      ++cursor;
    } while (*cursor >= '0' && *cursor <= '9');

    if (i < 2) {
      if (*cursor != ':') {
        return false;
      }
      ++cursor;
    } else if (*cursor != '\0' && *cursor != '\t') {
      return false;
    }
  }

  if (fields[0] > UINT64_MAX / 60000) {
    return false;
  }
  Uint64 milliseconds = fields[0] * 60000;
  if (fields[1] > (UINT64_MAX - milliseconds) / 1000) {
    return false;
  }
  milliseconds += fields[1] * 1000;
  if (fields[2] > (UINT64_MAX - milliseconds) / 10) {
    return false;
  }

  *result = milliseconds + fields[2] * 10;
  return true;
}

static bool KotonohaOrsCommandIs(const char *start, size_t length,
                                 const char *expected) {
  return SDL_strlen(expected) == length &&
         SDL_memcmp(start, expected, length) == 0;
}

static size_t KotonohaOrsFieldCount(const char *payload) {
  size_t count = 1;
  for (const char *cursor = payload; *cursor != '\0'; ++cursor) {
    if (*cursor == '\t') {
      ++count;
    }
  }
  return count;
}

static bool KotonohaOrsHasValidFieldCount(enum Kotonoha_OrsType command,
                                          size_t count) {
  switch (command) {
  case Next:
  case SkipFRAME:
    return count == 1;
  case PLAY_VOICE:
    return count == 5;
  case CREATE_BG:
  case PLAY_SE:
  case PLAY_MOVIE:
  case PRINT_TEXT:
    return count == 4;
  case BLACK_FADE:
  case WHITE_FADE:
  case PLAY_BGM:
  case END_BGM:
  case END_ROLL:
    return count == 3;
  case SetSELECT:
    return count >= 3;
  default:
    return true;
  }
}

static void KotonohaOrsDoDelete(Kotonoha_OrsEvent *target) {
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
  case SetSELECT:
    for (char **it = target->data.set_select->options; *it != NULL; it++) {
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
    KLOG_ERROR(PARSER, "Failed to clean script event list");
    exit(1);
    break;
  }
}

static Kotonoha_OrsEvent *KotonohaOrsParseLine(char *line) {
  if (!(line[0] == '[')) {
    return NULL;
  }

  Kotonoha_OrsEvent *event = SDL_calloc(1, sizeof(Kotonoha_OrsEvent));
  if (event == NULL) {
    KLOG_ERROR(PARSER, "Out of memory while parsing script event");
    exit(1);
  }

  char *open = strchr(line, '[');
  if (open == NULL) {
    SDL_free(event);
    return NULL;
  }

  char *commandStart = open + 1;
  char *commandEnd = strchr(commandStart, ']');
  if (commandEnd == NULL) {
    SDL_free(event);
    return NULL;
  }

  size_t commandLength = (size_t)(commandEnd - commandStart);

  if (KotonohaOrsCommandIs(commandStart, commandLength, "CreateBG")) {
    event->command = CREATE_BG;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "PlaySe")) {
    event->command = PLAY_SE;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "PlayMovie")) {
    event->command = PLAY_MOVIE;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "BlackFade")) {
    event->command = BLACK_FADE;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "WhiteFade")) {
    event->command = WHITE_FADE;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "PlayBgm")) {
    event->command = PLAY_BGM;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "PrintText")) {
    event->command = PRINT_TEXT;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "PlayVoice")) {
    event->command = PLAY_VOICE;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "SkipFRAME")) {
    event->command = SkipFRAME;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "EndBGM")) {
    event->command = END_BGM;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "EndRoll")) {
    event->command = END_ROLL;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "Next")) {
    event->command = Next;
  } else if (KotonohaOrsCommandIs(commandStart, commandLength, "SetSELECT")) {
    event->command = SetSELECT;
  } else {
    event->command = UNKNOWN;
    event->data.unknown = SDL_malloc(sizeof(struct Kotonoha_OrsTypeUnknown));
    if (event->data.unknown == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    event->data.unknown->line = SDL_malloc((strlen(line) + 1) * sizeof(char));
    if (event->data.unknown->line == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    memcpy(event->data.unknown->line, line, strlen(line) + 1);
    return event;
  }

  char *eq = strchr(line, '=');
  if (eq == NULL) {
    SDL_free(event);
    return NULL;
  }
  line = eq + 1;

  char *end = strrchr(line, ';');
  if (end == NULL) {
    SDL_free(event);
    return NULL;
  }
  *end = '\0';

  if (!KotonohaOrsHasValidFieldCount(event->command,
                                     KotonohaOrsFieldCount(line))) {
    SDL_free(event);
    return NULL;
  }

  if (!KotonohaOrsParseTimestamp(line, &event->start)) {
    SDL_free(event);
    return NULL;
  }

  if (event->command == Next || event->command == SkipFRAME) {
    event->end = event->start;
  } else {
    char *endField = strrchr(line, '\t');
    if (endField == NULL ||
        !KotonohaOrsParseTimestamp(endField + 1, &event->end)) {
      SDL_free(event);
      return NULL;
    }
  }

  line = KotonohaOrsCharJumpTab(line);
  bool transformed = false;
  int genericAction = 0;

  switch (event->command) {
  case Next:
  case SkipFRAME:
    break;

  case PLAY_SE: {
    event->data.play_se = SDL_malloc(sizeof(struct Kotonoha_OrsTypePlaySe));
    if (event->data.play_se == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    KotonohaOrsSwapTabNull(line, &transformed);
    event->data.play_se->a = strtoul(line, NULL, 10);
    KotonohaOrsSwapTabNull(line, &transformed);

    line = KotonohaOrsCharJumpTab(line);
    int sizeFind = KotonohaOrsSwapTabNull(line, &transformed);

    event->data.play_se->path = SDL_malloc((sizeFind + 1) * sizeof(char));
    if (event->data.play_se->path == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    memcpy(event->data.play_se->path, line, sizeFind * sizeof(char));
    event->data.play_se->path[sizeFind] = '\0';

    KotonohaOrsSwapTabNull(line, &transformed);
    line = KotonohaOrsCharJumpTab(line);
    break;
  }

  case PLAY_MOVIE: {
    event->data.play_movie =
        SDL_malloc(sizeof(struct Kotonoha_OrsTypePlayMovie));
    if (event->data.play_movie == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    int sizeFind = KotonohaOrsSwapTabNull(line, &transformed);
    event->data.play_movie->path = SDL_malloc((sizeFind + 1) * sizeof(char));
    if (event->data.play_movie->path == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    memcpy(event->data.play_movie->path, line, sizeFind * sizeof(char));
    event->data.play_movie->path[sizeFind] = '\0';

    KotonohaOrsSwapTabNull(line, &transformed);
    line = KotonohaOrsCharJumpTab(line);

    KotonohaOrsSwapTabNull(line, &transformed);
    event->data.play_movie->a = strtoul(line, NULL, 10);
    KotonohaOrsSwapTabNull(line, &transformed);

    line = KotonohaOrsCharJumpTab(line);
    break;
  }

  case WHITE_FADE:
    genericAction = WHITE_FADE;
    goto fade;
  case BLACK_FADE:
    genericAction = BLACK_FADE;
    goto fade;
  fade:
    {
      event->data.fade = SDL_malloc(sizeof(struct Kotonoha_OrsTypeFade));
      if (event->data.fade == NULL) {
        KLOG_ERROR(PARSER, "Out of memory while parsing script event");
        exit(1);
      }

      event->data.fade->color = genericAction;
      KotonohaOrsSwapTabNull(line, &transformed);
      event->data.fade->a = (strcmp(line, "IN") == 0) ? true : false;
      KotonohaOrsSwapTabNull(line, &transformed);

      line = KotonohaOrsCharJumpTab(line);
      break;
    }

  case PRINT_TEXT: {
    event->data.print_text =
        SDL_malloc(sizeof(struct Kotonoha_OrsTypePrintText));
    if (event->data.print_text == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    int sizeFind = KotonohaOrsSwapTabNull(line, &transformed);
    event->data.print_text->character =
        SDL_malloc((sizeFind + 1) * sizeof(char));
    if (event->data.print_text->character == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    memcpy(event->data.print_text->character, line, sizeFind * sizeof(char));
    event->data.print_text->character[sizeFind] = '\0';

    KotonohaOrsSwapTabNull(line, &transformed);
    line = KotonohaOrsCharJumpTab(line);

    sizeFind = KotonohaOrsSwapTabNull(line, &transformed);
    event->data.print_text->text = SDL_malloc((sizeFind + 1) * sizeof(char));
    if (event->data.print_text->text == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    memcpy(event->data.print_text->text, line, sizeFind * sizeof(char));
    event->data.print_text->text[sizeFind] = '\0';

    KotonohaOrsSwapTabNull(line, &transformed);
    line = KotonohaOrsCharJumpTab(line);
    break;
  }

  case PLAY_VOICE: {
    char *fields[4] = {line, NULL, NULL, NULL};
    for (size_t i = 1; i < SDL_arraysize(fields); ++i) {
      char *separator = strchr(fields[i - 1], '\t');
      if (separator == NULL) {
        SDL_free(event);
        return NULL;
      }
      *separator = '\0';
      fields[i] = separator + 1;
    }

    event->data.play_voice =
        SDL_calloc(1, sizeof(struct Kotonoha_OrsTypePlayVoice));
    if (event->data.play_voice == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    event->data.play_voice->path = SDL_strdup(fields[0]);
    if (event->data.play_voice->path == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    event->data.play_voice->a = SDL_strtoul(fields[1], NULL, 10);
    if (SDL_strcmp(fields[2], "xxx") != 0) {
      event->data.play_voice->character_short = SDL_strdup(fields[2]);
      if (event->data.play_voice->character_short == NULL) {
        SDL_free(event->data.play_voice->path);
        SDL_free(event->data.play_voice);
        SDL_free(event);
        return NULL;
      }
    }
    break;
  }

  case CREATE_BG: {
    event->data.create_bg = SDL_malloc(sizeof(struct Kotonoha_OrsTypeCreateBg));
    if (event->data.create_bg == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    int sizeFind = KotonohaOrsSwapTabNull(line, &transformed);
    event->data.create_bg->a = SDL_malloc((sizeFind + 1) * sizeof(char));
    if (event->data.create_bg->a == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    memcpy(event->data.create_bg->a, line, sizeFind * sizeof(char));
    event->data.create_bg->a[sizeFind] = '\0';

    KotonohaOrsSwapTabNull(line, &transformed);
    line = KotonohaOrsCharJumpTab(line);

    sizeFind = KotonohaOrsSwapTabNull(line, &transformed);
    event->data.create_bg->path = SDL_malloc((sizeFind + 1) * sizeof(char));
    if (event->data.create_bg->path == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    memcpy(event->data.create_bg->path, line, sizeFind * sizeof(char));
    event->data.create_bg->path[sizeFind] = '\0';

    KotonohaOrsSwapTabNull(line, &transformed);
    line = KotonohaOrsCharJumpTab(line);
    break;
  }

  case SetSELECT: {
    event->data.set_select = SDL_malloc(sizeof(struct KotonohaOrsSetSELECT));
    if (event->data.set_select == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    event->data.set_select->size = 0;
    event->data.set_select->options = SDL_malloc(sizeof(char *));
    if (event->data.set_select->options == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    event->data.set_select->options[0] = NULL;

    while (true) {
      int sizeStr = KotonohaOrsSwapTabNull(line, &transformed);
      if (sizeStr <= 0 || !transformed) {
        break;
      }

      const bool ignoreOption = sizeStr <= 1 || line[0] == '\0' ||
                                SDL_strcasecmp(line, "null") == 0 ||
                                SDL_strcasecmp(line, "\"null\"") == 0 ||
                                SDL_strcmp(line, "\"\"") == 0 ||
                                SDL_strcmp(line, "''") == 0;

      if (!ignoreOption) {
        char *optionText = SDL_malloc((size_t)sizeStr * sizeof(char));
        if (optionText == NULL) {
          KLOG_ERROR(PARSER, "Out of memory while parsing script event");
          exit(1);
        }

        memcpy(optionText, line, (size_t)sizeStr * sizeof(char));

        char **newOptions = SDL_realloc(
            event->data.set_select->options,
            (size_t)(event->data.set_select->size + 2) * sizeof(char *));
        if (newOptions == NULL) {
          SDL_free(optionText);
          KLOG_ERROR(PARSER, "Out of memory while growing script buffer");
          exit(1);
        }

        event->data.set_select->options = newOptions;
        event->data.set_select->options[event->data.set_select->size] =
            optionText;
        event->data.set_select->size++;
        event->data.set_select->options[event->data.set_select->size] = NULL;
      }

      KotonohaOrsSwapTabNull(line, &transformed);
      line = KotonohaOrsCharJumpTab(line);
    }

    break;
  }

  case PLAY_BGM:
  case END_BGM:
  case END_ROLL: {
    event->data.path_end = SDL_malloc(sizeof(struct Kotonoha_OrsTypePathEnd));
    if (event->data.path_end == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    int sizeFind = KotonohaOrsSwapTabNull(line, &transformed);
    event->data.path_end->path = SDL_malloc((sizeFind + 1) * sizeof(char));
    if (event->data.path_end->path == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
      exit(1);
    }

    memcpy(event->data.path_end->path, line, sizeFind * sizeof(char));
    event->data.path_end->path[sizeFind] = '\0';

    KotonohaOrsSwapTabNull(line, &transformed);
    line = KotonohaOrsCharJumpTab(line);
    break;
  }

  default:
    event->command = UNKNOWN;
    if (line != NULL) {
      event->data.unknown = SDL_malloc(sizeof(struct Kotonoha_OrsTypeUnknown));
      if (event->data.unknown == NULL) {
        KLOG_ERROR(PARSER, "Out of memory while parsing script event");
        exit(1);
      }

      event->data.unknown->line = SDL_malloc((strlen(line) + 1) * sizeof(char));
      if (event->data.unknown->line == NULL) {
        KLOG_ERROR(PARSER, "Out of memory while parsing script event");
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

static void KotonohaOrsStorage(Kotonoha_OrsData *target,
                               Kotonoha_OrsEvent *event) {
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

  Kotonoha_OrsEvent *replaceTarget = target->last;

  while (replaceTarget != NULL && replaceTarget->start > event->start) {
    replaceTarget = replaceTarget->prev;
  }

  if (replaceTarget == NULL) {
    event->next = target->data;
    target->data->prev = event;
    target->data = event;
  } else {
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

Kotonoha_OrsData KotonohaOrsParser(const char *input) {
  Kotonoha_OrsData result = {NULL, NULL, 0};
  FILE *file = fopen(input, "r");
  if (file == NULL) {
    KLOG_ERROR(PARSER, "Failed to open script file '%s'", input);
    return result;
  }

  bool not_end_of_file = true;

  while (not_end_of_file) {
    int c = MIMUMUM_LINE_SIZE;
    char *line = SDL_malloc(c * sizeof(char));
    if (line == NULL) {
      KLOG_ERROR(PARSER, "Out of memory while parsing script event");
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
          KLOG_ERROR(PARSER, "Out of memory while growing script buffer");
          exit(1);
        }
      }
      line[i++] = (char)ch;
    }

    line[i] = '\0';
    if (i > 0 && line[i - 1] == '\r') {
      line[i - 1] = '\0';
    }

    struct Kotonoha_OrsEvent *parsed_event = KotonohaOrsParseLine(line);
    if (parsed_event) {
      KotonohaOrsStorage(&result, parsed_event);
    }
    SDL_free(line);
  }

  fclose(file);
  return result;
}

void KotonokaOrsDelete(Kotonoha_OrsData *events, Kotonoha_OrsEvent *target) {
  if (events == NULL || events->data == NULL || target == NULL)
    return;

  for (struct Kotonoha_OrsEvent *here = events->data, *prv = NULL; here != NULL;
       prv = here, here = here->next) {
    if (here == target) {
      if (prv == NULL) {
        events->data = here->next;
        if (events->data != NULL)
          events->data->prev = NULL;
      } else {
        prv->next = here->next;
        if (here->next != NULL)
          here->next->prev = prv;
      }

      KotonohaOrsDoDelete(target);
      SDL_free(here);
      events->size--;
      break;
    }
  }
}

void KotonohaOrsClean(Kotonoha_OrsData *events) {
  if (events == NULL || events->data == NULL) {
    return;
  }

  Kotonoha_OrsEvent *base = events->data;
  events->data = NULL;
  events->last = NULL;
  events->size = 0;

  for (Kotonoha_OrsEvent *now = base; now != NULL;
       base = now, now = now->next, SDL_free(base)) {
    KotonohaOrsDoDelete(now);
  }
}