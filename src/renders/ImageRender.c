#include <Kotonoha/renders/ImageRender.h>

// Função auxiliar para liberar recursos de FFmpeg e SDL
static void cleanupResources(AVFrame *frame, AVPacket *packet, struct SwsContext *swsCtx)
{
    if (frame)
        av_frame_free(&frame);
    if (packet)
        av_packet_unref(packet);
    if (swsCtx)
        sws_freeContext(swsCtx);
}

// Função intermediária para carregar e converter o primeiro frame em RGBA
static AVFrame *renderFrame(const char *path, int h, int w, Uint8 **buffer)
{
    AVFormatContext *fmtCtx = NULL;
    AVCodecContext *codecCtx = NULL;
    struct SwsContext *swsCtx = NULL;
    AVPacket packet;
    int imageStreamIndex, response;

    // Carregar o formato de vídeo e inicializar o codec
    if (!Kotonoha_UtilsFFmpegLoad(path, &fmtCtx, &codecCtx, AVMEDIA_TYPE_VIDEO, &imageStreamIndex,
                                  0, NULL))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load video: %s\n", path);
        return NULL;
    }

    // Ler pacotes de vídeo e decodificar o primeiro frame
    while (av_read_frame(fmtCtx, &packet) >= 0)
    {
        if (packet.stream_index == imageStreamIndex)
        {
            // Enviar o pacote para o decodificador
            response = avcodec_send_packet(codecCtx, &packet);
            if (response < 0)
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error sending packet to codec: %s\n",
                             av_err2str(response));
                av_packet_unref(&packet);
                continue;
            }

            // Alocar frame e receber o frame decodificado
            AVFrame *frame = av_frame_alloc();
            if (!frame)
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error allocating frame\n");
                cleanupResources(frame, &packet, NULL);
                return NULL;
            }

            response = avcodec_receive_frame(codecCtx, frame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
            {
                cleanupResources(frame, &packet, NULL);
                continue;
            }
            else if (response < 0)
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error receiving frame: %s\n",
                             av_err2str(response));
                cleanupResources(frame, &packet, NULL);
                return NULL;
            }

            // Definir altura e largura padrão se não fornecidos
            h = (h == -1) ? codecCtx->height : h;
            w = (w == -1) ? codecCtx->width : w;

            // Criar contexto de conversão de pixel para RGBA
            swsCtx = Kotonoha_UtilsFFmpegCreateSwsContext(codecCtx, codecCtx->pix_fmt, w, h);
            if (!swsCtx)
            {
                cleanupResources(frame, &packet, swsCtx);
                return NULL;
            }

            // Alocar frame para dados em formato RGBA
            AVFrame *pFrameRGB = Kotonoha_UtilsFFmpegSwsConvertFrame(swsCtx, frame, buffer, h, w);
            if (!pFrameRGB)
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error converting frame to RGBA\n");
                cleanupResources(frame, &packet, swsCtx);
                return NULL;
            }
            pFrameRGB->width = w;
            pFrameRGB->height = h;

            // Liberar recursos intermediários e retornar o frame convertido
            cleanupResources(frame, &packet, swsCtx);
            avcodec_free_context(&codecCtx);
            avformat_close_input(&fmtCtx);
            return pFrameRGB;
        }
        av_packet_unref(&packet);
    }

    // Liberar recursos do FFmpeg
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    return NULL;
}

// Função para criar textura SDL a partir de um frame RGBA
SDL_Texture *Kotonoha_imageCreateTexture(SDL_Renderer *render, const char *path, int h, int w)
{
    Uint8 *buffer = NULL;
    AVFrame *pFrameRGB = renderFrame(path, h, w, &buffer);
    if (!pFrameRGB)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to render frame\n");
        return NULL;
    }

    SDL_Texture *texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STATIC,
                                             pFrameRGB->width, pFrameRGB->height);
    if (texture)
    {
        SDL_UpdateTexture(texture, NULL, pFrameRGB->data[0], pFrameRGB->linesize[0]);
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create texture: %s\n", SDL_GetError());
    }

    // Liberar recursos
    av_free(buffer);
    av_frame_free(&pFrameRGB);
    return texture;
}

// Função para criar surface SDL a partir de um frame RGBA
SDL_Surface *Kotonoha_imageCreateSurface(const char *path, int h, int w)
{
    Uint8 *buffer = NULL;
    AVFrame *pFrameRGB = renderFrame(path, h, w, &buffer);
    if (!pFrameRGB)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to render frame\n");
        return NULL;
    }

    SDL_Surface *surface = SDL_CreateSurface(pFrameRGB->width, pFrameRGB->height,
                                             SDL_PIXELFORMAT_RGBA32);

    if (!surface)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create surface: %s\n", SDL_GetError());
    }
    else
    {
        SDL_LockSurface(surface);
        SDL_memcpy(surface->pixels, pFrameRGB->data[0], pFrameRGB->linesize[0] * pFrameRGB->height);
        SDL_UnlockSurface(surface);
    }

    // Liberar recursos
    av_free(buffer);
    av_frame_free(&pFrameRGB);
    return surface;
}
