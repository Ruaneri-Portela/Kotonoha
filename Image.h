namespace image
{
    class imageObject
    {
    public:
        imageData *data = NULL;
        void push(std::string filenameString, std::string startTime, std::string endTime, drawControl *dataDraw, SDL_Window *window, SDL_Renderer *renderer)
        {

            if (atoi(filenameString.c_str()) != 1)
            {
                std::stringstream ss;
                ss << "./Midia/";
                ss << filenameString;
                ss << ".PNG";
                std::string filenameStr = ss.str();
                imageData *imageTemporary;
                imageTemporary = new imageData;
                imageTemporary->filename = filenameStr;
                imageTemporary->timeToPlay = timeUtils::convertToTime(startTime);
                imageTemporary->timeToEnd = timeUtils::convertToTime(endTime);
                imageTemporary->next = NULL;
                imageTemporary->prev = NULL;
                imageTemporary->renderer = renderer;
                imageTemporary->window = window;
                imageTemporary->dataDraw = dataDraw;
                if (data == NULL)
                {
                    data = imageTemporary;
                }
                else
                {
                    imageData *search = data;
                    while (search->next != NULL)
                    {
                        search = search->next;
                    }
                    search->next = imageTemporary;
                    search->next->prev = search;
                }
            }
        };
    };
    int play(void *import)
    {
        if (import != NULL)
        {
            imageData *data = static_cast<imageData *>(import);
            imageData *search = NULL;
            bool cache = false;
            int h = 0, w = 0;
            std::cout << "Image init" << std::endl;
            while (!data->dataDraw->exit)
            {
                cache = false;
                while (!data->dataDraw->imageD)
                {
                }
                if (data != NULL)
                {
                    search = data;

                    while (search != NULL)
                    {
                        if (search->texture == NULL)
                        {
                            SDL_Surface *surface = IMG_Load(search->filename.c_str());
                            search->texture = SDL_CreateTextureFromSurface(search->renderer, surface);
                            SDL_FreeSurface(surface);
                        }
                        if (search->timeToPlay <= data->dataDraw->timer0.pushTime())
                        {
                            SDL_GetWindowSize(data->window, &w, &h);
                            SDL_Rect square = {0, 0, w, h};
                            SDL_RenderCopy(data->renderer, data->texture, NULL, &square);
                            cache = true;
                        }
                        else if (search->timeToEnd <= data->dataDraw->timer0.pushTime())
                        {
                            SDL_DestroyTexture(data->texture);
                        }
                        search = search->next;
                    }
                }
                data->dataDraw->imageD = false;
                data->dataDraw->uiD = true;
                if (!cache)
                {
                    data->dataDraw->imageE = true;
                }
            };
            std::cout << "Image end" << std::endl;
        }

        return 0;
    };
}