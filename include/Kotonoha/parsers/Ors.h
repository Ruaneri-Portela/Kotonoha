#pragma once
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define MIMUMUM_LINE_SIZE 1024

enum Kotonoha_orsType {
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

enum Kotonoha_orsFadecolor { BLACK, WHITE };

struct Kotonoha_orsTypeCreateBg {
  char *a;
  char *path;
};

struct Kotonoha_orsTypePlaySe {
  Uint64 a;
  char *path;
};

struct Kotonoha_orsTypePlayMovie {
  char *path;
  Uint64 a;
};

struct Kotonoha_orsTypeFade {
  enum Kotonoha_orsFadecolor color;
  bool a;
};

struct Kotonoha_orsTypePathEnd {
  char *path;
};

struct Kotonoha_orsTypePrintText {
  char *character;
  char *text;
};

struct Kotonoha_orsTypePlayVoice {
  char *path;
  Uint64 a;
  char *character_short;
};

struct Kotonoha_orsTypeUnknown {
  char *line;
};

struct Kotonoha_orsSetSELECT {
  char **options;
  Uint64 size;
};

union Kotonoha_orsTypeGeneric {
  struct Kotonoha_orsTypeCreateBg *create_bg;
  struct Kotonoha_orsTypePlaySe *play_se;
  struct Kotonoha_orsTypePlayMovie *play_movie;
  struct Kotonoha_orsTypeFade *fade;
  struct Kotonoha_orsTypePrintText *print_text;
  struct Kotonoha_orsTypePlayVoice *play_voice;
  struct Kotonoha_orsSetSELECT *set_select;
  struct Kotonoha_orsTypePathEnd *path_end;
  struct Kotonoha_orsTypeUnknown *unknown;
};

struct Kotonoha_orsEvent {
  Uint64 start;
  Uint64 end;
  enum Kotonoha_orsType command;
  union Kotonoha_orsTypeGeneric data;
  struct Kotonoha_orsEvent *next;
  struct Kotonoha_orsEvent *prev;
  bool eventTouched;
};

struct Kotonoha_orsData {
  struct Kotonoha_orsEvent *data;
  struct Kotonoha_orsEvent *last;
  Uint64 size;
};

struct Kotonoha_orsData Kotonoha_OrsParser(const char *input);

void Kotonoha_OrsClean(struct Kotonoha_orsData *events);

void Kotonoka_OrsDelete(struct Kotonoha_orsData *events,
                        struct Kotonoha_orsEvent *target);