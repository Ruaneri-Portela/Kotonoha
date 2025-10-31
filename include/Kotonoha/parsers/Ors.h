#pragma once
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define MIMUMUM_LINE_SIZE 1024

enum Kotonoha_orsType
{
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

enum Kotonoha_orsFadecolor
{
	BLACK,
	WHITE
};

struct Kotonoha_orsTypeCreateBg
{
	wchar_t *a;
	wchar_t *path;
	Uint64 end;
};

struct Kotonoha_orsTypePlaySe
{
	Uint64 a;
	wchar_t *path;
	Uint64 end;
};

struct Kotonoha_orsTypePlayMovie
{
	wchar_t *path;
	Uint64 a;
	Uint64 end;
};

struct Kotonoha_orsTypeFade
{
	enum Kotonoha_orsFadecolor color;
	bool a;
	Uint64 end;
};

struct Kotonoha_orsTypePathEnd
{
	wchar_t *path;
	Uint64 end;
};

struct Kotonoha_orsTypePrintText
{
	wchar_t *character;
	wchar_t *text;
	Uint64 end;
};

struct Kotonoha_orsTypePlayVoice
{
	wchar_t *path;
	Uint64 a;
	wchar_t *character_short;
	Uint64 end;
};

struct Kotonoha_orsTypeUnknown
{
	wchar_t *line;
};

struct Kotonoha_orsSetSELECT
{
	wchar_t **options;
	Uint64 size;
	Uint64 end;
};

union Kotonoha_orsTypeGeneric
{
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

struct Kotonoha_orsEvent
{
	Uint64 start;
	enum Kotonoha_orsType command;
	union Kotonoha_orsTypeGeneric data;
	struct Kotonoha_orsEvent *next;
	struct Kotonoha_orsEvent *prev;
	bool eventTouched;
};

struct Kotonoha_orsData
{
	struct Kotonoha_orsEvent *data;
	struct Kotonoha_orsEvent *last;
	Uint64 size;
};

struct Kotonoha_orsData Kotonoha_OrsParser(const char *input);

void Kotonoha_OrsClean(struct Kotonoha_orsData *events);

void Kotonoka_OrsDelete(struct Kotonoha_orsData *events, struct Kotonoha_orsEvent *target);