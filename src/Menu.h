namespace kotonoha
{
	static kotonohaData::configsData fileConfig(int parm = 0, kotonohaData::configsData configs = { false, "", "", "", "" }, std::string path = "")
	{
		std::ifstream dataR(path);
		if (!dataR.is_open() or parm == 1)
		{
			std::ofstream dataW(path, std::ios::binary);
			dataW.write(reinterpret_cast<char*>(&configs), sizeof(kotonohaData::configsData));
			dataW.close();
		}
		else
		{
			dataR.read(reinterpret_cast<char*>(&configs), sizeof(kotonohaData::configsData));
			dataR.close();
		}
		return configs;
	}
	kotonohaData::envComponets menu(kotonohaData::envComponets data)
	{
		data.log->appendLog("(Menu) - Entry point to menu");
		data.config = { 0, true,"" };
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
		bool about = false;
		bool config = false;
		bool hw = data.initData.hwVideo;
		bool configSaved = false;
		bool returnPrompt = true;
		char scriptPath[256] = "";
		char mediaPath[256] = "";
		char audioExtension[32] = "";
		char videoExtension[32] = "";
		char imageExtension[32] = "";
		char soundFe0[256] = "";
		char stylesPath[256] = "";
		data.config.parms = fileConfig(0, data.config.parms, data.initData.configLoad);
		SDL_Event* event = new SDL_Event;
		if (data.initData.fromComandLine) {
			data.config.filenameString = data.initData.scriptPath;
			data.config.returnCode = 2;
			goto END;
		}
		if (data.config.parms.configured)
		{
			STRCPYFIX(audioExtension, data.config.parms.audioExtension);
			STRCPYFIX(videoExtension, data.config.parms.videoExtension);
			STRCPYFIX(imageExtension, data.config.parms.imageExtension);
			STRCPYFIX(mediaPath, data.config.parms.mediaPath);
			STRCPYFIX(soundFe0, data.config.parms.soundFe0);
			STRCPYFIX(stylesPath, data.config.parms.stylesPath);
		}
		while (data.config.returnCode == 0)
		{
			kotonohaTime::delay(data.initData.delayTps);
			while (SDL_PollEvent(event))
			{
				ImGui_ImplSDL2_ProcessEvent(event);
				data.config.returnCode = keyBinds0(event, data.window, 1);
			}
			ImGui_ImplSDLRenderer2_NewFrame();
			ImGui_ImplSDL2_NewFrame();
			ImGui::NewFrame();
			/// Start Windows
			// Windows main
			ImGui::Begin("Kotonoha Project, A visual novel engine");
			ImGui::Text("Copy the script path from text box e press start!");
			ImGui::InputText("Script file path", scriptPath, 256);
			ImGui::Checkbox("About", &about);
			ImGui::SameLine();
			ImGui::Checkbox("Enable logger", &data.log->enable);
			ImGui::SameLine();
			ImGui::Checkbox("HW Video", &hw);
			if (data.config.parms.configured)
			{
				if (ImGui::Button("Start"))
				{
					data.config.filenameString = scriptPath;
					data.config.returnCode = 2;
				}
				ImGui::SameLine();
				ImGui::Button("Configs") ? config = true : 0;
			}
			ImGui::SameLine();
			if (ImGui::Button("Close"))
			{
				data.config.returnCode = 1;
			}
			ImGui::End();
			// Windows about
			if (about)
			{
				ImGui::Begin("About");
				ImGui::Text("Kotonoha Project, A visual novel engine");
				ImGui::Text(KOTONOHA_VERSION);
				ImGui::Text("Developed by: Ruaneri F.Portela");
				ImGui::Text("Using SDL, FFMPEG, ASSLIB e IMGui");
				ImGui::Button("Close") ? about = false : 0;
				ImGui::End();
			}
			// Windows set config
			if (config or !data.config.parms.configured)
			{
				ImGui::Begin("Config");
				ImGui::InputText("Media path", mediaPath, 256);
				ImGui::InputText("Audio extension", audioExtension, 32);
				ImGui::InputText("Video extension", videoExtension, 32);
				ImGui::InputText("Image extension", imageExtension, 32);
				ImGui::InputText("Sound Effect 1", soundFe0, 256);
				ImGui::InputText("File with text Styles", stylesPath, 256);
				if (ImGui::Button("Save"))
				{
					data.config.parms.configured = true;
					STRCPYFIX(data.config.parms.audioExtension, audioExtension);
					STRCPYFIX(data.config.parms.videoExtension, videoExtension);
					STRCPYFIX(data.config.parms.imageExtension, imageExtension);
					STRCPYFIX(data.config.parms.mediaPath, mediaPath);
					STRCPYFIX(data.config.parms.soundFe0, soundFe0);
					STRCPYFIX(data.config.parms.stylesPath, stylesPath);
					fileConfig(1, data.config.parms, data.initData.configLoad);
					configSaved = true;
				}
				ImGui::SameLine();
				if (data.config.parms.configured)
				{
					ImGui::Button("Close") ? config = false : 0;
				}
				ImGui::End();
			}
			// Windows Alert Need Config
			if (!data.config.parms.configured)
			{
				ImGui::Begin("Alert");
				ImGui::Text("You need set configs before start");
				ImGui::End();
			}
			// Windows Config saved
			if (configSaved)
			{
				ImGui::Begin("Alert");
				ImGui::Text("Config saved");
				ImGui::Button("Close") ? configSaved = false : 0;
				ImGui::End();
			}
			// Windows Log
			if (data.log->enable)
			{
				data.log->drawLogger();
			}
			// Windows End Scene
			if (data.config.returnCode >= 3 && returnPrompt)
			{
				ImGui::Begin("Alert");
				ImGui::Text("The scene end, return code %d", data.config.returnCode);
				ImGui::Button("Close") ? returnPrompt = false : 0;
				ImGui::End();
			}
			/// End Windows
			ImGui::Render();
			SDL_RenderSetScale(data.renderer, data.io->DisplayFramebufferScale.x, data.io->DisplayFramebufferScale.y);
			SDL_SetRenderDrawColor(data.renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255));
			SDL_RenderClear(data.renderer);
			ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
			SDL_RenderPresent(data.renderer);
		}
		END:
		delete event;
		data.initData.hwVideo = hw;
		data.log->appendLog("(Menu) - Menu out");
		return data;
	}
}