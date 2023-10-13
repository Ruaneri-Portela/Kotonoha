namespace kotonoha
{
    class prompt
    {
    private:
        TTF_Font *font;
        SDL_Color color = {255, 255, 255};
        SDL_Rect rect[4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
        int h, w;
        int bH, bW;
        int rectCount = -1;

    public:
        int init()
        {
            font = TTF_OpenFont("YoungSerif-Regular.ttf", 50);
            return 0;
        };
        int show(SDL_Renderer *renderer, SDL_Window *window)
        {
            SDL_GetWindowSize(window, &w, &h);
            rectCount = 3;
            switch (rectCount)
            {
            case 1:
                rect[0] = {w / 2, h / 2, 0, 0};
                break;
            case 2:
                rect[0] = {w / 4, h / 2, 0, 0};
                rect[1] = {(w / 4) * 3, h / 2, 0, 0};
                break;
            case 3:
                rect[0] = {w / 4, h / 2, 0, 0};
                rect[1] = {w / 4 * 2, h / 2, 0, 0};
                rect[2] = {w / 4 * 3, h / 2, 0, 0};
                break;
            case 4:
                rect[0] = {w / 4, h / 4, 0, 0};
                rect[1] = {w / 4, (h / 4) * 3, 0, 0};
                rect[2] = {(w / 4) * 3, h / 4, 0, 0};
                rect[3] = {(w / 4) * 3, (h / 4) * 3, 0, 0};
                break;
            default:
                break;
            }
            for (int i = 0; i <= rectCount - 1; i++)
            {
                std::string text = "Prompt " + std::to_string(i + 1);
                SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
                SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
                SDL_QueryTexture(texture, NULL, NULL, &bW, &bH);
                rect[i] = {rect[i].x - bW / 2, rect[i].y - bH / 2, bW, bH};
                SDL_RenderCopy(renderer, texture, NULL, &rect[i]);
                SDL_FreeSurface(surface);
                SDL_DestroyTexture(texture);
            }
            return 0;
        };
        int detectTouch(SDL_Event *event)
        {
            int returnValue = 0;
            if (event->type == SDL_MOUSEBUTTONDOWN)
            {
                if (event->button.button == SDL_BUTTON_LEFT)
                {
                    for (int i = 0; i <= rectCount - 1; i++)
                    {
                        event->button.x > rect[i].x and event->button.x<rect[i].x + rect[i].w and event->button.y> rect[i].y and event->button.y < rect[i].y + rect[i].h ? returnValue = i + 1 : 0;
                    }
                }
            }
            return returnValue;
        };
        int close()
        {
            TTF_CloseFont(font);
            return 0;
        };
    };
}
