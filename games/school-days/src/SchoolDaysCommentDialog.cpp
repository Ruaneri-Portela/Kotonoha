#include <Kotonoha/routing/SchoolDaysCommentDialog.hpp>
#include <Kotonoha/routing/SchoolDaysFontData.hpp>
#include <Kotonoha/routing/SchoolDaysSaveLoadRenderPlan.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <array>
#include <cstdio>
#include <map>
#include <memory>

namespace Kotonoha {

namespace {

std::u16string ToUtf16(const std::string& text) {
    std::u16string out;
    const char* cursor = text.c_str();
    size_t left = text.size();
    while (left > 0) {
        uint32_t cp = SDL_StepUTF8(&cursor, &left);
        if (cp == 0) break;
        if (cp >= 0x10000u) {
            cp -= 0x10000u;
            out.push_back(static_cast<char16_t>(0xd800u + (cp >> 10u)));
            out.push_back(static_cast<char16_t>(0xdc00u + (cp & 0x3ffu)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

void PopLastCodePoint(std::u16string* text) {
    if (text->empty()) return;
    const char16_t last = text->back();
    text->pop_back();
    if (last >= 0xdc00u && last < 0xe000u && !text->empty()) text->pop_back();
}

// SDL's built-in debug font is ASCII-only; other code points are shown as '?'.
std::string ToDisplayAscii(const std::u16string& text) {
    std::string out;
    for (char16_t c : text) {
        if (c >= 0xdc00u && c < 0xe000u) continue;
        out.push_back(c >= 0x20u && c < 0x7fu ? static_cast<char>(c) : '?');
    }
    return out;
}

struct TextureDeleter {
    void operator()(SDL_Texture* texture) const { SDL_DestroyTexture(texture); }
};
using TexturePtr = std::unique_ptr<SDL_Texture, TextureDeleter>;

// Draws text with the glyph atlas shipped in the game's FONTDATA, scaled so
// 48px glyphs become `height` render pixels. Returns the drawn width.
class GlyphPainter {
public:
    GlyphPainter(SDL_Renderer* renderer, const SchoolDaysFontData* font,
                 bool english)
        : renderer_(renderer), font_(font), english_(english) {}

    float Draw(const std::u16string& text, float x, float y, float height,
               bool measureOnly = false) {
        const float scale = height / static_cast<float>(SchoolDaysFontData::kGlyphHeight);
        float cursor = x;
        for (char16_t c : text) {
            if (c >= 0xd800u && c < 0xe000u) continue;
            const uint16_t code = static_cast<uint16_t>(c);
            if (!measureOnly) {
                if (SDL_Texture* texture = Glyph(code)) {
                    const SDL_FRect dst{cursor, y, SchoolDaysFontData::kGlyphWidth * scale,
                                        height};
                    SDL_RenderTexture(renderer_, texture, nullptr, &dst);
                }
            }
            cursor += static_cast<float>(
                          SchoolDaysSaveLoadRenderPlan::GlyphAdvance(code, english_)) * scale;
        }
        return cursor - x;
    }

private:
    SDL_Texture* Glyph(uint16_t code) {
        auto found = cache_.find(code);
        if (found != cache_.end()) return found->second.get();
        TexturePtr texture;
        SchoolDaysFontData::Glyph glyph;
        if (font_ != nullptr && font_->HasGlyph(code) &&
            font_->DecodeGlyph(code, &glyph)) {
            std::array<uint32_t, SchoolDaysFontData::kGlyphPixels> pixels{};
            for (int i = 0; i < SchoolDaysFontData::kGlyphPixels; ++i) {
                const uint32_t g = glyph.gray[static_cast<size_t>(i)];
                pixels[static_cast<size_t>(i)] =
                    (static_cast<uint32_t>(glyph.alpha[static_cast<size_t>(i)]) << 24u) |
                    (g << 16u) | (g << 8u) | g;
            }
            texture.reset(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STATIC,
                                            SchoolDaysFontData::kGlyphWidth,
                                            SchoolDaysFontData::kGlyphHeight));
            if (texture) {
                SDL_UpdateTexture(texture.get(), nullptr, pixels.data(),
                                  SchoolDaysFontData::kGlyphWidth * 4);
                SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);
            }
        }
        SDL_Texture* raw = texture.get();
        cache_.emplace(code, std::move(texture));
        return raw;
    }

    SDL_Renderer* renderer_;
    const SchoolDaysFontData* font_;
    bool english_;
    std::map<uint16_t, TexturePtr> cache_;
};

} // namespace

SchoolDaysCommentDialog::Result SchoolDaysCommentDialog::Run(
    SDL_Window* owner, bool useEnglish, const std::u16string& initial,
    std::u16string* output, const SchoolDaysFontData* font,
    std::string* error) {
    SDL_Renderer* renderer = owner != nullptr ? SDL_GetRenderer(owner) : nullptr;
    if (renderer == nullptr || output == nullptr) {
        if (error != nullptr) *error = "comment dialog needs an SDL renderer";
        return Result::Failed;
    }

    const size_t limit = useEnglish ? 120u : 60u;
    std::u16string text = initial.substr(0, limit);

    // Keep the frame behind the dialog; the backbuffer is not preserved.
    SDL_Texture* backdrop = nullptr;
    if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
        backdrop = SDL_CreateTextureFromSurface(renderer, shot);
        SDL_DestroySurface(shot);
    }

    // Work in physical pixels so the dialog follows the display DPI.
    SDL_RendererLogicalPresentation presentation = SDL_LOGICAL_PRESENTATION_DISABLED;
    int logicalW = 0, logicalH = 0;
    SDL_GetRenderLogicalPresentation(renderer, &logicalW, &logicalH, &presentation);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    float dpi = SDL_GetWindowDisplayScale(owner);
    if (dpi <= 0.0f) dpi = SDL_GetWindowPixelDensity(owner);
    if (dpi <= 0.0f) dpi = 1.0f;
    GlyphPainter painter(renderer, font, useEnglish);
    const bool hasFont = font != nullptr && font->IsLoaded();

    const char* title = useEnglish ? "Insert comments (120 characters or less)"
                                   : "Insert comments (60 characters or less)";
    SDL_StartTextInput(owner);

    Result result = Result::Cancelled;
    bool done = false;
    SDL_FRect okButton{}, cancelButton{};
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                SDL_PushEvent(&event);
                result = Result::Cancelled;
                done = true;
                break;
            case SDL_EVENT_TEXT_INPUT:
                if (event.text.text != nullptr) {
                    const std::u16string add = ToUtf16(event.text.text);
                    if (text.size() + add.size() <= limit) text += add;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                    result = Result::Confirmed;
                    done = true;
                } else if (event.key.key == SDLK_ESCAPE) {
                    result = Result::Cancelled;
                    done = true;
                } else if (event.key.key == SDLK_BACKSPACE) {
                    PopLastCodePoint(&text);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                SDL_FPoint point;
                SDL_ConvertEventToRenderCoordinates(renderer, &event);
                point = {event.button.x, event.button.y};
                if (SDL_PointInRectFloat(&point, &okButton)) {
                    result = Result::Confirmed;
                    done = true;
                } else if (SDL_PointInRectFloat(&point, &cancelButton)) {
                    result = Result::Cancelled;
                    done = true;
                }
                break;
            }
            default:
                break;
            }
        }
        if (done) break;

        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
        const float width = static_cast<float>(w), height = static_cast<float>(h);
        const float boxW = std::min(width * 0.94f, 900.0f * dpi), boxH = 170.0f * dpi;
        const SDL_FRect box{(width - boxW) / 2, (height - boxH) / 2, boxW, boxH};
        const float line = 26.0f * dpi;
        okButton = {box.x + boxW - 230 * dpi, box.y + boxH - 50 * dpi, 100 * dpi, 38 * dpi};
        cancelButton = {box.x + boxW - 120 * dpi, box.y + boxH - 50 * dpi, 110 * dpi, 38 * dpi};

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (backdrop != nullptr) SDL_RenderTexture(renderer, backdrop, nullptr, nullptr);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
        SDL_RenderFillRect(renderer, nullptr);
        SDL_SetRenderDrawColor(renderer, 24, 26, 40, 245);
        SDL_RenderFillRect(renderer, &box);
        SDL_SetRenderDrawColor(renderer, 200, 210, 235, 255);
        SDL_RenderRect(renderer, &box);

        const SDL_FRect field{box.x + 14 * dpi, box.y + 50 * dpi, boxW - 28 * dpi, 38 * dpi};
        SDL_SetRenderDrawColor(renderer, 8, 9, 18, 255);
        SDL_RenderFillRect(renderer, &field);
        SDL_SetRenderDrawColor(renderer, 200, 210, 235, 255);
        SDL_RenderRect(renderer, &field);

        SDL_SetRenderDrawColor(renderer, 60, 80, 130, 255);
        SDL_RenderFillRect(renderer, &okButton);
        SDL_SetRenderDrawColor(renderer, 70, 70, 80, 255);
        SDL_RenderFillRect(renderer, &cancelButton);
        SDL_SetRenderDrawColor(renderer, 200, 210, 235, 255);
        SDL_RenderRect(renderer, &okButton);
        SDL_RenderRect(renderer, &cancelButton);

        std::u16string shown = text;
        if (((SDL_GetTicks() / 500u) & 1u) == 0u) shown.push_back(u'_');
        if (hasFont) {
            painter.Draw(ToUtf16(title), box.x + 14 * dpi, box.y + 12 * dpi, line);
            // Keep the tail of long text visible inside the field.
            const float maxWidth = field.w - 16 * dpi;
            std::u16string visible = shown;
            while (visible.size() > 1u &&
                   painter.Draw(visible, 0, 0, line, true) > maxWidth) {
                visible.erase(visible.begin());
            }
            painter.Draw(visible, field.x + 8 * dpi, field.y + 6 * dpi, line);
            const float okW = painter.Draw(u"OK", 0, 0, line, true);
            painter.Draw(u"OK", okButton.x + (okButton.w - okW) / 2,
                         okButton.y + 5 * dpi, line);
            const float cancelW = painter.Draw(u"Cancel", 0, 0, line, true);
            painter.Draw(u"Cancel", cancelButton.x + (cancelButton.w - cancelW) / 2,
                         cancelButton.y + 5 * dpi, line);
        } else {
            SDL_SetRenderScale(renderer, dpi, dpi);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDebugText(renderer, (box.x + 14 * dpi) / dpi, (box.y + 18 * dpi) / dpi, title);
            SDL_RenderDebugText(renderer, (field.x + 8 * dpi) / dpi, (field.y + 14 * dpi) / dpi,
                                ToDisplayAscii(shown).c_str());
            SDL_RenderDebugText(renderer, (okButton.x + 30 * dpi) / dpi, (okButton.y + 14 * dpi) / dpi, "OK");
            SDL_RenderDebugText(renderer, (cancelButton.x + 20 * dpi) / dpi, (cancelButton.y + 14 * dpi) / dpi, "Cancel");
            SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_StopTextInput(owner);
    SDL_SetRenderLogicalPresentation(renderer, logicalW, logicalH, presentation);
    if (backdrop != nullptr) SDL_DestroyTexture(backdrop);
    if (result == Result::Confirmed) *output = std::move(text);
    return result;
}

} // namespace Kotonoha
