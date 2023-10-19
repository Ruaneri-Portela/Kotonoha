namespace kotonoha
{
	// This component is to use to when check box is change, set vsync status conform check box status
	// Here is a setup all show and overlayed events in game screen
	int ui(void* import)
	{
		kotonohaData::acessMapper * mapper = static_cast<kotonohaData::acessMapper*>(import);
		mapper->root->log0->appendLog("(Ui) - Start");
		mapper->root->log0->appendLog("(Clock) - Start");
		mapper->control->timer0.initTimeCapture();
		mapper->root->soundFe0 = Mix_LoadWAV(mapper->root->fileConfigs->soundFe0);
		kotonoha::prompt prompt0;
		prompt0.init();
		prompt0.mapper = mapper;
		mapper->root->prompt0 = &prompt0;
		bool volumeTriggers = false;
		while (mapper->control->outCode == 0)
		{	// Check is a prompt is pressed
			kotonohaTime::delay(kotonoha::maxtps);
			if (mapper->control->display[3])
			{

				// Draw UI
				ImGui_ImplSDLRenderer2_NewFrame();
				ImGui_ImplSDL2_NewFrame();
				ImGui::NewFrame();
				{
					ImGui::Begin("Kotonoha Project Visual Novel Engine");
					ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / mapper->root->io->Framerate, mapper->root->io->Framerate);
					ImGui::Text("Time %.3f s", mapper->control->timer0.pushTime());
					!mapper->control->nonVideo ? ImGui::Text("Video end %d ", mapper->control->videoEnd) : ImGui::Text("Video not using in this scene");
					!mapper->control->nonAudio ? ImGui::Text("Audio end %d ", mapper->control->audioEnd) : ImGui::Text("Audio not using in this scene");
					!mapper->control->nonImage ? ImGui::Text("Image end %d ", mapper->control->imageEnd) : ImGui::Text("Image not using in this scene");
					ImGui::Button("End") ? mapper->control->outCode = 1 : 0;
					ImGui::SameLine();
					ImGui::Button("Reset") ? mapper->control->outCode = 2 : 0;
					ImGui::SameLine();
					ImGui::Button("Menu") ? mapper->control->outCode = 3 : 0;
					ImGui::SameLine();
					mapper->root->log0->enable ? mapper->root->log0->drawLogger() : (void)0;
					ImGui::SameLine();
					if (mapper->control->paused)
					{
						if (ImGui::Button("Unpause"))
						{
							Mix_Resume(-1);
							Mix_ResumeMusic();
							mapper->control->timer0.switchClock();
							mapper->control->paused = false;
						}
					}
					else
					{
						if (ImGui::Button("Pause"))
						{
							Mix_Pause(-1);
							Mix_PauseMusic();
							mapper->control->timer0.switchClock();
							mapper->control->paused = true;
						}
					}
					ImGui::SameLine();
					if (volumeTriggers)
					{
						if (ImGui::Button("Unmute"))
						{
							Mix_MasterVolume(MIX_MAX_VOLUME);
							Mix_VolumeMusic(MIX_MAX_VOLUME);
							volumeTriggers = false;
						};
					}
					else
					{
						if (ImGui::Button("Mute"))
						{
							Mix_MasterVolume(0);
							Mix_VolumeMusic(0);
							volumeTriggers = true;
						};
					}
					ImGui::SameLine();
					if (ImGui::Button("Dump Sub"))
					{
						std::ofstream assDump;
						assDump.open("dump.ass", std::ios::trunc);
						assDump << mapper->text.stream.str() << std::endl;
						assDump.close();
					}
					ImGui::Checkbox("Enable Log", &mapper->root->log0->enable);
					ImGui::SameLine();
					ImGui::Checkbox("Hidden Subtitles", &mapper->control->hiddenSub);
					ImGui::Checkbox("Hidden Image", &mapper->control->hiddenImage);
					ImGui::SameLine();
					ImGui::Checkbox("Hidden Video", &mapper->control->hiddenVideo);
					ImGui::End();
				}
				ImGui::EndFrame();
				// Check if time is end to out for menu
				mapper->control->endTime < mapper->control->timer0.pushTime() ? mapper->control->outCode = 4 : 0;
				// Show if exist a prompt
				prompt0.show(mapper->root->renderer, mapper->root->window);
				// Send frame to renderer
				ImGui::Render();
				SDL_RenderSetScale(mapper->root->renderer, mapper->root->io->DisplayFramebufferScale.x, mapper->root->io->DisplayFramebufferScale.y);
				ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
				// Pass comand to next trigger
				mapper->control->display[4] = true;
				mapper->control->display[3] = false;
			}
		}
		Mix_FreeChunk(mapper->root->soundFe0);
		prompt0.close();
		mapper->root->log0->appendLog("(Ui) - End");
		return 0;
	};
};