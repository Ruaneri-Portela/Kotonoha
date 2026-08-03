#include <Kotonoha/components/Prompt.hpp>
#include <SDL3/SDL_stdinc.h>
#include <string>
#include <vector>

namespace Kotonoha {

	namespace {
		struct PromptLayoutItem {
			int originalIndex;
			SDL_Rect rect;
		};

		static bool PromptTextValid(const std::string& text) {
			return !text.empty();
		}

		static std::vector<int> BuildVisiblePromptIndices(
			const std::vector<std::string>& items) {
			std::vector<int> indices;
			indices.reserve(items.size());

			for (int i = 0; i < static_cast<int>(items.size()); ++i) {
				if (PromptTextValid(items[i])) {
					indices.push_back(i);
				}
			}

			return indices;
		}
	} // namespace

	Prompt::Prompt(std::vector<std::string> prompt,
		int* promptResult,
		Uint64 start,
		Uint64 end,
		Kotonoha_time* time)
		: prompt(std::move(prompt)),
		promptResult(promptResult),
		time(time),
		startTime(start),
		endTime(end),
		font(nullptr) {
		font = TTF_OpenFont("assets/fonts/ConcertOne-Regular.ttf", 36);
		if (font == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Failed to open prompt font: %s",
				SDL_GetError());
		}
	}

	enum Kotonoha_Scene_Status Prompt::Render(KOTONOHA_SCENE_CALL) {
		if (userData == nullptr || render == nullptr || target == nullptr) {
			return KOTONOHA_SCENE_NULL;
		}

		auto* promptObj = static_cast<Prompt*>(userData);

		if (promptObj->font == nullptr || promptObj->time == nullptr ||
			promptObj->promptResult == nullptr) {
			return KOTONOHA_SCENE_NULL;
		}

		bool inRange = false;
		Sint64 diff = 0;
		(void)diff;

		Kotonoha_timeGetFromEvent(promptObj->time,
			promptObj->startTime,
			promptObj->endTime,
			&inRange,
			&diff);

		if (!inRange) {
			return KOTONOHA_SCENE_NULL;
		}

		const std::vector<int> visibleIndices =
			BuildVisiblePromptIndices(promptObj->prompt);

		if (visibleIndices.empty()) {
			return KOTONOHA_SCENE_DRAW_OVERLAYED;
		}

		float mouseX = 0.0f;
		float mouseY = 0.0f;
		SDL_GetMouseState(&mouseX, &mouseY);

		std::vector<PromptLayoutItem> layout;
		layout.reserve(visibleIndices.size());

		const int targetW = static_cast<int>(target->w);
		const int targetH = static_cast<int>(target->h);
		const int centerY = targetH / 2;
		const int halfW = targetW / 2;
		const int paddingX = 24;
		const int paddingY = 16;

		if (visibleIndices.size() == 1) {
			const int idx = visibleIndices[0];

			int textW = 0;
			int textH = 0;
			if (!TTF_GetStringSize(promptObj->font,
				promptObj->prompt[idx].c_str(),
				promptObj->prompt[idx].size(),
				&textW,
				&textH)) {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
					"Failed to measure prompt text: %s",
					SDL_GetError());
				return KOTONOHA_SCENE_DRAW_OVERLAYED;
			}

			SDL_Rect rect{};
			rect.w = textW + paddingX * 2;
			rect.h = textH + paddingY * 2;
			rect.x = (targetW - rect.w) / 2;
			rect.y = (targetH - rect.h) / 2;

			layout.push_back({ idx, rect });
		}
		else {
			const size_t count = visibleIndices.size() > 2 ? 2 : visibleIndices.size();

			for (size_t slot = 0; slot < count; ++slot) {
				const int idx = visibleIndices[slot];

				int textW = 0;
				int textH = 0;
				if (!TTF_GetStringSize(promptObj->font,
					promptObj->prompt[idx].c_str(),
					promptObj->prompt[idx].size(),
					&textW,
					&textH)) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Failed to measure prompt text: %s",
						SDL_GetError());
					continue;
				}

				SDL_Rect rect{};
				rect.w = textW + paddingX * 2;
				rect.h = textH + paddingY * 2;

				const int regionX = (slot == 0) ? 0 : halfW;
				const int regionW = halfW;

				rect.x = regionX + (regionW - rect.w) / 2;
				rect.y = centerY - (rect.h / 2);

				layout.push_back({ idx, rect });
			}
		}

		void* persistent = nullptr;
		SDL_Event event{};

		do {
			event = Kotonoha_eventRead(eventQueu, &persistent);

			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
				event.button.button == SDL_BUTTON_LEFT) {
				for (const auto& item : layout) {
					const bool hovered =
						mouseX >= item.rect.x &&
						mouseX <= (item.rect.x + item.rect.w) &&
						mouseY >= item.rect.y &&
						mouseY <= (item.rect.y + item.rect.h);

					if (!hovered) {
						continue;
					}

					if (*(promptObj->promptResult) == item.originalIndex) {
						*(promptObj->promptResult) = -1;
					}
					else {
						*(promptObj->promptResult) = item.originalIndex;
					}
					break;
				}
			}
		} while (persistent != nullptr);

		for (const auto& item : layout) {
			const bool hovered =
				mouseX >= item.rect.x &&
				mouseX <= (item.rect.x + item.rect.w) &&
				mouseY >= item.rect.y &&
				mouseY <= (item.rect.y + item.rect.h);

			const bool selected = (*(promptObj->promptResult) == item.originalIndex);

			SDL_Color fg = selected
				? SDL_Color{ 255, 230, 120, 255 }
				: hovered
				? SDL_Color{ 180, 220, 255, 255 }
			: SDL_Color{ 255, 255, 255, 255 };

			SDL_Color shadow = { 0, 0, 0,static_cast<Uint8>(hovered ? 200 : 160) };

			SDL_Surface* shadowSurface =
				TTF_RenderText_Blended(promptObj->font,
					promptObj->prompt[item.originalIndex].c_str(),
					promptObj->prompt[item.originalIndex].size(),
					shadow);
			if (shadowSurface == nullptr) {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
					"Failed to render prompt shadow surface: %s",
					SDL_GetError());
				continue;
			}

			SDL_Texture* shadowTexture =
				SDL_CreateTextureFromSurface(render, shadowSurface);
			if (shadowTexture == nullptr) {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
					"Failed to create prompt shadow texture: %s",
					SDL_GetError());
				SDL_DestroySurface(shadowSurface);
				continue;
			}

			SDL_Surface* textSurface =
				TTF_RenderText_Blended(promptObj->font,
					promptObj->prompt[item.originalIndex].c_str(),
					promptObj->prompt[item.originalIndex].size(),
					fg);
			if (textSurface == nullptr) {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
					"Failed to render prompt text surface: %s",
					SDL_GetError());
				SDL_DestroyTexture(shadowTexture);
				SDL_DestroySurface(shadowSurface);
				continue;
			}

			SDL_Texture* textTexture =
				SDL_CreateTextureFromSurface(render, textSurface);
			if (textTexture == nullptr) {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
					"Failed to create prompt text texture: %s",
					SDL_GetError());
				SDL_DestroySurface(textSurface);
				SDL_DestroyTexture(shadowTexture);
				SDL_DestroySurface(shadowSurface);
				continue;
			}

			SDL_SetTextureBlendMode(shadowTexture, SDL_BLENDMODE_BLEND);

			SDL_FRect shadowRect = {
				static_cast<float>(item.rect.x + paddingX + 3),
				static_cast<float>(item.rect.y + paddingY + 3),
				static_cast<float>(shadowSurface->w),
				static_cast<float>(shadowSurface->h)
			};

			SDL_FRect textRect = {
				static_cast<float>(item.rect.x + paddingX),
				static_cast<float>(item.rect.y + paddingY),
				static_cast<float>(textSurface->w),
				static_cast<float>(textSurface->h)
			};

			SDL_RenderTexture(render, shadowTexture, nullptr, &shadowRect);
			SDL_RenderTexture(render, textTexture, nullptr, &textRect);

			SDL_DestroyTexture(textTexture);
			SDL_DestroySurface(textSurface);
			SDL_DestroyTexture(shadowTexture);
			SDL_DestroySurface(shadowSurface);
		}

		return KOTONOHA_SCENE_DRAW_OVERLAYED;
	}

	Prompt::~Prompt() {
		if (font != nullptr) {
			TTF_CloseFont(font);
			font = nullptr;
		}
	}

} // namespace Kotonoha