#pragma once
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define MIMUMUM_LINE_SIZE 1024

enum KotonohaOrsType {
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
};

enum KotonohaOrsFadecolor { BLACK, WHITE };

struct KotonohaOrsTypeCreateBg {
  char *a;
  char *path;
};

struct KotonohaOrsTypePlaySe {
  Uint64 a;
  char *path;
};

struct KotonohaOrsTypePlayMovie {
  char *path;
  Uint64 a;
};

struct KotonohaOrsTypeFade {
  enum KotonohaOrsFadecolor color;
  bool a;
};

struct KotonohaOrsTypePathEnd {
  char *path;
};

struct KotonohaOrsTypePrintText {
  char *character;
  char *text;
};

struct KotonohaOrsTypePlayVoice {
  char *path;
  Uint64 a;
  char *character_short;
};

struct KotonohaOrsTypeUnknown {
  char *line;
};

struct KotonohaOrsSetSELECT {
  char **options;
  Uint64 size;
};

typedef union Kotonoha_OrsTypeGeneric {
  struct KotonohaOrsTypeCreateBg *create_bg;
  struct KotonohaOrsTypePlaySe *play_se;
  struct KotonohaOrsTypePlayMovie *play_movie;
  struct KotonohaOrsTypeFade *fade;
  struct KotonohaOrsTypePrintText *print_text;
  struct KotonohaOrsTypePlayVoice *play_voice;
  struct KotonohaOrsSetSELECT *set_select;
  struct KotonohaOrsTypePathEnd *path_end;
  struct KotonohaOrsTypeUnknown *unknown;
} Kotonoha_OrsTypeGeneric;

typedef struct Kotonoha_OrsEvent {
  Uint64 start;
  Uint64 end;
  enum KotonohaOrsType command;
  union Kotonoha_OrsTypeGeneric data;
  struct Kotonoha_OrsEvent *next;
  struct Kotonoha_OrsEvent *prev;
  bool eventTouched;
} Kotonoha_OrsEvent;

typedef struct Kotonoha_OrsData {
  struct Kotonoha_OrsEvent *data;
  struct Kotonoha_OrsEvent *last;
  Uint64 size;
} Kotonoha_OrsData;

Kotonoha_OrsData KotonohaOrsParser(const char *input);

void KotonohaOrsClean(Kotonoha_OrsData *events);

void KotonohaOrsDelete(Kotonoha_OrsData *events, Kotonoha_OrsEvent *target);