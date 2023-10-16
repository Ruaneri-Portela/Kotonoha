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
                ss << exportTo->root->fileConfigs->mediaPath;
                ss << filenameString;
                ss << exportTo->root->fileConfigs->imageExtension;
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
        importedTo->root->log0->appendLog("(Image) - " + std::to_string(importedTo->image.size()) + " Images to show");
        double timePass = 0.0;
        int h = 0, w = 0;
        while (importedTo->control->outCode == 0)
        {
            if (importedTo->control->display[0])
            {
                if (!importedTo->control->imageEnd && !importedTo->control->nonImage && !(importedTo->image.size() == 0))
                {
                    for (std::vector<kotonohaData::imageData>::size_type i = 0; i < importedTo->image.size(); i++)
                    {
                        timePass = importedTo->control->timer0.pushTime();
                        // Load the image if necessary
                        if (importedTo->image[i].texture == NULL && importedTo->image[i].play < timePass + 20)
                        {
                            importedTo->root->log0->appendLog("(Image) - Loading... " + importedTo->image[i].path);
                            importedTo->image[i].texture = IMG_LoadTexture(importedTo->root->renderer, importedTo->image[i].path.c_str());
                            if (importedTo->image[i].texture == NULL)
                            {
                                importedTo->root->log0->appendLog("(Image) - Error on load file" + importedTo->audio[i].path);
                                importedTo->image.erase(importedTo->image.begin() + i);
                            }
                        }
                        // Play image on renderer
                        else if (importedTo->image[i].play < timePass)
                        {
                            if (!importedTo->image[i].played)
                            {
                                importedTo->root->log0->appendLog("(Image) - Showing " + importedTo->image[i].path);
                                importedTo->image[i].played = true;
                            }
                            SDL_GetWindowSize(importedTo->root->window, &w, &h);
                            SDL_Rect square = {0, 0, w, h};
                            SDL_RenderCopy(importedTo->root->renderer, importedTo->image[i].texture, NULL, &square);
                        }
                        // Delete image texture from ram
                        if (importedTo->image[i].end < timePass)
                        {
                            SDL_DestroyTexture(importedTo->image[i].texture);
                            importedTo->image.erase(importedTo->image.begin() + i);
                            importedTo->root->log0->appendLog("(Image) - Drop out... " + importedTo->audio[i].path);
                            break;
                        }
                    }
                }
                importedTo->control->display[1] = true;
                importedTo->control->display[0] = false;
                // If not have no more images
                importedTo->image.size() == 0 &&!importedTo->control->imageEnd ? importedTo->control->imageEnd = true : 0;
            }
        }
        importedTo->root->log0->appendLog("(Image) - End");
        return 0;
    }

};