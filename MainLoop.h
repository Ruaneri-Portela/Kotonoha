namespace kotonoha
{
	class loop
	{
	public:
		int game(kotonohaData::envComponets data)
		{
			// Create data structure
			kotonohaData::rootData* rootData = new kotonohaData::rootData;
			rootData->window = data.window;
			rootData->renderer = data.renderer;
			rootData->log0 = data.log;
			rootData->fileConfigs = &data.config.parms;
			rootData->io = data.io;
			kotonohaData::controlData* controlData = new kotonohaData::controlData;
			kotonohaData::acessMapper* global = new kotonohaData::acessMapper;
			global->control = controlData;
			global->root = rootData;
			global->delayTps = &data.initData.delayTps;
			rootData->image0 = new kotonohaData::imageData;
			rootData->audio0 = new kotonohaData::audioData;
			rootData->video0 = new kotonohaData::videoData;
			rootData->text0 = new kotonohaData::textData;
			rootData->question0 = new kotonohaData::questionData;
			// Export global to inside objects structs, this allow acess in all threads root var
			static_cast<kotonoha::audioObject*>(rootData->audio0)->exportTo = global;
			static_cast<kotonoha::videoObject*>(rootData->video0)->exportTo = global;
			static_cast<kotonoha::imageObject*>(rootData->image0)->exportTo = global;
			static_cast<kotonoha::textObject*>(rootData->text0)->exportTo = global;
			static_cast<kotonoha::questionObject*>(rootData->question0)->exportTo = global;
			// Loading script to create all game objects
			data.log->appendLog("(ORS - Pre) - Reading " + data.config.filenameString);
			controlData->outCode = kotonoha::orsLoader(global, data.config.filenameString);
			// Import value isCmd to data struct
			controlData->isCmd = data.initData.fromComandLine;
			controlData->hardwareVideo = data.initData.hwVideo;
			// Start game loop and threads
			std::thread thread1(kotonoha::ui, global);
			std::thread thread2(kotonoha::playImage, global);
			std::thread thread3(kotonoha::playVideo, global);
			std::thread thread4(kotonoha::playAudio, global);
			std::thread thread5(kotonoha::playText, global);
			data.log->appendLog("(ML) - Entry point to while");
			rootData->event = new SDL_Event;
			kotonoha::prompt* local = NULL;
			double timeStart = 0;
			double timeHiddenMouse = 0;
			while (controlData->outCode == 0)
			{
				kotonohaTime::delay(data.initData.delayTps / 2);
				// Event reciver
				while (SDL_PollEvent(rootData->event))
				{
					if (local != NULL) {
						size_t var = local->detectTouch(rootData->event);
						var != 0 ? data.log->appendLog("(Ui) - Touch detected " + std::to_string(var)) : (void)0;
					}
					else {
						rootData->prompt0 != NULL ? local = static_cast<kotonoha::prompt*>(rootData->prompt0) : NULL;
					}
					if (rootData->event->type == SDL_MOUSEMOTION) {
						timeStart = controlData->timer0.pushTime();
						SDL_ShowCursor(SDL_ENABLE);
					}
					ImGui_ImplSDL2_ProcessEvent(rootData->event);
					controlData->outCode = keyBinds0(rootData->event, data.window, 3);
				}
				// Hidden Mouse
				timeHiddenMouse = controlData->timer0.pushTime() - timeStart;
				if (timeHiddenMouse > 3) {
					SDL_ShowCursor(SDL_DISABLE);
				}
				// Render send
				if (controlData->display[4])
				{
					SDL_RenderPresent(data.renderer);
					controlData->display[0] = true;
					controlData->display[4] = false;
				}
			}
			// Wait threads
			thread1.join();
			thread2.join();
			thread3.join();
			thread4.join();
			thread5.join();
			// Write on log the event that caused the close
			switch (controlData->outCode)
			{
			case 1:
				data.log->appendLog("(ML) - Close");
				break;
			case 2:
				data.log->appendLog("(ML) - Reset");
				break;
			case 3:
				data.log->appendLog("(ML) - Return to menu");
				break;
			case 4:
				data.log->appendLog("(ML) - Scene ended");
				break;
			case 5:
				data.log->appendLog("(ML) - Erro on loading ORS file");
				break;
			default:
				data.log->appendLog("(ML) - Uknonw return code");
				break;
			}
			int returnCode = controlData->outCode;
			data.log->appendLog("(ML) - End");
			// Free objects
			delete rootData->event;
			delete static_cast<kotonoha::audioObject*>(rootData->audio0);
			delete static_cast<kotonoha::videoObject*>(rootData->video0);
			delete static_cast<kotonoha::imageObject*>(rootData->image0);
			delete static_cast<kotonoha::textObject*>(rootData->text0);
			delete global;
			delete controlData;
			delete rootData;
			data.initData.fromComandLine ? returnCode = 1 : 0;
			return returnCode;
		};
	};
}