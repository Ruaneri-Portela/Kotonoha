#include "Headers.h"
#include "Utils.h"
#include "Audio.h"
#include "Video.h"
#include "Image.h"
typedef struct gameData
{
    audio::audioObject *audio0;
    video::videoObject *video0;
    image::imageObject *image0;
    SDL_Renderer *renderer = NULL;
    SDL_Window *window = NULL;
    drawControl *dataDraw = NULL;
    SDL_Event event;
} gameData;
int comanderControler(void *array, void *import)
{
    gameData *local = static_cast<gameData *>(import);
    std::vector<std::vector<std::string>> *arrayString = static_cast<std::vector<std::vector<std::string>> *>(array);
    for (std::vector<std::vector<std::string>>::size_type cont = 0; cont < arrayString->size(); cont++)
    {
        std::string comandString = (*arrayString)[cont][0];
        const char *comand = comandString.c_str();
        if (strstr(comand, "SkipFRAME") != NULL)
        {
            // std::cout << "SkipFRAME" << std::endl;
        }
        else if (strstr(comand, "PlaySe") != NULL)
        {
            local->audio0->push((*arrayString)[cont][2], (*arrayString)[cont][0], (*arrayString)[cont][3], 0, local->dataDraw);
            local->dataDraw->nonAudio = false;
        }
        else if (strstr(comand, "PlayMovie") != NULL)
        {
            local->video0->push((*arrayString)[cont][1], (*arrayString)[cont][0], (*arrayString)[cont][3], local->renderer, local->dataDraw, local->window);
            local->dataDraw->nonVideo = false;
        }
        else if (strstr(comand, "BlackFade") != NULL)
        {
            // std::cout << "BlackFade" << std::endl;
        }
        else if (strstr(comand, "PlayBgm") != NULL)
        {
            local->audio0->push((*arrayString)[cont][1], (*arrayString)[cont][0], (*arrayString)[cont][2], 1, local->dataDraw);
            local->dataDraw->nonAudio = false;
        }
        else if (strstr(comand, "PlayVoice") != NULL)
        {
            local->audio0->push((*arrayString)[cont][1], (*arrayString)[cont][0], (*arrayString)[cont][4], 2, local->dataDraw);
            local->dataDraw->nonAudio = false;
        }
        else if (strstr(comand, "PrintText") != NULL)
        {
            // std::cout << "PrintText" << std::endl;
        }
        else if (strstr(comand, "CreateBG") != NULL)
        {
            local->image0->push((*arrayString)[cont][2], (*arrayString)[cont][0], (*arrayString)[cont][3], local->dataDraw, local->window, local->renderer);
            local->dataDraw->nonImage = false;
        }
        else if (strstr(comand, "Next") != NULL)
        {
            // std::cout << "Next" << std::endl;
        }
        else if (strstr(comand, " ;") != NULL)
        {
            std::cout << "End orc load." << std::endl;
        }
        else
        {
            std::cout << "Comand no valid or not implemented (" << comand << ")." << std::endl;
        }
    }
    return 0;
}
void ors(void *import, std::string comand)
{
    std::vector<std::vector<std::string>> vector = textExtract::readFileLineAndTab(comand);
    comanderControler(&vector, import);
}