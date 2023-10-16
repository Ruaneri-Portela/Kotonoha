namespace kotonoha
{
	typedef struct menuReturn
	{
		int returnCode = 0;
		std::string filenameString = "";
		bool debugPromptEnabled = false;
		kotonohaData::configsData configs;
	} menuReturn;
	kotonohaData::configsData fileConfig(int parm = 0, kotonohaData::configsData configs = { false, "", "", "", "" })
	{
		std::ifstream dataR("kotonoha.ckot");
		if (!dataR.is_open() or parm == 1)
		{
			std::ofstream dataW("kotonoha.ckot", std::ios::binary);
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
	menuReturn menu(SDL_Window* window, SDL_Renderer* renderer, int gameReturn, kotonoha::logger* log0, ImGuiIO* io)
	{
		log0->appendLog("(Menu) - Entry point to menu");
		menuReturn object = { 0, "", true };
		object.configs = fileConfig(0);
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
		bool about = false;
		bool config = false;
		bool configSaved = false;
		bool returnPrompt = true;
		char scriptPath[256] = "";
		char mediaPath[256] = "";
		char audioExtension[32] = "";
		char videoExtension[32] = "";
		char imageExtension[32] = "";
		char soundFe0[256] = "";
		char stylesPath[256] = "";
		if (object.configs.configured)
		{
			STRCPYFIX(audioExtension, object.configs.audioExtension);
			STRCPYFIX(videoExtension, object.configs.videoExtension);
			STRCPYFIX(imageExtension, object.configs.imageExtension);
			STRCPYFIX(mediaPath, object.configs.mediaPath);
			STRCPYFIX(soundFe0, object.configs.soundFe0);
			STRCPYFIX(stylesPath, object.configs.stylesPath);
		}
		bool vsync = true;
		int vsyncStatus = vsync;
		SDL_RenderSetVSync(renderer, vsyncStatus);
		SDL_Event event;
		while (object.returnCode == 0)
		{
			while (SDL_PollEvent(&event))
			{
				ImGui_ImplSDL2_ProcessEvent(&event);
				object.returnCode = keyBinds0(&event, window, 1, renderer, &vsync, log0);
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
			ImGui::Checkbox("Enable logger", &log0->enable);
			if (object.configs.configured)
			{
				if (ImGui::Button("Start"))
				{
					object.filenameString = scriptPath;
					object.returnCode = 2;
				}
				ImGui::SameLine();
				ImGui::Button("Configs") ? config = true : 0;
			}
			ImGui::SameLine();
			if (ImGui::Button("Close"))
			{
				object.returnCode = 1;
			}
			ImGui::End();
			// Windows about
			if (about)
			{
				ImGui::Begin("About");
				ImGui::Text("Kotonoha Project, A visual novel engine");
				ImGui::Text(kotonoha::version.c_str());
				ImGui::Text("Developed by: Ruaneri F.Portela");
				ImGui::Text("Using SDL, FFMPEG, ASSLIB e IMGui");
				ImGui::Button("Close") ? about = false : 0;
				ImGui::End();
			}
			// Windows set config
			if (config or !object.configs.configured)
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
					object.configs.configured = true;
					STRCPYFIX(object.configs.audioExtension, audioExtension);
					STRCPYFIX(object.configs.videoExtension, videoExtension);
					STRCPYFIX(object.configs.imageExtension, imageExtension);
					STRCPYFIX(object.configs.mediaPath, mediaPath);
					STRCPYFIX(object.configs.soundFe0, soundFe0);
					STRCPYFIX(object.configs.stylesPath, stylesPath);
					object.configs.configured = true;
					fileConfig(1, object.configs);
					configSaved = true;
				}
				ImGui::SameLine();
				if (object.configs.configured)
				{
					ImGui::Button("Close") ? config = false : 0;
				}
				ImGui::End();
			}
			// Windows Alert Need Config
			if (!object.configs.configured)
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
			if (log0->enable)
			{
				log0->drawLogger();
			}
			// Windows End Scene
			if (gameReturn >= 3 && returnPrompt)
			{
				ImGui::Begin("Alert");
				ImGui::Text("The scene end, return code %d", gameReturn);
				ImGui::Button("Close") ? returnPrompt = false : 0;
				ImGui::End();
			}
			/// End Windows
			ImGui::Render();
			SDL_RenderSetScale(renderer, io->DisplayFramebufferScale.x, io->DisplayFramebufferScale.y);
			SDL_SetRenderDrawColor(renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255));
			SDL_RenderClear(renderer);
			ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
			SDL_RenderPresent(renderer);
		}
		log0->appendLog("(Menu) - Menu out");
		return object;
	}
}