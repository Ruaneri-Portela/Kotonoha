namespace kotonoha
{
    /**
     * @brief The audioObject class represents an audio object that can be played in Kotonoha.
     * 
     */
    class audioObject
    {
    public:
        kotonohaData::acessMapper *exportTo = NULL; /**< Pointer to the access mapper object. */
        
        /**
         * @brief Pushes an audio file to the audio object.
         * 
         * @param filenameString The name of the audio file.
         * @param startTime The start time of the audio file.
         * @param endTime The end time of the audio file.
         * @param channel The channel to play the audio file on.
         */
        void push(std::string filenameString, std::string startTime, std::string endTime, int channel);
    };
    
    /**
     * @brief Plays the audio objects in the access mapper.
     * 
     * @param import Pointer to the access mapper object.
     * @return int Returns 0 when the audio playback is complete.
     */
    int playAudio(void *import);
}
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
                ss << "./Midia/";
                ss << filenameString;
                ss << ".ogg";
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
        std::vector<kotonohaData::audioData> audioData = importedTo->audio;
        kotonohaData::rootData *root = importedTo->root;
        kotonohaData::controlData *control = importedTo->control;
        root->log0->appendLog("(Audio) - Start");
        while (!control->exit)
        {
            if (!control->audioEnd)
            {
                for (std::vector<kotonohaData::imageData>::size_type i = 0; i < audioData.size(); i++)
                {
                    double timePass = control->timer0.pushTime();

                    if (audioData[i].sound == NULL && audioData[i].play - 10 < timePass)
                    {
                        root->log0->appendLog("(Audio) - Loading... " + audioData[i].path);
                        audioData[i].sound = Mix_LoadWAV(audioData[i].path.c_str());
                    }
                    else if (audioData[i].play < timePass && audioData[i].played == false)
                    {
                        root->log0->appendLog("(Audio) - Playing " + audioData[i].path);
                        Mix_PlayChannel(audioData[i].channel, audioData[i].sound, 0);
                        audioData[i].played = true;
                    }
                    else if (audioData[i].end < timePass)
                    {
                        audioData.erase(audioData.begin() + i);
                        i = 0;
                    }
                    if (audioData.size() == 0)
                    {
                        control->audioEnd = true;
                        goto Out;
                    }
                }
            }
        Out:
        };
        root->log0->appendLog("(Audio) - End");
        Mix_HaltChannel(0);
        Mix_HaltChannel(1);
        Mix_HaltChannel(2);
        return 0;
    };
}
