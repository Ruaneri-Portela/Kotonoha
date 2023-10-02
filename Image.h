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
                imageTemporary.surface = IMG_Load(filenameString.c_str());
                exportTo->image.push_back(imageTemporary);
            }
        };
    };
    int playImage(void *import)
    {
        kotonohaData::acessMapper *importedTo = static_cast<kotonohaData::acessMapper *>(import);
        importedTo->root->log0->appendLog("(Image) - Start");
        importedTo->root->log0->appendLog("(Image) - " + std::to_string(importedTo->image.size()) + " Images to show");
        while (!importedTo->control->exit)
        {
            if (!importedTo->control->imageEnd && !importedTo->control->nonImage && !(importedTo->image.size() == 0))
            {
                for (std::vector<kotonohaData::imageData>::size_type i = 0; i < importedTo->image.size(); i++)
                {
                    double timePass = importedTo->control->timer0.pushTime();
                    if (importedTo->image[i].play < timePass && importedTo->image[i].end > timePass && !importedTo->image[i].played)
                    {
                        if (!importedTo->image[i].touched)
                        {
                            importedTo->root->log0->appendLog("(Image) - Showing " + importedTo->image[i].path);
                            importedTo->image[i].touched = true;
                        }
                        int h = 0, w = 0;
                        SDL_GetWindowSize(importedTo->root->window, &w, &h);
                        SDL_Rect square = {0, 0, w, h};
                        SDL_Texture *texture = SDL_CreateTextureFromSurface(importedTo->root->renderer, importedTo->image[i].surface);
                        SDL_RenderCopy(importedTo->root->renderer, texture, NULL, &square);
                        SDL_DestroyTexture(texture);
                        importedTo->control->display[0] = false;
                        importedTo->control->display[1] = true;
                    }
                    else if (importedTo->image[i].end < timePass && !importedTo->image[i].played)
                    {
                        importedTo->image[i].played = true;
                        SDL_FreeSurface(importedTo->image[i].surface);
                        importedTo->image.erase(importedTo->image.begin() + i);
                        break;
                    }
                }
                importedTo->control->display[0] = false;
                importedTo->control->display[1] = true;
            }
            else
            {
                importedTo->control->display[0] = false;
                importedTo->control->display[1] = true;
            }
            if (importedTo->image.size() == 0 && !importedTo->control->imageEnd)
            {
                importedTo->control->imageEnd = true;
            }
        }
        importedTo->root->log0->appendLog("(Image) - End");
        return 0;
    }

};