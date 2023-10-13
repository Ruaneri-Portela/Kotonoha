namespace kotonoha
{
    int comanderControler(std::vector<std::vector<std::string>> array, kotonohaData::acessMapper *import)
    {
        kotonoha::audioObject *audio0 = static_cast<kotonoha::audioObject *>(import->root->audio0);
        kotonoha::videoObject *video0 = static_cast<kotonoha::videoObject *>(import->root->video0);
        kotonoha::imageObject *image0 = static_cast<kotonoha::imageObject *>(import->root->image0);
        kotonoha::textObject *text0 = static_cast<kotonoha::textObject *>(import->root->text0);
        import->root->log0->appendLog("(ORS) - Start");
        // Here is convert all line comand in a game object behaviour
        for (std::vector<std::vector<std::string>>::size_type cont = 0; cont < array.size(); cont++)
        {
            std::string comandString = array[cont][0];
            const char *comand = comandString.c_str();
            if (strstr(comand, "SkipFRAME") != NULL)
            {
            }
            else if (strstr(comand, "PlaySe") != NULL)
            {
                audio0->push(array[cont][3], array[cont][1], array[cont][4], 1);
                import->root->log0->appendLog("(ORS) - " + comandString + " " + array[cont][3]);
                import->control->nonAudio = false;
            }
            else if (strstr(comand, "PlayMovie") != NULL)
            {
                video0->push(array[cont][2], array[cont][1], array[cont][4]);
                import->root->log0->appendLog("(ORS) - " + comandString + " " + array[cont][2]);
                import->control->nonVideo = false;
            }
            else if (strstr(comand, "EndRoll") != NULL)
            {
                video0->push(array[cont][2], array[cont][1], array[cont][3]);
                import->root->log0->appendLog("(ORS) - " + comandString + " " + array[cont][2]);
                import->control->nonVideo = false;
            }
            else if (strstr(comand, "BlackFade") != NULL)
            {
            }
            else if (strstr(comand, "PlayBgm") != NULL)
            {
                audio0->push(array[cont][2], array[cont][1], array[cont][3], 2);
                import->root->log0->appendLog("(ORS) - " + comandString + " " + array[cont][2]);
                import->control->nonAudio = false;
            }
            else if (strstr(comand, "EndBGM") != NULL)
            {
                audio0->push(array[cont][2], array[cont][1], array[cont][3], 2);
                import->root->log0->appendLog("(ORS) - " + comandString + " " + array[cont][2]);
                import->control->nonAudio = false;
            }
            else if (strstr(comand, "PlayVoice") != NULL)
            {
                if (array[cont].size() > 4)
                {
                    audio0->push(array[cont][2], array[cont][1], array[cont][5], 3);
                    import->root->log0->appendLog("(ORS) - " + comandString + " " + array[cont][2]);
                    import->control->nonAudio = false;
                }
                else
                {
                    audio0->push(array[cont][2], array[cont][1], array[cont][3], 3);
                    import->root->log0->appendLog("(ORS) - " + comandString + " " + array[cont][2]);
                    import->control->nonAudio = false;
                }
            }
            else if (strstr(comand, "PrintText") != NULL)
            {
                text0->push(array[cont][3], array[cont][1], array[cont][4], "Dialoge", "Subtitle");
                import->root->log0->appendLog("(ORS) - Loading text to ASS data ");
            }
            else if (strstr(comand, "CreateBG") != NULL)
            {
                image0->push(array[cont][3], array[cont][1], array[cont][4]);
                import->root->log0->appendLog("(ORS) - " + comandString + " " + array[cont][3]);
                import->control->nonImage = false;
            }
            else if (strstr(comand, "Next") != NULL)
            {
                import->control->endTime = kotonohaTime::convertToTime(array[cont][1]);
                import->root->log0->appendLog("(ORS) - End script in time " + std::to_string(kotonohaTime::convertToTime(array[cont][1])));
            }
            else if (strstr(comand, " ;") != NULL)
            {
                import->root->log0->appendLog("(ORS) - End Script file");
            }
            else
            {
                import->root->log0->appendLog("(ORS) - Comand Not Found" + comandString);
            }
        }
        import->root->log0->appendLog("(ORS) - End");
        return 0;
    }
    void orsLoader(kotonohaData::acessMapper *import, std::string comand)
    {
        // Convert and Split script parameters
        std::vector<std::vector<std::string>> vector = kotonoha::readScript(comand);
        // Send to comander Generator
        comanderControler(vector, import);
    }
}