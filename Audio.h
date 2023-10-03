
/**
 * @brief The audioObject class represents an audio object that can be played in Kotonoha.
 *
 */

/**
 * @brief Pushes an audio file to the audio object.
 *
 * @param filenameString The name of the audio file.
 * @param startTime The start time of the audio file.
 * @param endTime The end time of the audio file.
 * @param channel The channel to play the audio file on.
 */

/**
 * @brief Plays the audio objects in the access mapper.
 *
 * @param import Pointer to the access mapper object.
 * @return int Returns 0 when the audio playback is complete.
 */

namespace kotonoha
{
    class audioObject
    {
    public:
        kotonohaData::acessMapper *exportTo = NULL;
        void push(std::string filenameString, std::string startTime, std::string endTime, int channel)
        {
            if (atoi(filenameString.c_str()) != 1)
            {
                std::stringstream ss;
                ss << exportTo->root->fileConfigs->mediaPath;
                ss << filenameString;
                ss << exportTo->root->fileConfigs->audioExtension;
                std::string filenameStr = ss.str();
                // Ceate new audio object
                kotonohaData::audioData audioTemporary;
                audioTemporary.path = filenameStr;
                audioTemporary.play = kotonohaTime::convertToTime(startTime);
                audioTemporary.end = kotonohaTime::convertToTime(endTime);
                audioTemporary.channel = channel;
                exportTo->audio.push_back(audioTemporary);
            }
        };
    };
    int playAudio(void *import)
    {
        kotonohaData::acessMapper *importedTo = static_cast<kotonohaData::acessMapper *>(import);
        importedTo->root->log0->appendLog("(Audio) - Start");
        importedTo->root->log0->appendLog("(Audio) - " + std::to_string(importedTo->audio.size()) + " Audios to play");
        double timePass = 0.0;
        while (importedTo->control->outCode == -1)
        {
            if (!importedTo->control->audioEnd && !importedTo->control->nonAudio && !(importedTo->audio.size() == 0))
            {
                for (std::vector<kotonohaData::imageData>::size_type i = 0; i < importedTo->audio.size(); i++)
                {
                    timePass = importedTo->control->timer0.pushTime();
                    if (importedTo->audio[i].sound == NULL && importedTo->audio[i].play - 20 < timePass)
                    {
                        importedTo->root->log0->appendLog("(Audio) - Loading... " + importedTo->audio[i].path);
                        importedTo->audio[i].sound = Mix_LoadWAV(importedTo->audio[i].path.c_str());
                        if (importedTo->audio[i].sound == NULL)
                        {
                            importedTo->root->log0->appendLog("(Audio) - Error on load file" + importedTo->audio[i].path);
                            importedTo->audio.erase(importedTo->audio.begin() + i);
                        }
                    }
                    else if (importedTo->audio[i].play < timePass && !importedTo->audio[i].played)
                    {
                        kotonohaTime::delay(5);
                        importedTo->root->log0->appendLog("(Audio) - Playing " + importedTo->audio[i].path);
                        Mix_PlayChannel(importedTo->audio[i].channel, importedTo->audio[i].sound, 0);
                        importedTo->audio[i].played = true;
                    }
                    if (importedTo->audio[i].end < timePass)
                    {
                        Mix_FreeChunk(importedTo->audio[i].sound);
                        importedTo->root->log0->appendLog("(Audio) - Drop out... " + importedTo->audio[i].path);
                        importedTo->audio.erase(importedTo->audio.begin() + i);
                        break;
                    }
                }
            }
            if (importedTo->audio.size() == 0 && !importedTo->control->audioEnd)
            {
                importedTo->control->audioEnd = true;
            }
        };
        importedTo->root->log0->appendLog("(Audio) - End Engine");
        Mix_HaltChannel(0);
        Mix_HaltChannel(1);
        Mix_HaltChannel(2);
        return 0;
    };
}