#include "Audio.h"
#include "Video.h"
typedef struct gameData
{
    bool drawUi = false;
    audio::audioObject audio0;
    video::videoObject video0;
    SDL_Renderer *renderer = NULL;
    SDL_Window *window = NULL;
    drawControl *dataDraw = NULL;
    SDL_Rect square = {0, 0, 0, 0};
    SDL_Event event;
    bool run = true;
} gameData;
int comanderControler(void *array, void *import)
{
    gameData *local = static_cast<gameData *>(import);
    std::vector<std::vector<std::string>> *arrayString = static_cast<std::vector<std::vector<std::string>> *>(array);
    for (int cont = 0; cont < arrayString->size(); cont++)
    {
        std::string comandString = (*arrayString)[cont][0];
        const char *comand = comandString.c_str();
        if (strstr(comand, "SkipFRAME") != NULL)
        {
            // std::cout << "SkipFRAME" << std::endl;
        }
        else if (strstr(comand, "PlaySe") != NULL)
        {
            local->audio0.push((*arrayString)[cont][2], (*arrayString)[cont][0], (*arrayString)[cont][3], 0);
        }
        else if (strstr(comand, "PlayMovie") != NULL)
        {
            local->video0.push((*arrayString)[cont][1], (*arrayString)[cont][0], (*arrayString)[cont][3], local->renderer,local->dataDraw,&local->square);
            // std::cout << "PlayMovie" << std::endl;
        }
        else if (strstr(comand, "BlackFade") != NULL)
        {
            // std::cout << "BlackFade" << std::endl;
        }
        else if (strstr(comand, "PlayBgm") != NULL)
        {
            local->audio0.push((*arrayString)[cont][1], (*arrayString)[cont][0], (*arrayString)[cont][2], 1);
        }
        else if (strstr(comand, "PlayVoice") != NULL)
        {
            local->audio0.push((*arrayString)[cont][1], (*arrayString)[cont][0], (*arrayString)[cont][4], 2);
        }
        else if (strstr(comand, "PrintText") != NULL)
        {
            // std::cout << "PrintText" << std::endl;
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

void ors(void *import)
{
    std::string filePath = "Scripts/00/00-00-A00.ENG.ORS";
    std::vector<std::vector<std::string>> vector = textExtract::readFileLineAndTab(filePath);
    comanderControler(&vector, import);
}