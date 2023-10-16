namespace kotonoha
{
	class audioObject
	{
	public:
		kotonohaData::acessMapper* exportTo = NULL;
		void push(std::string filenameString, std::string startTime, std::string endTime, int channel)
		{
			if (atoi(filenameString.c_str()) != 1)
			{
				// Prepare String for valid file path
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
				// Push to audio data array
				exportTo->audio.push_back(audioTemporary);
			}
		};
	};
	int playAudio(void* import)
	{
		kotonohaData::acessMapper * importedTo = static_cast<kotonohaData::acessMapper*>(import);
		importedTo->root->log0->appendLog("(Audio) - Start");
		importedTo->root->log0->appendLog("(Audio) - " + std::to_string(importedTo->audio.size()) + " Audios to play");
		double timePass = 0.0;
		// Wait fist frame is ready
		while (importedTo->control->display[4] == false && importedTo->control->outCode == 0) {
			continue;
		}
		while (importedTo->control->outCode == 0)
		{
			// Percurrent the audio array
			if (!importedTo->control->audioEnd && !importedTo->control->nonAudio && !(importedTo->audio.size() == 0))
			{
				for (std::vector<kotonohaData::imageData>::size_type i = 0; i < importedTo->audio.size(); i++)
				{
					timePass = importedTo->control->timer0.pushTime();
					// Check if the audio is loaded on RAM
					if (importedTo->audio[i].sound == NULL && importedTo->audio[i].play - 20 < timePass)
					{
						importedTo->root->log0->appendLog("(Audio) - Loading... " + importedTo->audio[i].path);
						importedTo->audio[i].sound = Mix_LoadWAV(importedTo->audio[i].path.c_str());
						if (importedTo->audio[i].sound == NULL)
						{
							importedTo->root->log0->appendLog("(Audio) - Error on load file " + importedTo->audio[i].path);
							importedTo->audio.erase(importedTo->audio.begin() + i);
						}
					}
					// If is time to play, play audio on specific mix channel
					else if (importedTo->audio[i].play < timePass && !importedTo->audio[i].played)
					{
						importedTo->root->log0->appendLog("(Audio) - Playing " + importedTo->audio[i].path);
						Mix_PlayChannel(importedTo->audio[i].channel, importedTo->audio[i].sound, 0);
						importedTo->audio[i].played = true;
					}
					// Destroy audio from RAM if time is end
					if (importedTo->audio[i].end < timePass)
					{
						Mix_FreeChunk(importedTo->audio[i].sound);
						importedTo->root->log0->appendLog("(Audio) - Drop out... " + importedTo->audio[i].path);
						importedTo->audio.erase(importedTo->audio.begin() + i);
					}
				}
			}
			// Check if audio array is ended
			importedTo->audio.size() == 0 && !importedTo->control->audioEnd ? importedTo->control->audioEnd = true : 0;
		};
		importedTo->root->log0->appendLog("(Audio) - End Engine");
		Mix_HaltChannel(0);
		Mix_HaltChannel(1);
		Mix_HaltChannel(2);
		return 0;
	};
}