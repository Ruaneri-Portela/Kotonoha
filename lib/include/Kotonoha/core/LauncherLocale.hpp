#pragma once

#include <string>
#include <string_view>

namespace Kotonoha::LauncherLocale {

// Presentation-only preference. Does not select the game's translation,
// rendering backend, save location, or any EngineSettings field.
enum class Language { English, PortugueseBrazil, Spanish, Russian, Chinese };

Language parse(std::string_view tag);
Language systemLanguage(std::string_view language, std::string_view country);
const char *tag(Language language);
const char *nativeName(Language language);

// Persist next to settings.json, never inside the game data or save file.
// Existing installations without a preference retain English for continuity;
// a new installation may use a supported system locale.
Language load(const std::string &settingsPath, Language newInstallDefault);
void save(const std::string &settingsPath, Language language);

// Returns the original English UI text if a key is not in the catalog.
// Returned pointers are static and remain valid through UI redraws.
const char *text(Language language, const char *english);

} // namespace Kotonoha::LauncherLocale
