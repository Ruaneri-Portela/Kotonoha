#pragma once
/*
This a part of Kotonoha project, copyright 2024

# Kotonoha.h

Kotonoha.h is a C binding for Kotonoha.hpp
How as SDL3 define as some subrotines from replace classic main function, Kotonoha::Kotonoha class is exposed from a (void)* pointer

The global context is as defined by appContext

# KOTONOHA_SCENE_CALL
To KOTONOHA_SCENE_CALL macro, is to two uses for renders and scenes

Your mission is passed render control to new funciontion, to keep event stack as uses a custrom linked array named as eventQueu, when uses inside Kotonoha_SCENE_CALL DONT forget
 NOT CLEAN THE EVENT STACK, this is a job for Kotonoha::Kotonoha::Main() method
*/

#include <ass/ass.h>
#include <Kotonoha/utils/UserEvents.h>
#include <SDL3/SDL.h>

enum Kotonoha_Scene_Status
{
	KOTONOHA_SCENE_NULL,
	KOTONOHA_SCENE_WAITING,
	KOTONOHA_SCENE_DRAW,
	KOTONOHA_SCENE_DRAW_LAST,
	KOTONOHA_SCENE_DRAW_OVERLAYED,
	KOTONOHA_SCENE_COMPLETE,
	KOTONOHA_SCENE_FATAL_ERROR,
	KOTONOHA_SCENE_CLOSE_APPLICATION
};

struct Kotonoha_Game
{
	SDL_Window *window;
	SDL_Renderer *render;
	SDL_Mutex *taskLock;
	SDL_WindowFlags flags;

	ASS_Library *ass_library;
	ASS_Renderer *ass_renderer;

	char *assetsPath, *configPath, *styleStr;

	bool showFps, paused;
	int vsync;

	struct Kotonoha_eventStack eventQueu;
	void *soundCtx;
	void *processPoolTasks;
};

#define KOTONOHA_SCENE_CALL SDL_Window *window, SDL_Renderer *render, struct Kotonoha_eventStack *eventQueu, void *userData, SDL_Texture *target

#define KOTONOHA_AUDIO_COMPONENTS int (*function)(void *parms, Uint8 **target, int *size), void (*closeFunction)(void *parms), void *parms