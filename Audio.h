#include "Utils.h"
namespace audio
{
    class audioObject
    {
    public:
        soundData *data = NULL;
        void push(std::string filenameString, std::string startTime, std::string endTime, int channel, drawControl *dataDraw)
        {

            if (atoi(filenameString.c_str()) != 1)
            {
                std::stringstream ss;
                ss << "./Midia/";
                ss << filenameString;
                ss << ".ogg";
                std::string filenameStr = ss.str();
                // Criar um novo objeto SoundObject
                soundData *soundTemporary;
                soundTemporary = new soundData;
                soundTemporary->filename = filenameStr;
                soundTemporary->timeToPlay = timeUtils::convertToTime(startTime);
                soundTemporary->timeToEnd = timeUtils::convertToTime(endTime);
                soundTemporary->channel = channel;
                soundTemporary->next = NULL;
                soundTemporary->prev = NULL;
                soundTemporary->sound = Mix_LoadWAV(filenameStr.c_str());
                soundTemporary->dataDraw = dataDraw;
                if (data == NULL)
                {
                    data = soundTemporary;
                }
                else
                {
                    soundData *search = data;
                    while (search->next != NULL)
                    {
                        search = search->next;
                    }
                    search->next = soundTemporary;
                    search->next->prev = search;
                }
                // Adicionar o novo objeto à lista
            }
        };
    };
    int play(void *import)
    {
        if (import != NULL)
        {
            soundData *data = static_cast<soundData *>(import);
            soundData *search = NULL;
            std::cout << "Audio init" << std::endl;
            while (!data->dataDraw->exit)
            {
                if (data != NULL)
                {
                    search = data;
                    while (search != NULL)
                    {
                        if (search->timeToPlay <= data->dataDraw->timer0.pushTime() - 15 && !search->played && search->sound != NULL)
                        {
                            Mix_PlayChannel(search->channel, search->sound, 0);
                            search->played = true;
                        }
                        search = search->next;
                    }
                }
            };
        }
        std::cout << "Audio end" << std::endl;
        return 0;
    };
}
