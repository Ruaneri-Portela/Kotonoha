#include <Kotonoha/components/Audio.hpp>

namespace Kotonoha {

	Audio::Audio(Sound* soundCtx, struct Kotonoha_time* timeCtx)
		: soundCtx(soundCtx), timeCtx(timeCtx) {}

	struct Kotonoha_audioDecode* Audio::AddMedia(const char* path,
		Uint64 start,
		Uint64 end,
		bool inLoop,
		const char* channel) {
		if (soundCtx == nullptr || timeCtx == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Invalid audio context.");
			return nullptr;
		}

		if (path == nullptr || *path == '\0') {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Invalid audio path.");
			return nullptr;
		}

		if (channel == nullptr || *channel == '\0') {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Invalid channel name.");
			return nullptr;
		}

		Sound::Channel* targetChannel = soundCtx->GetChannelByName(channel);
		if (targetChannel == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Channel not found: %s", channel);
			return nullptr;
		}

		std::vector<std::tuple<Kotonoha_audioDecode*, Sound::Channel::Pipe*>>* audioDecodesPipe = nullptr;

		for (auto& object : mediaObjects) {
			if (std::get<0>(object) == targetChannel) {
				audioDecodesPipe = &std::get<1>(object);
				break;
			}
		}

		if (audioDecodesPipe == nullptr) {
			mediaObjects.emplace_back(
				targetChannel,
				std::vector<std::tuple<Kotonoha_audioDecode*, Sound::Channel::Pipe*>>{});
			audioDecodesPipe = &std::get<1>(mediaObjects.back());
		}

		Kotonoha_audioDecode* newAudioDecode =
			Kotonoha_AudioInit(path, targetChannel->GetSpecs());
		if (newAudioDecode == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Error initializing audio decode: %s", path);
			return nullptr;
		}

		newAudioDecode->tm = &timeCtx;
		newAudioDecode->start = start;
		newAudioDecode->end = end;
		newAudioDecode->dataGeneric = static_cast<void*>(this);
		newAudioDecode->inLoop = inLoop;

		Sound::Channel::Pipe* newPipe =
			targetChannel->AddPipe(RenderMedia, Kotonoha_AudioFree, newAudioDecode);

		if (newPipe == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Failed to create audio pipe: %s", path);
			Kotonoha_AudioFree(newAudioDecode);
			return nullptr;
		}

		audioDecodesPipe->emplace_back(newAudioDecode, newPipe);
		return newAudioDecode;
	}

	void Audio::RemoveMedia(struct Kotonoha_audioDecode* mediaPtr) {
		for (auto mediaIt = mediaObjects.begin(); mediaIt != mediaObjects.end();) {
			Sound::Channel* channel = std::get<0>(*mediaIt);
			auto& audioDecodesPipe = std::get<1>(*mediaIt);

			for (auto pipeIt = audioDecodesPipe.begin();
				pipeIt != audioDecodesPipe.end();) {
				Kotonoha_audioDecode* decodePtr = std::get<0>(*pipeIt);
				Sound::Channel::Pipe* pipePtr = std::get<1>(*pipeIt);

				if (mediaPtr == nullptr || decodePtr == mediaPtr) {
					if (channel != nullptr && pipePtr != nullptr) {
						channel->RemovePipe(pipePtr);
					}

					pipeIt = audioDecodesPipe.erase(pipeIt);
				}
				else {
					++pipeIt;
				}
			}

			if (audioDecodesPipe.empty()) {
				mediaIt = mediaObjects.erase(mediaIt);
			}
			else {
				++mediaIt;
			}
		}
	}

	int Audio::RenderMedia(void* data, Uint8** target, int* size) {
		if (data == nullptr || target == nullptr || size == nullptr) {
			return 0;
		}

		Kotonoha_audioDecode* instance =
			static_cast<Kotonoha_audioDecode*>(data);

		if (instance->tm == nullptr || *instance->tm == nullptr) {
			return 0;
		}

		bool inRange = false;
		Sint64 diff = 0;

		instance->lastTime = Kotonoha_timeGetFromEvent(
			*instance->tm,
			instance->start,
			instance->end,
			&inRange,
			&diff);

		if (!inRange) {
			return 0;
		}

		const int rt = Kotonoha_AudioRender(instance, target, size);
		if (rt == -1 && instance->inLoop) {
			Kotonoha_AudioSeek(instance, instance->lastTime);
		}

		return rt;
	}

	Audio::~Audio() {
		RemoveMedia(nullptr);
	}

} // namespace Kotonoha