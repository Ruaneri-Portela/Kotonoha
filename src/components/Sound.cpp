#include <algorithm>
#include <climits>
#include <Kotonoha/components/Sound.hpp>

namespace Kotonoha {

	Sound::Channel::Pipe::Pipe(KOTONOHA_AUDIO_COMPONENTS)
		: function(function),
		closeFucntion(closeFunction),
		parms(parms),
		data(nullptr),
		lenghtData(0),
		lastGetData(0) {
		Render();
	}

	Uint8* Sound::Channel::Pipe::GetData(int optimalSize, int* gettedSize) {
		if (gettedSize == nullptr) {
			return nullptr;
		}

		*gettedSize = 0;

		if (lastGetData >= lenghtData) {
			if (data != nullptr) {
				SDL_free(data);
				data = nullptr;
			}
			Render();
		}

		if (data == nullptr || lenghtData <= 0 || optimalSize <= 0) {
			return nullptr;
		}

		Uint8* result = data + lastGetData;
		const int remaining = lenghtData - lastGetData;

		if (optimalSize >= remaining) {
			*gettedSize = remaining;
			lastGetData = lenghtData;
			return result;
		}

		*gettedSize = optimalSize;
		lastGetData += optimalSize;
		return result;
	}

	int Sound::Channel::Pipe::Render() {
		if (function == nullptr) {
			return INT_MIN;
		}

		if (data != nullptr) {
			SDL_free(data);
			data = nullptr;
		}

		lenghtData = 0;
		lastGetData = 0;
		return function(parms, &data, &lenghtData);
	}

	Sound::Channel::Pipe::~Pipe() {
		if (closeFucntion != nullptr) {
			closeFucntion(parms);
		}

		if (data != nullptr) {
			SDL_free(data);
			data = nullptr;
		}
	}

	Sound::Channel::Channel(Sound* parent, SDL_AudioSpec spec, bool startPaused,
		bool* status)
		: spec(spec), stream(nullptr), parms(nullptr), lockPipes(nullptr) {
		if (status == nullptr) {
			return;
		}

		*status = false;

		parms = static_cast<void**>(SDL_malloc(sizeof(void*) * 2));
		if (parms == nullptr) {
			SDL_LogError(0, "Failed to alloc parms");
			return;
		}

		parms[0] = this;
		parms[1] = parent;

		lockPipes = SDL_CreateMutex();
		if (lockPipes == nullptr) {
			SDL_LogError(0, "Failed to create pipe mutex: %s", SDL_GetError());
			SDL_free(parms);
			parms = nullptr;
			return;
		}

		stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
			&spec, Render, &parms);
		if (stream == nullptr) {
			SDL_LogError(0, "Failed to open audio: %s", SDL_GetError());
			SDL_DestroyMutex(lockPipes);
			lockPipes = nullptr;
			SDL_free(parms);
			parms = nullptr;
			return;
		}

		if (!startPaused) {
			SDL_ResumeAudioStreamDevice(stream);
		}

		*status = true;
	}

	Sound::Channel::~Channel() {
		inExit = true;

		RemovePipe(nullptr);

		if (stream != nullptr) {
			SDL_DestroyAudioStream(stream);
			stream = nullptr;
		}

		if (lockPipes != nullptr) {
			SDL_DestroyMutex(lockPipes);
			lockPipes = nullptr;
		}

		if (parms != nullptr) {
			SDL_free(parms);
			parms = nullptr;
		}
	}

	SDL_AudioSpec Sound::Channel::GetSpecs() { return spec; }

	Sound::Channel::Pipe* Sound::Channel::AddPipe(KOTONOHA_AUDIO_COMPONENTS) {
		Pipe* pipe = new Pipe(function, closeFunction, parms);
		if (pipe == nullptr) {
			return nullptr;
		}

		if (lockPipes == nullptr) {
			delete pipe;
			return nullptr;
		}

		SDL_LockMutex(lockPipes);
		pipes.push_back(pipe);
		SDL_UnlockMutex(lockPipes);
		return pipe;
	}

	void Sound::Channel::RemovePipe(Pipe* ptr) {
		if (lockPipes == nullptr) {
			if (ptr == nullptr) {
				for (auto* pipe : pipes) {
					delete pipe;
				}
				pipes.clear();
			}
			return;
		}

		SDL_LockMutex(lockPipes);

		if (ptr == nullptr) {
			for (auto* pipe : pipes) {
				delete pipe;
			}
			pipes.clear();
			SDL_UnlockMutex(lockPipes);
			return;
		}

		auto it = std::find(pipes.begin(), pipes.end(), ptr);
		if (it != pipes.end()) {
			delete* it;
			pipes.erase(it);
		}

		SDL_UnlockMutex(lockPipes);
	}

	static void SDLCALL destroy(void* userdata, const void* buf, int buflen) {
		(void)buf;
		(void)buflen;

		if (userdata != nullptr) {
			SDL_free(userdata);
		}
	}

	void Sound::Channel::Render(void* userdata, SDL_AudioStream* astream,
		int additionalAmount, int totalAmount) {
		(void)additionalAmount;

		if (userdata == nullptr || astream == nullptr || totalAmount <= 0) {
			return;
		}

		void** parms = *(static_cast<void***>(userdata));
		if (parms == nullptr) {
			return;
		}

		Channel* thisChannel = static_cast<Channel*>(parms[0]);
		Sound* thisSound = static_cast<Sound*>(parms[1]);

		if (thisChannel == nullptr || thisSound == nullptr ||
			thisChannel->lockPipes == nullptr) {
			return;
		}

		SDL_LockMutex(thisChannel->lockPipes);

		if (thisChannel->pipes.empty()) {
			SDL_UnlockMutex(thisChannel->lockPipes);
			return;
		}

		Uint8* data = nullptr;
		int maxFilled = 0;

		for (auto* pipe : thisChannel->pipes) {
			if (pipe == nullptr) {
				continue;
			}

			int filled = 0;

			while (filled < totalAmount) {
				int gettedSize = 0;
				Uint8* localData = pipe->GetData(totalAmount - filled, &gettedSize);

				if (localData == nullptr || gettedSize <= 0) {
					break;
				}

				if (data == nullptr) {
					data = static_cast<Uint8*>(SDL_calloc(totalAmount, 1));
					if (data == nullptr) {
						SDL_UnlockMutex(thisChannel->lockPipes);
						SDL_LogError(0, "Failed to alloc audio mix buffer");
						return;
					}
				}

				SDL_MixAudio(data + filled, localData, thisChannel->spec.format,
					gettedSize, thisChannel->volume * thisSound->volume);

				filled += gettedSize;
				if (filled > maxFilled) {
					maxFilled = filled;
				}
			}
		}

		SDL_UnlockMutex(thisChannel->lockPipes);

		if (data == nullptr || maxFilled == 0) {
			if (data != nullptr) {
				SDL_free(data);
			}
			return;
		}

		if (!SDL_PutAudioStreamDataNoCopy(astream, data, totalAmount, destroy, data)) {
			SDL_free(data);
			SDL_LogError(0, "Failed to queue audio data: %s", SDL_GetError());
		}
	}

	Sound::Sound() {
		lockChannels = SDL_CreateMutex();
	}

	Sound::~Sound() {
		CleanupPipes(nullptr);

		if (lockChannels != nullptr) {
			SDL_LockMutex(lockChannels);
			for (auto* channel : channels) {
				delete channel;
			}
			channels.clear();
			SDL_UnlockMutex(lockChannels);

			SDL_DestroyMutex(lockChannels);
			lockChannels = nullptr;
		}
	}

	Sound::Channel* Sound::CreateChannel(SDL_AudioFormat fmt, int channels,
		int freq, bool startPaused,
		const char* name, size_t* newIndex) {
		if (lockChannels == nullptr) {
			return nullptr;
		}

		bool status = false;
		Sound::Channel* channel =
			new Sound::Channel(this, { fmt, channels, freq }, startPaused, &status);

		if (channel == nullptr) {
			return nullptr;
		}

		channel->name = name ? name : "";

		if (!status) {
			delete channel;
			return nullptr;
		}

		SDL_LockMutex(lockChannels);
		this->channels.push_back(channel);
		if (newIndex != nullptr) {
			*newIndex = this->channels.size() - 1;
		}
		SDL_UnlockMutex(lockChannels);

		return channel;
	}

	Sound::Channel* Sound::GetChannelByIndex(size_t index) {
		if (index >= this->channels.size()) {
			return nullptr;
		}
		return this->channels[index];
	}

	Sound::Channel* Sound::GetChannelByName(std::string name) {
		if (lockChannels == nullptr) {
			return nullptr;
		}

		SDL_LockMutex(lockChannels);

		Sound::Channel* result = nullptr;
		for (auto* channel : channels) {
			if (channel != nullptr && channel->name == name) {
				result = channel;
				break;
			}
		}

		SDL_UnlockMutex(lockChannels);
		return result;
	}

	void Sound::CleanupPipes(Channel* ch) {
		if (lockChannels == nullptr) {
			return;
		}

		SDL_LockMutex(lockChannels);
		for (auto* channel : channels) {
			if (channel == ch || ch == nullptr) {
				channel->RemovePipe(nullptr);
			}
		}
		SDL_UnlockMutex(lockChannels);
	}

	void Sound::DestroyChannel(Channel* ptr) {
		if (lockChannels == nullptr || ptr == nullptr) {
			return;
		}

		SDL_LockMutex(lockChannels);

		auto it = std::find(channels.begin(), channels.end(), ptr);
		if (it != channels.end()) {
			Channel* channel = *it;
			channels.erase(it);
			SDL_UnlockMutex(lockChannels);

			delete channel;
			return;
		}

		SDL_UnlockMutex(lockChannels);
	}

} // namespace Kotonoha
