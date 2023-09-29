#ifdef _MSC_VER
#include <SDL.h>
#include <SDL_image.h>
#else
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#endif
typedef struct image
{
    char *filename;
    double startTime;
    double endTime;
    struct image *next;
    struct image *prev;
};
class ImageEngine
{
public:
    image *engine = NULL;
    image *temp = NULL;
    void load(char *filename, double startTime, double endTime)
    {
        if (engine == NULL)
        {
            engine = new image;
            engine->endTime = endTime;
            engine->startTime = startTime;
            engine->next = NULL;
            engine->prev = NULL;
        }
        else
        {
            temp = engine;
            while (true)
            {
                if (temp->next == NULL)
                {
                    temp = new image;
                    temp->endTime = endTime;
                    temp->next = NULL;
                    temp->prev = engine;
                }else{
                    temp = temp->next;
                }
            }
        }
    };
    void run(){

    };
    void destroy(){

    };
};
// Rendenizador dos itens de fundo e GUI
class RenderImage
{
public:
    SDL_Texture *texture = NULL;
    SDL_Texture *back = NULL;
    int scoreGame = 0;
    void loadImage(SDL_Renderer *renderer, const char *filename)
    {
        SDL_Surface *surface = IMG_Load(filename);
        texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
    };
    void drawImage(SDL_Renderer *renderer)
    {
        SDL_RenderCopy(renderer, texture, NULL, NULL);
    };
    void destroyImage()
    {
        SDL_DestroyTexture(texture);
    };
};
