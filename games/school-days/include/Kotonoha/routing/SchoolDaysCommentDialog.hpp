#pragma once

#include <string>

struct SDL_Window;

namespace Kotonoha {

class SchoolDaysCommentDialog {
public:
    enum class Result {
        Confirmed,
        Cancelled,
        Unsupported,
        Failed,
    };

    static Result Run(SDL_Window* owner, bool useEnglish,
                      const std::u16string& initial,
                      std::u16string* output,
                      std::string* error = nullptr);
};

} // namespace Kotonoha
