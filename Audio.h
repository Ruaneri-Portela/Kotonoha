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
				channel == -10 ? audioTemporary.music = Mix_LoadMUS(filenameStr.c_str()) : 0;
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
			kotonohaTime::delay(kotonoha::maxtps);
			// Percurrent the audio array
			if (!importedTo->control->audioEnd && !importedTo->control->nonAudio && !(importedTo->audio.size() == 0))
			{
				for (std::vector<kotonohaData::imageData>::size_type i = 0; i < importedTo->audio.size(); i++)
				{
					timePass = importedTo->control->timer0.pushTime();
					// Check if the audio is loaded on RAM
					if (importedTo->audio[i].music == NULL && importedTo->audio[i].sound == NULL && importedTo->audio[i].play - 20 < timePass)
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
						if (importedTo->audio[i].music != NULL) {
							importedTo->root->log0->appendLog("(Audio) - Playing Music " + importedTo->audio[i].path);
							Mix_PlayMusic(importedTo->audio[i].music, 0);
						}
						else {
							bool isPlaying = false;
							for (int j = importedTo->audio[i].channel; j < importedTo->audio[i].channel + 5; j++) {
								if (Mix_Playing(j) == 0) {
									importedTo->root->log0->appendLog("(Audio) - Playing " + importedTo->audio[i].path + " on channel " + std::to_string(j));
									Mix_PlayChannel(importedTo->audio[i].channel, importedTo->audio[i].sound, 0);
									isPlaying = true;
									break;
								}
							}
							!isPlaying ? importedTo->root->log0->appendLog("(Audio) - No channel avaliable " + importedTo->audio[i].path) : (void)0;
						}
						importedTo->audio[i].played = true;
					}
					// Destroy audio from RAM if time is end
					if (importedTo->audio[i].end < timePass)
					{
						importedTo->audio[i].music == NULL ? Mix_FreeChunk(importedTo->audio[i].sound) : Mix_FreeMusic(importedTo->audio[i].music);
						importedTo->root->log0->appendLog("(Audio) - Drop out... " + importedTo->audio[i].path);
						importedTo->audio.erase(importedTo->audio.begin() + i);
					}
				}
			}
			// Check if audio array is ended
			importedTo->audio.size() == 0 && !importedTo->control->audioEnd ? importedTo->control->audioEnd = true : 0;
		};
		// Free memory if have audio loaders
		if (importedTo->audio.size() > 0) {
			for (size_t i = 0; i < importedTo->audio.size(); i++) {
				importedTo->audio[i].sound != NULL ? Mix_FreeChunk(importedTo->audio[i].sound) : (void)0;
				importedTo->audio[i].music != NULL ? Mix_FreeMusic(importedTo->audio[i].music) : (void)0;
			};
		}
		importedTo->root->log0->appendLog("(Audio) - End Engine");
		Mix_HaltChannel(-1);
		Mix_HaltMusic();
		return 0;
	};
}