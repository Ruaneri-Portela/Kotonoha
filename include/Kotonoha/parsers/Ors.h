#pragma once
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define MIMUMUM_LINE_SIZE 1024

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Kotonoha_OrsType {
  CREATE_BG,
  PLAY_SE,
  PLAY_MOVIE,
  WHITE_FADE,
  BLACK_FADE,
  PLAY_BGM,
  PRINT_TEXT,
  PLAY_VOICE,
  SkipFRAME,
  SetSELECT,
  END_BGM,
  END_ROLL,
  Next,
  UNKNOWN
} Kotonoha_OrsType;

enum KotonohaOrsFadecolor { BLACK, WHITE };

struct Kotonoha_OrsTypeCreateBg {
  char *a;
  char *path;
};

struct Kotonoha_OrsTypePlaySe {
  Uint64 a;
  char *path;
};

struct Kotonoha_OrsTypePlayMovie {
  char *path;
  Uint64 a;
};

struct Kotonoha_OrsTypeFade {
  enum KotonohaOrsFadecolor color;
  bool a;
};

struct Kotonoha_OrsTypePathEnd {
  char *path;
};

struct Kotonoha_OrsTypePrintText {
  char *character;
  char *text;
};

struct Kotonoha_OrsTypePlayVoice {
  char *path;
  Uint64 a;
  char *character_short;
};

struct Kotonoha_OrsTypeUnknown {
  char *line;
};

struct KotonohaOrsSetSELECT {
  char **options;
  Uint64 size;
};

typedef union Kotonoha_OrsTypeGeneric {
  struct Kotonoha_OrsTypeCreateBg *create_bg;
  struct Kotonoha_OrsTypePlaySe *play_se;
  struct Kotonoha_OrsTypePlayMovie *play_movie;
  struct Kotonoha_OrsTypeFade *fade;
  struct Kotonoha_OrsTypePrintText *print_text;
  struct Kotonoha_OrsTypePlayVoice *play_voice;
  struct KotonohaOrsSetSELECT *set_select;
  struct Kotonoha_OrsTypePathEnd *path_end;
  struct Kotonoha_OrsTypeUnknown *unknown;
} Kotonoha_OrsTypeGeneric;

typedef struct Kotonoha_OrsEvent {
  Uint64 start;
  Uint64 end;
  Kotonoha_OrsType command;
  Kotonoha_OrsTypeGeneric data;
  struct Kotonoha_OrsEvent *next;
  struct Kotonoha_OrsEvent *prev;
  bool eventTouched;
} Kotonoha_OrsEvent;

typedef struct Kotonoha_OrsData {
  Kotonoha_OrsEvent *data;
  Kotonoha_OrsEvent *last;
  Uint64 size;
} Kotonoha_OrsData;

Kotonoha_OrsData KotonohaOrsParser(const char *input);

void KotonohaOrsClean(Kotonoha_OrsData *events);

void KotonohaOrsDelete(Kotonoha_OrsData *events, Kotonoha_OrsEvent *target);

#ifdef __cplusplus
}
#endif
