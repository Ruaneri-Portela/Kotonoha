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
        importedTo->root->log0->appendLog("(Image) - Start");
        importedTo->root->log0->appendLog("(Image) - "+std::to_string(importedTo->image.size()) + " Images to show");
        while (!importedTo->control->exit)
        {
            if (!importedTo->control->imageEnd && !importedTo->control->nonImage && !(importedTo->image.size() == 0))
            {
                for (std::vector<kotonohaData::imageData>::size_type i = 0; i < importedTo->image.size(); i++)
                {
                    double timePass = importedTo->control->timer0.pushTime();
                    if (importedTo->image[i].texture == NULL)
                    {
                        SDL_Surface *surface = IMG_Load(importedTo->image[i].path.c_str());
                        importedTo->image[i].texture = SDL_CreateTextureFromSurface(importedTo->root->renderer, surface);
                        importedTo->root->log0->appendLog("(Image) - Loading " + importedTo->image[i].path);
                        SDL_FreeSurface(surface);
                    }
                    else if (importedTo->image[i].play < timePass && importedTo->image[i].end > timePass && !importedTo->image[i].played)
                    {
                        int h = 0, w = 0;
                        SDL_GetWindowSize(importedTo->root->window, &w, &h);
                        SDL_Rect square = {0, 0, w, h};
                        SDL_RenderCopy(importedTo->root->renderer, importedTo->image[i].texture, NULL, &square);
                    }
                    else if (importedTo->image[i].end < timePass)
                    {
                        importedTo->image[i].played=true;
                        importedTo->image.erase(importedTo->image.begin() + i);
                        SDL_DestroyTexture(importedTo->image[i].texture);
                        break;
                    }
                }
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