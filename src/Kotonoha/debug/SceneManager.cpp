#include <Kotonoha/debug/DebugInternal.hpp>
#include <sstream>
#include <utility>

namespace Kotonoha::Debug {

bool LoadSceneEditorFromDisk(::Kotonoha::Gameplay *play) {
  if (play == nullptr || play->scriptPath.empty()) {
    SetEditorStatus("Invalid gameplay script path.");
    return false;
  }

  std::string loadedText;
  if (!LoadTextFile(play->scriptPath, loadedText)) {
    sceneEditorText.clear();
    LoadTextEditor(sceneEditorText);
    editorSourcePath = play->scriptPath;
    editorDirty = false;
    textEditorDirty = false;
    return false;
  }

  sceneEditorText = std::move(loadedText);
  LoadTextEditor(sceneEditorText);
  editorDirty = false;
  textEditorDirty = false;
  editorSourcePath = play->scriptPath;
  SetEditorStatus("Editor loaded from disk.");
  return true;
}

bool OpenPaths(::Kotonoha::Kotonoha *game, const char *buffer, bool append) {
  if (game == nullptr || buffer == nullptr)
    return false;

  bool ok = true;
  bool loadedAny = false;
  std::stringstream ss(buffer);
  std::string part;

  if (!append) {
    game->ClearGameplays();
  }

  while (std::getline(ss, part, ';')) {
    if (part.empty())
      continue;

    if (!game->LoadScriptFile(part.c_str())) {
      ok = false;
    } else {
      loadedAny = true;
    }
  }

  if (!loadedAny && !append) {
    game->ClearGameplays();
  }

  return ok && loadedAny;
}

bool SelectScene(::Kotonoha::Kotonoha *game, int index) {
  ::Kotonoha::Gameplay *play = GetGameplayAt(game, index);
  if (play == nullptr) {
    SetEditorStatus("Invalid scene selection.");
    return false;
  }

  std::string loadedText;
  if (!LoadTextFile(play->scriptPath, loadedText)) {
    return false;
  }

  selectedSceneIndex = index;
  editorSceneIndex = index;
  sceneEditorText = std::move(loadedText);
  LoadTextEditor(sceneEditorText);
  editorDirty = false;
  textEditorDirty = false;
  editorSourcePath = play->scriptPath;
  SetEditorStatus("Editor loaded from disk.");
  return true;
}

void TryAutoLoadCurrentScene(::Kotonoha::Kotonoha *game,
                             Kotonoha_Game &context) {
  if (game == nullptr || context.config.scene >= game->gameplays.size()) {
    return;
  }

  if (editorSceneIndex >= 0 &&
      editorSceneIndex < static_cast<int>(game->gameplays.size())) {
    return;
  }

  SelectScene(game, static_cast<int>(context.config.scene));
}

void GoToScene(::Kotonoha::Kotonoha *game, Kotonoha_Game &context, int index) {
  if (!IsValidGameplayIndex(game, index))
    return;

  ::Kotonoha::Gameplay *item = game->gameplays[index];
  if (item == nullptr)
    return;

  if (context.config.scene == static_cast<size_t>(index)) {
    item->Reset(true);
    return;
  }

  const size_t older = context.config.scene;
  context.config.scene = index;
  game->gameplays[context.config.scene]->Reset(true);

  if (older < game->gameplays.size() && game->gameplays[older] != nullptr) {
    game->gameplays[older]->Pause();
  }
}

bool RecreateGameplayAtPath(::Kotonoha::Kotonoha *game, Kotonoha_Game &context,
                            int index, const std::string &newPath) {
  if (game == nullptr || !IsValidGameplayIndex(game, index) ||
      newPath.empty()) {
    SetEditorStatus("Invalid recreation parameters.");
    return false;
  }

  ::Kotonoha::Gameplay *oldPlay = game->gameplays[index];
  if (oldPlay == nullptr)
    return false;

  ::Kotonoha::Gameplay *newPlay = nullptr;

  try {
    TTF_Font *preferredFont = nullptr;
    auto it = game->fonts.find("ConcertOne-Regular");
    if (it != game->fonts.end()) {
      preferredFont = it->second;
    }
    newPlay = game->CreateGameplay(newPath.c_str(), preferredFont);
    if (newPlay == nullptr) {
      SetEditorStatus("Gameplay factory rejected scene recreation.");
      return false;
    }
  } catch (const std::exception &e) {
    SetEditorStatus(e.what());
    return false;
  } catch (...) {
    SetEditorStatus("Failed to recreate gameplay.");
    return false;
  }

  newPlay->SetTime(oldPlay->GetTime());
  game->gameplays[index] = newPlay;
  game->DeleteGameplay(oldPlay);

  if (selectedSceneIndex == index || editorSceneIndex == index) {
    selectedSceneIndex = index;
    editorSceneIndex = index;
    sceneEditorText.clear();
    editorDirty = false;
    LoadSceneEditorFromDisk(newPlay);
  }

  return true;
}

bool SaveAndReloadReplace(::Kotonoha::Kotonoha *game, Kotonoha_Game &context,
                          int index) {
  ::Kotonoha::Gameplay *selected = GetGameplayAt(game, index);
  if (selected == nullptr)
    return false;
  if (IsReadOnlyAssetPath(selected->scriptPath)) {
    SetEditorStatus("Scene scripts inside a mounted archive are read-only.");
    return false;
  }

  const std::string targetPath =
      (temporaryGameplayActive && temporaryGameplayIndex == index)
          ? originalScriptPathForTemporary
          : selected->scriptPath;

  if (targetPath.empty() || !SaveTextFile(targetPath, sceneEditorText)) {
    return false;
  }

  editorDirty = false;
  temporaryGameplayActive = false;
  temporaryGameplayIndex = -1;
  temporaryScriptPath.clear();
  originalScriptPathForTemporary.clear();

  return RecreateGameplayAtPath(game, context, index, targetPath);
}

bool LoadTemporaryGameplayAt(::Kotonoha::Kotonoha *game, Kotonoha_Game &context,
                             int index) {
  if (game == nullptr || !IsValidGameplayIndex(game, index))
    return false;

  ::Kotonoha::Gameplay *selected = game->gameplays[index];
  if (selected == nullptr)
    return false;
  if (IsReadOnlyAssetPath(selected->scriptPath)) {
    SetEditorStatus("Scene scripts inside a mounted archive are read-only.");
    return false;
  }

  const std::string baseForTempPath =
      (temporaryGameplayActive && temporaryGameplayIndex == index)
          ? originalScriptPathForTemporary
          : selected->scriptPath;

  std::string tempPath = GetTemporaryScenePath(baseForTempPath);
  if (tempPath.empty() || !SaveTextFile(tempPath, sceneEditorText)) {
    return false;
  }

  if (!temporaryGameplayActive) {
    originalScriptPathForTemporary = selected->scriptPath;
  }

  temporaryScriptPath = tempPath;
  temporaryGameplayIndex = index;
  temporaryGameplayActive = true;
  editorDirty = false;

  return RecreateGameplayAtPath(game, context, index, tempPath);
}

bool RestoreOriginalPath(::Kotonoha::Kotonoha *game, Kotonoha_Game &context) {
  if (!temporaryGameplayActive ||
      !IsValidGameplayIndex(game, temporaryGameplayIndex)) {
    return false;
  }

  const int index = temporaryGameplayIndex;
  const std::string originalPath = originalScriptPathForTemporary;
  if (IsReadOnlyAssetPath(originalPath)) {
    SetEditorStatus("Scene scripts inside a mounted archive are read-only.");
    return false;
  }

  temporaryGameplayActive = false;
  temporaryGameplayIndex = -1;
  temporaryScriptPath.clear();
  originalScriptPathForTemporary.clear();

  return RecreateGameplayAtPath(game, context, index, originalPath);
}

} // namespace Kotonoha::Debug
