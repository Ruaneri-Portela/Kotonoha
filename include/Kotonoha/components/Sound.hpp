#pragma once
#include <algorithm>
#include <climits>
#include <string>
#include <vector>

extern "C"
{
#include <SDL3/SDL.h>
#include <Kotonoha/Kotonoha.h>
}

namespace Kotonoha
{
	class Sound
	{
	public:
		class Channel
		{
		public:
			std::string name;
			class Pipe
			{
			private:
				int (*function)(void *parms, Uint8 **target, int *size) = nullptr;
				void (*closeFucntion)(void *parms) = nullptr;
				void *parms = nullptr;
				Uint8 *data = nullptr;
				int lenghtData = 0;
				int lastGetData = 0;

			public:
				Pipe(KOTONOHA_AUDIO_COMPONENTS);
				Uint8 *GetData(int optimalSize, int *gettedSize);
				int Render();
				~Pipe();
			};

			Channel(Sound *parent, SDL_AudioSpec spec, bool startPaused, bool *status);
			SDL_AudioSpec GetSpecs();
			Pipe *AddPipe(KOTONOHA_AUDIO_COMPONENTS);
			void RemovePipe(Pipe *ptr);
			static void SDLCALL Render(void *userdata, SDL_AudioStream *astream, int additionalAmount, int totalAmount);
			SDL_Mutex *lockPipes = NULL;
			~Channel();

		private:
			float volume = 1.0f;
			void **parms = nullptr;
			bool inExit = false;
			bool close = false;
			std::vector<Pipe *> pipes;
			SDL_AudioStream *stream;
			SDL_AudioSpec spec;
		};

		Sound();

		Channel *CreateChannel(SDL_AudioFormat fmt, int channels, int freq, bool startPaused, const char *name, size_t *newIndex);

		Channel *GetChannelByIndex(size_t index);

		Channel *GetChannelByName(std::string name);

		void CleanupPipes(Channel *ch);

		void DestroyChannel(Channel *ptr);

		~Sound();

	private:
		float volume = 1.0f;
		std::vector<Channel *> channels;
		SDL_Mutex *lockChannels;
	};
}