#include <Kotonoha/utils/Time.h>

// Reseta a instância de Kotonoha_time
void Kotonoha_timeReset(struct Kotonoha_time *instance, bool resetStopped)
{
	if (instance == NULL)
		return;

	instance->startTime = resetStopped ? 0 : SDL_GetTicks();
	instance->pauseDuration = 0;
	instance->pauseTimeStart = 0;
	instance->pauseTime = 0;
	instance->isPaused = resetStopped;
	instance->seekTime = 0;
}

// Cria uma nova instância de Kotonoha_time
struct Kotonoha_time *Kotonoha_timeNew(bool startStopped)
{
	struct Kotonoha_time *time = (struct Kotonoha_time *)SDL_malloc(sizeof(struct Kotonoha_time));
	if (time == NULL)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for Kotonoha_time");
		return NULL;
	}

	Kotonoha_timeReset(time, startStopped);
	return time;
}

// Destrói a instância de Kotonoha_time
void Kotonoha_timeDestroy(struct Kotonoha_time *instance)
{
	if (instance != NULL)
	{
		SDL_free(instance);
	}
}

// Pausa a contagem de tempo de uma instância ativa de Kotonoha_time
void Kotonoha_timePause(struct Kotonoha_time *instance)
{
	if (instance == NULL || instance->isPaused)
		return;

	instance->pauseTimeStart = SDL_GetTicks();
	instance->pauseTime = Kotonoha_timeGet(instance);
	instance->isPaused = true;
}

// Retoma a contagem de tempo de uma instância pausada de Kotonoha_time
void Kotonoha_timeResume(struct Kotonoha_time *instance)
{
	if (instance == NULL || !instance->isPaused)
		return;

	instance->isPaused = false;

	if (instance->startTime == 0)
	{
		instance->startTime = SDL_GetTicks();
		return;
	}

	instance->pauseDuration += SDL_GetTicks() - instance->pauseTimeStart;
	instance->pauseTimeStart = 0;
}

// Obtém o tempo atual da instância (ou o tempo pausado, se estiver pausada)
Uint64 Kotonoha_timeGet(struct Kotonoha_time *instance)
{
	if (instance == NULL)
		return 0;

	if (instance->isPaused)
	{
		return instance->pauseTime;
	}

	return SDL_GetTicks() - (instance->startTime + instance->pauseDuration) + instance->seekTime;
}

// Define um tempo específico na instância de Kotonoha_time
void Kotonoha_timeSet(struct Kotonoha_time *instance, Uint64 time)
{
	if (instance == NULL)
		return;

	instance->startTime = SDL_GetTicks() - time;
}

// Obtém o tempo em relação a um intervalo de evento e verifica se está dentro do intervalo
Uint64 Kotonoha_timeGetFromEvent(
	struct Kotonoha_time *instance,
	Uint64 start,
	Uint64 end,
	bool *inRange,
	int *diff)
{
	if (instance == NULL || inRange == NULL || diff == NULL)
		return 0;

	Uint64 currentTime = Kotonoha_timeGet(instance);

	if (currentTime < start || currentTime > end)
	{
		*inRange = false;
		*diff = (int)(currentTime - end);
		return 0;
	}

	*inRange = true;
	*diff = 0;
	return currentTime - start;
}
