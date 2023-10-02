/**
 * @brief This function controls the execution of commands in a script file.
 * 
 * @param array A vector of vectors of strings containing the commands to be executed.
 * @param import A pointer to an instance of the acessMapper class.
 * @return int Returns 0 upon successful execution of all commands.
 */
int comanderControler(std::vector<std::vector<std::string>> array, kotonohaData::acessMapper *import)
{
    kotonohaData::rootData *local = import->root;
    kotonohaData::controlData *control = import->control;
    kotonoha::audioObject *audio0 = static_cast<kotonoha::audioObject *>(local->audio0);
    kotonoha::videoObject *video0 = static_cast<kotonoha::videoObject *>(local->video0);
    kotonoha::imageObject *image0 = static_cast<kotonoha::imageObject *>(local->image0);
    local->log0->appendLog("(ORS) - Start");
    for (std::vector<std::vector<std::string>>::size_type cont = 0; cont < array.size(); cont++)
    {
        std::string comandString = array[cont][0];
        const char *comand = comandString.c_str();
        if (strstr(comand, "SkipFRAME") != NULL)
        {
        }
        else if (strstr(comand, "PlaySe") != NULL)
        {
            audio0->push(array[cont][2], array[cont][0], array[cont][3], 0);
            local->log0->appendLog("(ORS) - PlaySe" + array[cont][2]);
            control->nonAudio = false;
        }
        else if (strstr(comand, "PlayMovie") != NULL)
        {
            video0->push(array[cont][1], array[cont][0], array[cont][3]);
            local->log0->appendLog("(ORS) - PlayMovie" + array[cont][1]);
            control->nonVideo = false;
        }
        else if (strstr(comand, "BlackFade") != NULL)
        {
        }
        else if (strstr(comand, "PlayBgm") != NULL)
        {
            audio0->push(array[cont][1], array[cont][0], array[cont][2], 1);
            local->log0->appendLog("(ORS) - PlayBgm" + array[cont][2]);
            control->nonAudio = false;
        }
        else if (strstr(comand, "PlayVoice") != NULL)
        {
            audio0->push(array[cont][1], array[cont][0], array[cont][4], 2);
            local->log0->appendLog("(ORS) - PlayVoice" + array[cont][1]);
            control->nonAudio = false;
        }
        else if (strstr(comand, "PrintText") != NULL)
        {
        }
        else if (strstr(comand, "CreateBG") != NULL)
        {
            image0->push(array[cont][2], array[cont][0], array[cont][3]);
            local->log0->appendLog("(ORS) - PlayVoice" + array[cont][2]);
            control->nonImage = false;
        }
        else if (strstr(comand, "Next") != NULL)
        {
            local->log0->appendLog("(ORS) - Next in" + array[cont][0]);
        }
        else if (strstr(comand, " ;") != NULL)
        {
            local->log0->appendLog("(ORS) - End Script file");
        }
        else
        {
            local->log0->appendLog("(ORS) - Comand Not Found" + array[cont][0]);
        }
    }
    local->log0->appendLog("(ORS) - End");
    return 0;
}
void ors(kotonohaData::acessMapper *import, std::string comand)
{
    std::vector<std::vector<std::string>> vector = kotonoha::readScript(comand);
    comanderControler(vector, import);
}