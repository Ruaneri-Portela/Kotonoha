#include <Kotonoha/components/Sound.hpp>

namespace Kotonoha
{

	// Implementa��o da classe Sound::Channel::Pipe
	Sound::Channel::Pipe::Pipe(KOTONOHA_AUDIO_COMPONENTS) : function(function), closeFucntion(closeFunction), parms(parms), data(nullptr), lenghtData(0), lastGetData(0)
	{
		Render();
	}

	Uint8 *Sound::Channel::Pipe::GetData(int optimalSize, int *gettedSize)
	{
		Uint8 *result = nullptr;

		if (lastGetData >= lenghtData)
		{
			SDL_free(data);
			data = nullptr;
			Render();
		}

		if (!data)
		{
			*gettedSize = 0;
			return result;
		}

		if (optimalSize >= (lenghtData - lastGetData))
		{
			*gettedSize = lenghtData - lastGetData;
			result = data + lastGetData;
			lastGetData = lenghtData;
			return result;
		}

		*gettedSize = optimalSize;
		result = data + lastGetData;
		lastGetData += optimalSize;
		return result;
	}

	int Sound::Channel::Pipe::Render()
	{
		if (!function)
			return INT_MIN;

		if (data)
			SDL_free(data);

		lenghtData = 0;
		lastGetData = 0;
		return function(parms, &data, &lenghtData);
	}

	Sound::Channel::Pipe::~Pipe()
	{
		if (closeFucntion != nullptr)
			closeFucntion(parms);
		if (data)
			SDL_free(data);
	}

	// Implementa��o da classe Sound::Channel
	Sound::Channel::Channel(Sound *parent, SDL_AudioSpec spec, bool startPaused, bool *status)
		: spec(spec), stream(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Render, &parms))
	{
		if (!stream)
		{
			SDL_LogError(0, "Failed to open audio: %s", SDL_GetError());
			*status = false;
			return;
		}

		parms = static_cast<void **>(SDL_malloc(sizeof(void *) * 2));
		if (parms == nullptr)
		{
			SDL_LogError(0, "Failed to alloc parms");
			*status = false;
			return;
		}

		parms[0] = this;
		parms[1] = parent;

		if (!startPaused)
		{
			SDL_ResumeAudioStreamDevice(stream);
		}

		*status = true;
		lockPipes = SDL_CreateMutex();
	}

	Sound::Channel::~Channel()
	{
		inExit = true;
		SDL_DestroyMutex(lockPipes);
	}

	SDL_AudioSpec Sound::Channel::GetSpecs()
	{
		return spec;
	}

	Sound::Channel::Pipe *Sound::Channel::AddPipe(KOTONOHA_AUDIO_COMPONENTS)
	{
		Pipe *pipe = new Pipe(function, closeFunction, parms);
		SDL_LockMutex(lockPipes);
		pipes.push_back(pipe);
		SDL_UnlockMutex(lockPipes);
		return pipe;
	}

	void Sound::Channel::RemovePipe(Pipe *ptr)
	{
		SDL_LockMutex(lockPipes);
		if (pipes.empty())
			;
		else if (ptr == nullptr)
		{
			for (auto *pipe : pipes)
			{
				delete pipe;
			}
			pipes.clear();
		}
		else
		{
			auto it = std::remove(pipes.begin(), pipes.end(), ptr);
			if (it != pipes.end())
			{
				delete ptr;
				pipes.erase(it, pipes.end());
			}
		}
		SDL_UnlockMutex(lockPipes);
	}

	void Sound::Channel::Render(void *userdata, SDL_AudioStream *astream, int additionalAmount, int totalAmount)
	{
		if (userdata == nullptr)
			return;

		void **parms = *(static_cast<void ***>(userdata));

		Channel *thisChannel = static_cast<Channel *>(parms[0]);
		Sound *thisSound = static_cast<Sound *>(parms[1]);

		if (thisChannel == nullptr || thisSound == nullptr || thisChannel->pipes.empty())
			return;

		Uint8 *data = nullptr;

		SDL_LockMutex(thisChannel->lockPipes);

		auto it = thisChannel->pipes.begin();
		int filled = 0;	   // Quantidade de dados preenchidos no buffer
		int maxFilled = 0; // Quantidade m�xima de dados preenchidos at� o momento
		bool firts = true;
		// Loop atrav�s dos pipes para obter dados de �udio
		while (it != thisChannel->pipes.end())
		{
			int gettedSize = 0;
			Uint8 *localData = (*it)->GetData(totalAmount - filled, &gettedSize);

			if (localData == nullptr)
			{
				filled = 0;
				++it;
				continue;
			}

			if (gettedSize > 0 && data == nullptr)
			{
				data = static_cast<Uint8 *>(SDL_calloc(totalAmount, 1));
				if (data == nullptr)
				{
					SDL_LogError(0, "Failed to alloc data");
					return;
				}
			}

			// Mistura os dados obtidos no buffer
			Uint8 *target = data + filled;
			SDL_MixAudio(target, localData, thisChannel->spec.format, gettedSize, thisChannel->volume * thisSound->volume);
			filled += gettedSize;								   // Atualiza a quantidade preenchida
			maxFilled = (maxFilled > filled) ? maxFilled : filled; // Atualiza o m�ximo preenchido

			// Se o buffer j� est� cheio, vai para o pr�ximo pipe
			if (filled >= totalAmount)
			{
				filled = 0;
				++it;
			}
		}

		SDL_UnlockMutex(thisChannel->lockPipes);
		// Se n�o foram preenchidos dados, retorna
		if (maxFilled == 0)
		{
			if (data)
				SDL_free(data);
			return;
		}

		// Envia os dados para o stream
		SDL_PutAudioStreamData(astream, data, totalAmount);
		SDL_FlushAudioStream(astream);
		if (data)
			SDL_free(data);
	}

	// Implementa��o da classe Sound
	Sound::Sound()
	{
		lockChannels = SDL_CreateMutex();
	}

	Sound::~Sound()
	{
		for (auto *channel : channels)
		{
			delete channel;
		}
		channels.clear();
		SDL_DestroyMutex(lockChannels);
	}

	Sound::Channel *Sound::CreateChannel(SDL_AudioFormat fmt, int channels, int freq, bool startPaused, const char *name, size_t *newIndex)
	{
		bool status = false;
		Sound::Channel *channel = new Sound::Channel(this, {fmt, channels, freq}, startPaused, &status);
		channel->name = name;

		if (!status)
		{
			delete channel; // Libera mem�ria se a cria��o falhar
			return nullptr;
		}
		SDL_LockMutex(lockChannels);
		this->channels.push_back(channel);
		if (newIndex)
		{
			*newIndex = this->channels.size() - 1;
		}
		SDL_UnlockMutex(lockChannels);
		return channel;
	}

	Sound::Channel *Sound::GetChannelByIndex(size_t index)
	{
		if (index < 0 || index >= this->channels.size())
		{
			return nullptr; // Verifica se o �ndice � v�lido
		}
		return this->channels[index];
	}

	Sound::Channel *Sound::GetChannelByName(std::string name)
	{
		SDL_LockMutex(lockChannels);
		Sound::Channel *rt = nullptr;
		for (auto *channel : channels)
		{
			if (channel->name == name)
			{
				rt = channel;
				break;
			}
		}
		SDL_UnlockMutex(lockChannels);
		return rt;
	}

	void Sound::CleanupPipes(Channel *ch)
	{
		SDL_LockMutex(lockChannels);
		if (!channels.empty())
		{
			for (auto *channel : channels)
			{
				if (channel == ch || ch == nullptr)
				{
					channel->RemovePipe(nullptr);
				}
			}
		}
		SDL_UnlockMutex(lockChannels);
	}

	void Sound::DestroyChannel(Channel *ptr)
	{
		SDL_LockMutex(lockChannels);
		auto it = std::remove(channels.begin(), channels.end(), ptr);
		if (it != channels.end())
		{
			delete ptr; // Libera mem�ria do canal
			channels.erase(it, channels.end());
		}
		SDL_UnlockMutex(lockChannels);
	}
}