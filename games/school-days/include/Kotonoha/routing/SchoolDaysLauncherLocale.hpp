#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Kotonoha::SchoolDaysLauncherLocale {

enum class Language : std::size_t {
  En = 0,
  PtBr = 1,
  Es = 2,
  Ru = 3,
  ZhCn = 4,
  Count = 5
};

enum class Key : std::size_t {
  WindowTitle = 0,
  MainPlay,
  MainOptions,
  MainQuit,
  MusicOn,
  MusicOff,
  LauncherLanguageLabel,
  LauncherLanguageTooltip,
  MissingArtwork,
  BannerSubtitle,
  TabDisplay,
  TabGraphics,
  TabAudio,
  TabGeneral,
  TabLanguage,
  TabGameFiles,
  TabAdvanced,
  BtnReset,
  BtnOk,
  BtnCancel,
  StatusResetStaged,

  // Display tab
  DisplaySeparator,
  DisplayAndroidNote,
  DisplayDeviceDefault,
  DisplayAndroidResolution,
  DisplayAndroidNoModes,
  DisplayAndroidRenderNote,
  DisplayDesktopNote,
  DisplayWindowMode,
  DisplayModeWindowed,
  DisplayModeBorderless,
  DisplayModeExclusive,
  DisplayMonitor,
  DisplayMonitorPrefix,
  DisplayMonitorUnavailable,
  DisplayMonitorGeneric,
  DisplayBorderlessWarning,
  DisplaySuffixDesktop,
  DisplaySuffixWindow,
  DisplayResolutionRefresh,
  DisplayNoModes,
  DisplayCustomWindowSize,
  DisplayVsync,
  DisplayResizableWindow,

  // Graphics tab
  GraphicsSeparator,
  GraphicsRenderScale,
  GraphicsRendererGles,
  GraphicsRenderer,
  GraphicsAutomatic,
  GraphicsUpscaler,
  GraphicsUpscalerAuto,
  GraphicsUpscalerNone,
  GraphicsUpscalerLinear,
  GraphicsUpscalerMpv,
  GraphicsUpscaleFactor,
  GraphicsUpscaleOffNote,
  GraphicsCustomGlslPreset,
  GraphicsShaderPreset,
  GraphicsShaderPresetNote,
  GraphicsNonGlWarning,
  GraphicsAutoRendererNote,
  GraphicsCustomGlslPaths,
  GraphicsCustomGlslFileHint,
  GraphicsFsrcnnxHint,
  GraphicsFolderNotShaderError,
  GraphicsCustomGlslInactiveNote,
  GraphicsAdditionalEngineUpscaling,

  // Audio tab
  AudioLauncherOnlySeparator,
  AudioPlayLauncherMusic,
  AudioLauncherMusicNote,
  AudioRetailSeparator,
  AudioNoConfigDat,
  AudioBgmVolume,
  AudioSeVolume,
  AudioVoiceVolume,
  AudioMaleVoices,
  AudioMuteGame,
  AudioMasterVolume,
  AudioMovieVolume,
  AudioMasterRetailNote,

  // General tab
  GeneralSeparator,
  GeneralNoConfigDat,
  GeneralShowText,
  GeneralSkipUnread,
  GeneralSuperSkip,
  GeneralAutoDraw,
  GeneralAutoMode,
  GeneralSwapAb,
  GeneralForcedSubtitles,
  GeneralOptionalExpansion,
  GeneralEnableLxh,
  GeneralEnableLxhDesc,
  GeneralLxhStatusPrefix,
  GeneralAspectAuto,
  GeneralAspect43,
  GeneralAspect169,
  GeneralNarrativeAspect,
  GeneralNarrativeAspectNote,
  GeneralRetailDefaultsBtn,

  // LxH dynamic status strings
  LxhStatusActive,
  LxhStatusDisabled,
  LxhStatusMissingPack,
  LxhStatusMissingTranslation,
  LxhStatusNoFolder,

  // Language tab (Game translation)
  LangSeparator,
  LangSelectPrompt,
  LangComboLabel,
  LangFormatLabel,
  LangSpanishGpkMissing,
  LangChineseGpkMissing,
  LangRussianGpkMissing,
  LangEnglishHqNote,
  LangBaseHqGpk,
  LangLxhEndingsGpk,
  LangMissingHqScript,
  LangMissingOptionalLxhGpk,
  LangRetailOnlyNote,
  LangOriginalPacksUntouched,
  LangLxhStatusLabel,
  LangReadFromScriptsNote,
  LangEnglishDialogueNote,
  LangBaseHqJson,
  LangJsonValidationNote,
  LangBaseJsonMissing,
  LangInstalledJsonOnlyNote,
  LangLxhJsonSeparator,
  LangLxhJsonLabel,
  LangLxhAutoMatched,
  LangLxhManualOverride,
  LangNotFound,
  LangChooseLxhJsonBtn,
  LangUseAutoLxhBtn,
  LangLxhJsonFooterNote,

  // Game Files & Advanced tabs
  FilesAndroidSeparator,
  FilesAndroidDesc,
  FilesAndroidPacks,
  FilesAndroidRoute,
  FilesAndroidImportHint,
  FilesAndroidUriHint,
  FilesDistSeparator,
  FilesDistDesc,
  FilesDistImportant,
  FilesFolderLabel,
  FilesFolderHint,
  FilesBrowseFolderBtn,
  FilesBrowseIsoBtn,
  FilesBrowseFileBtn,
  FilesRouteLabel,
  FilesRouteHint,
  AdvancedDataSeparator,
  AdvancedOverlaySeparator,
  AdvancedOverlayFolder,
  AdvancedChooseOverlayBtn,
  AdvancedAutoOverlayBtn,
  AdvancedEngineSettings,

  // Validation / status errors
  ErrCustomGlslFolder,
  ErrCustomGlslNotFound,
  ErrCustomGlslInvalid,
  ErrMpvRequiresOpenGl,
  ErrGpkNeedsSettingsPath,
  ErrGpkCannotCombineJson,
  ErrBaseGpkIncompatible,
  ErrNonEnglishMissingJson,
  ErrManualLxhMissing,
  ErrJsonNoTranslations,
  GeneralDiscordPresence,

  Count
};

constexpr std::size_t kLanguageCount = static_cast<std::size_t>(Language::Count);
constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);
constexpr const char *kPreferenceFilename = "launcher-language.txt";

constexpr std::array<Language, kLanguageCount> AllLanguages() {
  return {Language::En, Language::PtBr, Language::Es, Language::Ru,
          Language::ZhCn};
}

const char *Code(Language language);
const char *NativeName(Language language);
std::optional<Language> ParseCode(std::string_view raw);

// Preserves the previous default ("en") for existing installations that do not
// have launcher-language.txt yet; detects the system locale for fresh installs
// when it matches one of the 5 supported languages.
Language ResolveDefault(bool existingInstallation,
                        std::string_view systemLocaleTag);

std::filesystem::path PreferencePath(const std::string &settingsPath);
std::optional<Language> ReadPreference(const std::string &settingsPath);
bool WritePreference(const std::string &settingsPath, Language language);

const char *Text(Language language, Key key);
bool ValidateCoverage(std::string *firstMissing = nullptr);

} // namespace Kotonoha::SchoolDaysLauncherLocale
