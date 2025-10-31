#pragma once
#include <SDL3/SDL.h>
struct Kotonoha_eventQueue
{
	SDL_Event event;
	struct Kotonoha_eventQueue *next;
};

struct Kotonoha_eventStack
{
	struct Kotonoha_eventQueue *base, *top;
	Sint16 size;
	SDL_Mutex *mutex;
};

SDL_Event Kotonoha_eventPop(struct Kotonoha_eventStack *stack);

void Kotonoha_eventPush(struct Kotonoha_eventStack *stack, SDL_Event event);

void Kotonoha_eventFree(struct Kotonoha_eventStack *stack);

SDL_Event Kotonoha_eventRead(struct Kotonoha_eventStack *stack, void **persistent);