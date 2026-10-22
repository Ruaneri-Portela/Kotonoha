#pragma once

#include <string>

#include <SDL3/SDL.h>

#include <Kotonoha/routing/SchoolDaysFontData.hpp>
#include <Kotonoha/routing/SchoolDaysBackLog.hpp>

namespace Kotonoha {

// SDL owner for the authenticated v1.02 BackLog skin.  Resource geometry stays
// in SchoolDaysBackLogRenderPlan; this class only resolves textures and applies
// the physical aspect-fit viewport.
class SchoolDaysBackLogPresenter {
public:
    SchoolDaysBackLogPresenter() = default;
    ~SchoolDaysBackLogPresenter();

    SchoolDaysBackLogPresenter(const SchoolDaysBackLogPresenter&) = delete;
    SchoolDaysBackLogPresenter& operator=(const SchoolDaysBackLogPresenter&) = delete;

    bool Open(SDL_Renderer* renderer, const std::string& assetsRoot,
              SchoolDaysBackLogModel::Orientation orientation,
              bool useEnglish, const std::string& fontDataLogicalPath,
              std::string* error = nullptr);
    void Close();

    bool Render(SDL_Renderer* renderer, const SchoolDaysBackLogModel& model,
                std::string* error = nullptr);
    bool IsOpen() const { return opened_; }

private:
    static bool Fail(std::string* error, const char* text);

    SchoolDaysBackLogModel::Orientation orientation_ =
        SchoolDaysBackLogModel::Orientation::Vertical;
    SDL_Texture* base_ = nullptr;
    SDL_Texture* chip_ = nullptr;
    SDL_Texture* text_ = nullptr;
    SchoolDaysFontData font_;
    std::vector<uint32_t> textPixels_;
    bool useEnglish_ = false;
    bool opened_ = false;
};

} // namespace Kotonoha
