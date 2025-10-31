#include <Kotonoha/renders/TextRender.h>

// Função para desenhar a textura a partir de uma imagem ASS
static SDL_Texture *Kotonoha_TextRenderDrawText(SDL_Renderer *render, ASS_Image *img)
{
    // Cria a textura com o formato adequado
    SDL_Texture *texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_STREAMING, img->w, img->h);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    if (!texture)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_CreateTexture failed: %s", SDL_GetError());
        return NULL;
    }

    // Obtém o ponteiro para os pixels da textura
    uint32_t *pixels;
    int pitch = 0;
    if (SDL_LockTexture(texture, NULL, (void **)&pixels, &pitch) == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_LockTexture failed: %s", SDL_GetError());
        SDL_DestroyTexture(texture);
        return NULL;
    }

    // Preenche os pixels com base no bitmap da imagem ASS
    unsigned char *bitmap = img->bitmap;
    for (int y = 0; y < img->h; y++)
    {
        for (int x = 0; x < img->w; x++)
        {
            pixels[x] = (bitmap[x] == 0) ? 0 : (img->color & 0xffffff00) | bitmap[x];
        }
        pixels = (uint32_t *)((uintptr_t)pixels + pitch);
        bitmap += img->stride;
    }

    SDL_UnlockTexture(texture);
    return texture;
}

// Função para inicializar os dados de renderização de texto
struct Kotonoha_textData *Kotonoha_TextRenderInit(struct Kotonoha_time *time, struct Kotonoha_Game *gameCtx)
{
    struct Kotonoha_textData *object = SDL_malloc(sizeof(struct Kotonoha_textData));
    if (!object)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate memory for Kotonoha_textData");
        return NULL;
    }

    // Inicializa os dados do objeto de renderização de texto
    object->ass_library = gameCtx->ass_library;
    object->ass_renderer = gameCtx->ass_renderer;
    object->track = NULL;
    object->time = time;
    object->lastFrame = NULL;
    object->subTexture = NULL;

    return object;
}

// Função para destruir os dados de renderização de texto e liberar a memória
void Kotonoha_TextRenderShutdown(struct Kotonoha_textData **object)
{
    if (object && *object)
    {
        if ((*object)->subTexture)
        {
            SDL_DestroyTexture((*object)->subTexture);
        }
        if ((*object)->track)
        {
            ass_free_track((*object)->track);
        }
        SDL_free(*object);
        *object = NULL;
    }
}

// Função principal para renderizar o texto
enum Kotonoha_Scene_Status Kotonoha_TextRenderDraw(KOTONOHA_SCENE_CALL)
{
    if (!userData)
    {
        return KOTONOHA_SCENE_FATAL_ERROR;
    }

    struct Kotonoha_textData *environment = (struct Kotonoha_textData *)userData;

    // Caso o track esteja vazio, espera até que seja preenchido
    if (!environment->track)
    {
        return KOTONOHA_SCENE_WAITING;
    }

    // Configura o renderizador
    ass_set_storage_size(environment->ass_renderer, target->w, target->h);
    ass_set_frame_size(environment->ass_renderer, target->w, target->h);

    int asChanged = 0;
    ASS_Image *frame = ass_render_frame(environment->ass_renderer, environment->track,
                                        Kotonoha_timeGet(environment->time), &asChanged);

    if (asChanged)
    {
        SDL_RenderClear(render);

        // Se já houver sub-textura, libera-a antes de criar uma nova
        if (environment->subTexture)
        {
            SDL_DestroyTexture(environment->subTexture);
        }

        // Cria uma nova textura de destino para o sub-texto
        environment->subTexture = SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA8888,
                                                    SDL_TEXTUREACCESS_TARGET, target->w, target->h);
        if (!environment->subTexture)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create subTexture: %s", SDL_GetError());
            return KOTONOHA_SCENE_FATAL_ERROR;
        }

        // Define o render target como a sub-textura e limpa o renderizador
        SDL_SetRenderTarget(render, environment->subTexture);
        SDL_RenderClear(render);

        // Atualiza o último quadro renderizado
        environment->lastFrame = frame;

        // Se não houver quadro, continua esperando
        if (!environment->lastFrame)
        {
            return KOTONOHA_SCENE_WAITING;
        }

        // Renderiza cada imagem do quadro
        for (ASS_Image *img = frame; img; img = img->next)
        {
            SDL_Texture *texture = Kotonoha_TextRenderDrawText(render, img);
            if (!texture)
                continue;

            // Define a posição e tamanho da imagem
            SDL_FRect dst = {(float)img->dst_x, (float)img->dst_y, (float)img->w,
                             (float)img->h};
            SDL_RenderTexture(render, texture, NULL, &dst);

            // Libera a textura temporária
            SDL_DestroyTexture(texture);
        }

        // Restaura o render target para o alvo original
        SDL_SetRenderTarget(render, target);

        // Renderiza a sub-textura no alvo final
        SDL_RenderTexture(render, environment->subTexture, NULL, NULL);
    }

    // Caso não haja quadro renderizado, continua esperando
    if (!environment->lastFrame)
    {
        return KOTONOHA_SCENE_WAITING;
    }

    return KOTONOHA_SCENE_DRAW_OVERLAYED; // Continua com a aplicação
}
