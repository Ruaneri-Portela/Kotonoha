# Launcher interface language

**English (US)** | [Português (Brasil)](LAUNCHER_INTERFACE_LANGUAGE.pt-BR.md)

The shared desktop launcher now has a presentation-only **Interface language**
selector for English, Brazilian Portuguese, Spanish, Russian and Simplified
Chinese (`en`, `pt-BR`, `es`, `ru`, `zh-CN`). The selector appears above the
Play button, and the language choices always display their native names.

This is deliberately separate from the **Translation** tab: changing the
launcher language does not change `settings.json`, the game translation
dictionary, translation packs, save data, renderer settings, or game paths.
The engine and the game continue using the same runtime settings as before.

The choice is stored next to the launcher settings as
`launcher-language.txt`. Existing installs without this file retain the
previous English interface; new installs use a supported system language, or
English for unsupported locales. `zh-TW` and `zh-Hant` never automatically
select Simplified Chinese. Invalid saved values fall back to English.

Shared launcher labels and the standard School Days folder/route field labels
are localized; game-specific hints, dynamic diagnostics, technical option
names and some optional ImGui panels retain their English source labels.
The default UI font (`NotoSansJP-Regular.ttf`) supports CJK text; users with
custom fonts should choose a font containing the selected script.

To validate the independent preference, language parsing and text catalog,
run `ctest -R '^Kotonoha.LauncherLocale$'` after building tests. No commercial
game data is required.

On Android, the **universal** Kotonoha launcher (not a game-specific setup
screen) offers the same five languages via a dropdown. It stores the chosen
interface language in the private `kotonoha_launcher_ui` preferences under
`interface_language`, independently from the game's locale, SAF permissions,
and saves. Changing the selection recreates the launcher activity without
resetting game options. The default follows a supported device language;
Traditional Chinese device locales fall back to English. Validate Android
strings and resource references with
`python buildsystems/android/tools/test_launcher_locales.py -v`.
