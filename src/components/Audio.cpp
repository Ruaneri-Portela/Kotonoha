#include <Kotonoha/components/Audio.hpp>

namespace Kotonoha
{
	Audio::Audio(Sound *soundCtx, struct Kotonoha_time *timeCtx)
		: soundCtx(soundCtx), timeCtx(timeCtx) {}

	struct Kotonoha_audioDecode *Audio::AddMedia(const char *path, Uint64 start, Uint64 end, bool inLoop, const char *channel)
	{
		// Obtém o canal pelo nome
		Sound::Channel *gettedCh = soundCtx->GetChannelByName(channel);
		if (gettedCh == nullptr) // Verifica se o canal é válido
			return nullptr;

		// Inicializa ponteiro para o vetor de áudio e pipes
		std::vector<std::tuple<struct Kotonoha_audioDecode *, Sound::Channel::Pipe *>> *audioDecodesPipe = nullptr;

		// Procura se o canal já existe em mediaObjects
		for (auto &object : mediaObjects)
		{
			if (std::get<0>(object) == gettedCh)
			{
				audioDecodesPipe = &std::get<1>(object);
				break; // Canal encontrado, sai do loop
			}
		}

		// Se o canal não foi encontrado, adiciona uma nova tupla
		if (audioDecodesPipe == nullptr)
		{
			std::vector<std::tuple<Kotonoha_audioDecode *, Sound::Channel::Pipe *>> innerVector;
			mediaObjects.push_back(std::make_tuple(gettedCh, innerVector));
			audioDecodesPipe = &std::get<1>(mediaObjects.back()); // Obtém referência ao vetor recém-criado
		}

		// Inicializa e adiciona o novo Kotonoha_audioDecode ao vetor
		Kotonoha_audioDecode *newAudioDecode = Kotonoha_AudioInit(path, gettedCh->GetSpecs());
		if (newAudioDecode == nullptr)
		{
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error initializing audio decode: %s", path);
			return nullptr;
		}
		newAudioDecode->tm = &timeCtx;
		newAudioDecode->start = start;
		newAudioDecode->end = end;
		newAudioDecode->dataGeneric = static_cast<void *>(this);
		newAudioDecode->inLoop = inLoop;

		// Adiciona o pipe ao canal
		Sound::Channel::Pipe *newPipe = gettedCh->AddPipe(RenderMedia, Kotonoha_AudioFree, newAudioDecode);
		audioDecodesPipe->push_back(std::make_tuple(newAudioDecode, newPipe));

		return newAudioDecode; // Retorna o novo objeto adicionado
	}

	void Audio::RemoveMedia(struct Kotonoha_audioDecode *mediaPtr)
	{
		for (auto it = mediaObjects.begin(); it != mediaObjects.end();)
		{
			// Obtendo o vetor de tuplas de áudio e pipe
			auto &audioDecodesPipe = std::get<1>(*it);
			auto &channel = std::get<0>(*it);

			// Itera sobre o vetor de tuplas de áudio e pipes
			for (auto audioIt = audioDecodesPipe.begin(); audioIt != audioDecodesPipe.end();)
			{
				// Verifica se o ponteiro corresponde ou se queremos remover todos
				if (mediaPtr == nullptr || std::get<0>(*audioIt) == mediaPtr)
				{
					// Remove o pipe associado, se não for nulo
					Sound::Channel::Pipe *pipePtr = std::get<1>(*audioIt);
					if (pipePtr != nullptr)
					{
						channel->RemovePipe(pipePtr); // Remove o pipe do canal
					}

					// Remove a tupla do vetor
					audioIt = audioDecodesPipe.erase(audioIt); // Erase e continue a iteração
				}
				else
				{
					++audioIt; // Avança apenas se nada foi removido
				}
			}

			// Se o vetor de audioDecodesPipe estiver vazio após a remoção, remove o objeto de mediaObjects
			if (audioDecodesPipe.empty())
			{
				it = mediaObjects.erase(it); // Remove o objeto do vetor de mediaObjects
			}
			else
			{
				++it; // Avança para o próximo objeto
			}
		}
	}

	int Audio::RenderMedia(void *data, Uint8 **target, int *size)
	{
		// Cast do ponteiro para o tipo correto
		struct Kotonoha_audioDecode *instance = static_cast<struct Kotonoha_audioDecode *>(data);
		Audio *thisClass = static_cast<Audio *>(instance->dataGeneric);
		// Chama a função de renderização de áudio
		bool inRange;
		int diff;
		instance->lastTime = Kotonoha_timeGetFromEvent(*instance->tm, instance->start, instance->end, &inRange, &diff);
		if (inRange)
		{
			int rt = Kotonoha_AudioRender(data, target, size);
			if (rt == -1 && instance->inLoop)
			{
				Kotonoha_AudioSeek(instance, 0);
			}
			return rt;
		}
		return 0;
	}

	Audio::~Audio()
	{
		RemoveMedia(nullptr); // Remove todos os médias ao destruir o objeto
	}
}
