# School Days HQ — Launcher Interface Language

The Windows and Android **School Days HQ** launchers support five interface
languages: `en`, `pt-BR`, `es`, `ru`, and `zh-CN`. The launcher-language
control is directly below the launcher-music control on the initial screen.
Choices are displayed by their native names, regardless of current UI locale.

**Launcher language does not select a game translation**. Game language and
GPK/JSON backend selection, optional L×H availability/preference, save files,
the external SAF folder, and forced-subtitle locale remain independent.

## Persistence and defaults

- **Windows:** `launcher-language.txt` lives beside the launcher settings
  file. `SchoolDaysLauncherLocale::ReadPreference/WritePreference` handle it
  without editing `language.txt`, `translation-pack.txt`, or `Kotonoha.ini`.
- **Android:** the `SchoolDaysSetup` Activity preferences store only the
  launcher presentation choice under `launcherUiLanguage`. The Activity
  applies that choice through its localized Context and recreates its own UI.
  Gameplay language remains in the existing `languageSelection` setting.
- **Existing installations:** when no explicit UI-language preference exists,
  retain the previous English launcher interface.
- **Fresh installations:** adopt a supported system language, or English when
  the device/desktop language is not among the five supported values.

## Localization sources and validation

- Windows/ImGui: `games/school-days/src/SchoolDaysLauncherLocale.cpp`, which
  maintains a complete table of localized UI keys.
- Android: `buildsystems/android/app/src/schoolDays/res/values*/strings.xml`.
- Run `SchoolDays.LauncherLocale` through CTest for the Windows-facing
  interface locale table and persistence. Run
  `python tools/android/test_school_days_launcher_strings.py` to check
  Android localization key/placeholder parity.

Translations are interface strings, not the game's dialogue translations.
Any new option requires a localized entry in all five languages.
The approved `forced_subtitles` v4 assets are not changed by this feature.

**Release caution:** successful compilation and string coverage alone do not
establish visual QA approval. Verify font fallback, text clipping, persistence,
and game-language independence on actual Windows and Android launchers before
replacing any approved v1.00 artifact.
