#pragma once

// Private header shared by the debug UI translation units (src/debug/**).
// The public entry points live in <Kotonoha/debug/DebugUi.hpp>.

#include <Kotonoha/Kotonoha.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstddef>
#include <string>
#include <vector>

namespace Kotonoha::Debug {

/* ---------------- Shared state ---------------- */

struct TextEditorEntry {
  bool isSelection = false;
  size_t lineIndex = 0;
  std::string startTime;
  std::string endTime;
  std::string prefix;
  std::string suffix;
  std::string character;
  std::string text;
  std::vector<std::string> options;
};

// Window visibility.
inline bool scriptEditorVisible = false;
inline bool textEditorVisible = false;
inline bool settingsVisible = false;
inline bool aboutVisible = false;
inline bool playlistVisible = false;
inline bool logVisible = false;
inline bool sceneBuffersVisible = false;
inline bool errorPopupVisible = false;

// True while any debug UI window owns the focus (game input is suppressed).
inline bool anyWindowFocused = false;

// Input buffers.
inline char openBuf[4096] = {0};
inline char appendBuf[4096] = {0};

// Script editor.
inline int selectedSceneIndex = -1;
inline int editorSceneIndex = -1;
inline bool editorDirty = false;
inline std::string sceneEditorText;
inline char editorStatusBuf[512] = {0};

// Text editor model (parsed from the script text).
inline std::vector<std::string> textEditorLines;
inline std::vector<TextEditorEntry> textEditorEntries;
inline std::string textEditorSyncedScript;
inline bool textEditorFinalNewline = false;
inline bool textEditorDirty = false;

// Temporary gameplay used for "preview" runs of the edited script.
inline std::string originalScriptPathForTemporary;
inline std::string temporaryScriptPath;
inline int temporaryGameplayIndex = -1;
inline bool temporaryGameplayActive = false;

/* ---------------- Utilities ---------------- */

void SetEditorStatus(const char *text);
bool IsValidGameplayIndex(::Kotonoha::Kotonoha *game, int index);
::Kotonoha::Gameplay *GetGameplayAt(::Kotonoha::Kotonoha *game, int index);
std::vector<std::string> SplitTabs(const std::string &text);
std::string JoinLines(const std::vector<std::string> &lines, bool finalNewline);
std::string FormatTime(Uint64 milliseconds);
std::string FormatScriptTime(const std::string &timestamp);
bool LoadTextFile(const std::string &path, std::string &outText);
bool SaveTextFile(const std::string &path, const std::string &text);
std::string GetTemporaryScenePath(const std::string &path);

/* ---------------- Text editor model ---------------- */

void LoadTextEditor(const std::string &source);
void SyncTextEditorToScript();

/* ---------------- Scene management ---------------- */

bool LoadSceneEditorFromDisk(::Kotonoha::Gameplay *play);
bool OpenPaths(::Kotonoha::Kotonoha *game, const char *buffer, bool append);
bool SelectScene(::Kotonoha::Kotonoha *game, int index);
void TryAutoLoadCurrentScene(::Kotonoha::Kotonoha *game,
                             Kotonoha_Game &context);
void GoToScene(::Kotonoha::Kotonoha *game, Kotonoha_Game &context, int index);
bool RecreateGameplayAtPath(::Kotonoha::Kotonoha *game, Kotonoha_Game &context,
                            int index, const std::string &newPath);
bool SaveAndReloadReplace(::Kotonoha::Kotonoha *game, Kotonoha_Game &context,
                          int index);
bool LoadTemporaryGameplayAt(::Kotonoha::Kotonoha *game,
                             Kotonoha_Game &context, int index);
bool RestoreOriginalPath(::Kotonoha::Kotonoha *game, Kotonoha_Game &context);

/* ---------------- Windows ---------------- */

void DrawAboutFields();
void DrawAboutWindow();
void DrawSettingsFields(Kotonoha_Config *cfg, Kotonoha_Game &context);
void DrawSettingsWindow(Kotonoha_Game &context);
void DrawAudio(Kotonoha_Game &context);
void DrawControlPanel(::Kotonoha::Kotonoha *game, ::Kotonoha::Gameplay *play,
                      Kotonoha_Game &context);
void DrawOpenScenesWindow(::Kotonoha::Kotonoha *game, Kotonoha_Game &context,
                          bool &shouldExit);
void DrawPlaylistWindow(::Kotonoha::Kotonoha *game, Kotonoha_Game &context);
void DrawSceneBuffersWindow(::Kotonoha::Gameplay *play);
bool DrawScriptEditorWindow(::Kotonoha::Kotonoha *game, Kotonoha_Game &context);
bool DrawTextEditorWindow(::Kotonoha::Kotonoha *game, Kotonoha_Game &context);

} // namespace Kotonoha::Debug
