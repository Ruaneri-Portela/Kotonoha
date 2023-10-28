namespace kotonoha {
	std::stringstream help() {
		std::stringstream helpMsg;
		helpMsg << "Kotonoha Project ver." << KOTONOHA_VERSION << std::endl;
		helpMsg << "========================================" << std::endl;
		helpMsg << "Usage: kotonoha [options] [script]" << std::endl;
		helpMsg << "Options:" << std::endl;
		helpMsg << "  -v\t\t\tShow version" << std::endl;
		helpMsg << "  -h\t\t\tShow this help" << std::endl;
		helpMsg << "  -d [0|1]\t\tEnable or disable hardware video decoding" << std::endl;
		helpMsg << "  -t [tps]\t\tSet delay tps" << std::endl;
		helpMsg << "  -c [config]\t\tLoad config file" << std::endl;
		helpMsg << "  -f [script]\t\tLoad script file" << std::endl;
		helpMsg << "  -m [master]\t\tLoad master file" << std::endl;
		helpMsg << "  -l [log]\t\tLog destination file" << std::endl;
		helpMsg << "========================================" << std::endl;
		return helpMsg;
	}
	std::stringstream initMsg(kotonohaData::initOpts data) {
		std::stringstream initMsg;
		std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
		std::time_t time = std::chrono::system_clock::to_time_t(now);
		char buffer[80];
		std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
		std::string st(buffer);
		initMsg << "Kotonoha Project ver." << KOTONOHA_VERSION << std::endl;
		initMsg << "========================================" << std::endl;
		initMsg << "SDL Version: " << SDL_MAJOR_VERSION << "." << SDL_MINOR_VERSION << "." << SDL_PATCHLEVEL << std::endl;
		initMsg << "SDL_TTF Version: " << SDL_TTF_MAJOR_VERSION << "." << SDL_TTF_MINOR_VERSION << "." << SDL_TTF_PATCHLEVEL << std::endl;
		initMsg << "SDL_Image Version: " << SDL_IMAGE_MAJOR_VERSION << "." << SDL_IMAGE_MINOR_VERSION << "." << SDL_IMAGE_PATCHLEVEL << std::endl;
		initMsg << "SDL_Mixer Version: " << SDL_MIXER_MAJOR_VERSION << "." << SDL_MIXER_MINOR_VERSION << "." << SDL_MIXER_PATCHLEVEL << std::endl;
		initMsg << "ImGUI Version: " << IMGUI_VERSION << std::endl;
		initMsg << "Avcodec Version: " << LIBAVCODEC_VERSION_MAJOR << "." << LIBAVCODEC_VERSION_MINOR << "." << LIBAVCODEC_VERSION_MICRO << std::endl;
		initMsg << "Avformat Version: " << LIBAVFORMAT_VERSION_MAJOR << "." << LIBAVFORMAT_VERSION_MINOR << "." << LIBAVFORMAT_VERSION_MICRO << std::endl;
		initMsg << "Avutil Version: " << LIBAVUTIL_VERSION_MAJOR << "." << LIBAVUTIL_VERSION_MINOR << "." << LIBAVUTIL_VERSION_MICRO << std::endl;
		initMsg << "Asslib Version: " << LIBASS_VERSION << std::endl;
		initMsg << "This version as compile using " << COMPILE << std::endl;
		initMsg << "SDL Video Driver: " << SDL_GetVideoDriver(0) << std::endl;
		initMsg << "System CPU cores: " << SDL_GetCPUCount() << std::endl;
		initMsg << "System RAM: " << SDL_GetSystemRAM() << std::endl;
		initMsg << "Start time: " << st << std::endl;
		initMsg << "TPS deley set to: " << data.delayTps << std::endl;
		initMsg << "The program as run on this path: " << std::filesystem::current_path() << std::endl;
		initMsg << "The script file is: " << data.scriptPath << std::endl;
		initMsg << "The config file is: " << data.configLoad << std::endl;
		initMsg << "Hardware video decoding is: " << data.hwVideo << std::endl;
		initMsg << "========================================" << std::endl;
		return initMsg;
	}
	kotonohaData::initOpts args(int argc, char* args[]) {
		kotonohaData::initOpts opts;
		if (argc > 1) {
			for (int i = 1; i < argc; i++) {
				if (strcmp(args[i], "-v") == 0) {
					opts.init = false;
					//std::count << "Kotonoha Project ver." << KOTONOHA_VERSION << std::endl;
					return opts;
				}
				else if (strcmp(args[i], "-h") == 0) {
					opts.init = false;
					std::cout << kotonoha::help().str();
					return opts;
				}
				else if (strcmp(args[i], "-d") == 0 && i > argc) {
					opts.hwVideo = std::stoi(args[i + 1]);
					i++;
				}
				else if (strcmp(args[i], "-t") == 0 && i > argc) {
					opts.delayTps = std::stoi(args[i + 1]);
					i++;
				}
				else if (strcmp(args[i], "-c") == 0 && i > argc) {
					STRCPYFIX(opts.configLoad, args[i + 1]);
					i++;
				}
				else if (strcmp(args[i], "-m") == 0 && i > argc) {
					STRCPYFIX(opts.masterPath, args[i + 1]);
					i++;
				}
				else if (strcmp(args[i], "-l") == 0 && i > argc) {
					STRCPYFIX(opts.logPath, args[i + 1]);
					i++;
				}
				else {
					opts.fromComandLine = true;
					STRCPYFIX(opts.scriptPath, args[i]);
				}
			}
		}
		return opts;
	}
	class game {
		public:
		kotonohaData::envComponets env;
		game(int argc, char* args[]) {
			env.initData = kotonoha::args(argc, args);
			if (env.initData.init) {
				kotonoha::start(&env);
				env.log = new kotonoha::logger;
				env.log->open(env.initData.logPath);
				env.log->appendLog(args[0]);
				env.log->appendLog(kotonoha::initMsg(env.initData).str());
				env.started = true;
			}
		}
		~game() {
			env.log->appendLog("Closing Kotonoha Project");
			env.log->appendLog("========================================");
			kotonoha::close(&env);
		}
	};
}