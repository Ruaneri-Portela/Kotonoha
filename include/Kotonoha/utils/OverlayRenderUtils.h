#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>

bool Kotonoha_OverlayGetMetrics(SDL_Window* window, SDL_Renderer* render,
	int* outputWidth, int* outputHeight, float* windowScale);

TTF_Font* Kotonoha_OverlayOpenFont(const char* path, float pointSize,
	float windowScale);

bool Kotonoha_OverlayCreateTextTexture(SDL_Renderer* render, TTF_Font* font,
	const char* text, SDL_Color color, SDL_Texture** texture,
	float* textWidth, float* textHeight);
