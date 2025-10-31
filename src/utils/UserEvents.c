#include <Kotonoha/utils/UserEvents.h>

// Remove e retorna o evento do topo da pilha
SDL_Event Kotonoha_eventPop(struct Kotonoha_eventStack *stack)
{
	SDL_Event empty = {0}; // Evento vazio

	if (stack == NULL)
		return empty; // Verifica se a pilha é válida

	SDL_LockMutex(stack->mutex); // Bloqueia o mutex para manipulação segura

	struct Kotonoha_eventQueue *topEvent = stack->base; // Acessa o evento no topo
	if (topEvent == NULL)
	{
		SDL_UnlockMutex(stack->mutex); // Libera o mutex antes de retornar
		return empty;
	}

	// Remove o evento do topo da pilha
	stack->base = topEvent->next;
	SDL_Event poppedEvent = topEvent->event; // Salva o evento para retorno
	SDL_free(topEvent);						 // Libera a memória do evento
	stack->size--;							 // Atualiza o tamanho da pilha

	SDL_UnlockMutex(stack->mutex); // Libera o mutex
	return poppedEvent;
}

// Adiciona um evento no topo da pilha
void Kotonoha_eventPush(struct Kotonoha_eventStack *stack, SDL_Event event)
{
	if (stack == NULL)
		return;

	// Inicializa o mutex se ainda não estiver criado
	if (stack->mutex == NULL)
	{
		stack->mutex = SDL_CreateMutex();
	}

	// Aloca memória para o novo evento
	struct Kotonoha_eventQueue *newEvent = (struct Kotonoha_eventQueue *)SDL_malloc(sizeof(struct Kotonoha_eventQueue));
	if (newEvent == NULL)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Falha na alocação de memória para evento");
		return;
	}

	SDL_LockMutex(stack->mutex); // Bloqueia o mutex

	// Preenche os dados do novo evento
	newEvent->event = event;
	newEvent->next = NULL;

	// Adiciona o evento à pilha
	if (stack->size == 0)
	{
		stack->base = newEvent;
		stack->top = newEvent;
	}
	else
	{
		stack->top->next = newEvent;
		stack->top = newEvent;
	}

	stack->size++;				   // Atualiza o tamanho da pilha
	SDL_UnlockMutex(stack->mutex); // Libera o mutex
}

// Libera todos os eventos na pilha e destrói a pilha
void Kotonoha_eventFree(struct Kotonoha_eventStack *stack)
{
	if (stack == NULL)
		return;

	struct Kotonoha_eventQueue *current = stack->base; // Inicia na base da pilha
	struct Kotonoha_eventQueue *next = NULL;

	// Redefine a pilha e destrói o mutex
	stack->top = NULL;
	stack->size = 0;
	stack->base = NULL;
	SDL_DestroyMutex(stack->mutex);
	stack->mutex = NULL;

	// Libera cada evento na pilha
	while (current != NULL)
	{
		next = current->next;
		SDL_free(current);
		current = next;
	}
}

// Lê o próximo evento da pilha sem removê-lo
SDL_Event Kotonoha_eventRead(struct Kotonoha_eventStack *stack, void **persistent)
{
	SDL_Event empty = {0};

	if (stack == NULL)
		return empty;

	// Inicializa o ponteiro persistente se for NULL
	if (*persistent == NULL)
	{
		*persistent = stack->base;
	}

	SDL_LockMutex(stack->mutex); // Bloqueia o mutex

	struct Kotonoha_eventQueue *currentEvent = (struct Kotonoha_eventQueue *)*persistent;
	if (currentEvent == NULL)
	{
		SDL_UnlockMutex(stack->mutex);
		return empty;
	}

	*persistent = currentEvent->next; // Atualiza o ponteiro persistente para o próximo evento
	SDL_UnlockMutex(stack->mutex);
	return currentEvent->event;
}
