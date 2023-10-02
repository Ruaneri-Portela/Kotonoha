namespace kotonoha
{
    class imageObject
    {
    public:
        kotonohaData::acessMapper *exportTo = NULL;
        void push(std::string filenameString, std::string startTime, std::string endTime)
        {
            if (atoi(filenameString.c_str()) != 1)
            {
                std::stringstream ss;
                ss << "./Midia/";
                ss << filenameString;
                ss << ".PNG";
                std::string filenameStr = ss.str();
                // Create new image object
                kotonohaData::imageData imageTemporary;
                imageTemporary.path = filenameStr;
                imageTemporary.play = kotonohaTime::convertToTime(startTime);
                imageTemporary.end = kotonohaTime::convertToTime(endTime);
                exportTo->image.push_back(imageTemporary);
            }
        };
    };
    int playImage(void *import)
    {
        kotonohaData::acessMapper *importedTo = static_cast<kotonohaData::acessMapper *>(import);
        std::vector<kotonohaData::imageData> data = importedTo->image;
        kotonohaData::rootData *root = importedTo->root;
        kotonohaData::controlData *control = importedTo->control;
        int h = 0, w = 0;
        root->log0->appendLog("(Image) - Start");
        while (!control->exit)
        {
            if (!control->imageEnd)
            {
                for (std::vector<kotonohaData::imageData>::size_type i = 0; i < data.size(); i++)
                {
                    double timePass = control->timer0.pushTime();
                    if (data[i].texture == NULL)
                    {
                        SDL_Surface *surface = IMG_Load(data[i].path.c_str());
                        data[i].texture = SDL_CreateTextureFromSurface(root->renderer, surface);
                        root->log0->appendLog("(Image) - Loading "+data[i].path);
                        SDL_FreeSurface(surface);
                    }
                    else if (data[i].play < timePass && data[i].end > timePass)
                    {
                        SDL_GetWindowSize(root->window, &w, &h);
                        SDL_Rect square = {0, 0, w, h};
                        SDL_RenderCopy(root->renderer, data[i].texture, NULL, &square);

                    }
                    else if (data[i].end < timePass)
                    {
                        //data.erase(data.begin() + i);
                        //i = 0;
                        //SDL_DestroyTexture(data[i].texture);
                    }
                    if (data.size() == 0)
                    {
                        control->imageEnd = true;
                        goto END;
                    }
                }
            }
            END:
            control->display[0] = false;
            control->display[1] = true;
        }
        root->log0->appendLog("(Image) - End");
        return 0;
    }

};