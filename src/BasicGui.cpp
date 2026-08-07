#include <cstddef>
#include <exception>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_stdlib.h>
#include <Kotonoha/Kotonoha.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_iostream.h>
#include <sstream>
#include <string>

bool Kotonoha_BasicGuiShow = false;
bool Kotonoha_BasicGuiEditorShow = false;
bool showError = false;

static char openBuf[4096] = { 0 };
static char appendBuf[4096] = { 0 };
static bool show_playlist = false;

static int selectedSceneIndex = -1;
static int editorSceneIndex = -1;
static bool editorDirty = false;

static std::string sceneEditorText;
static char editorStatusBuf[512] = { 0 };

static std::string originalScriptPathForTemporary;
static std::string temporaryScriptPath;
static int temporaryGameplayIndex = -1;
static bool temporaryGameplayActive = false;

static void Kotonoha_SetEditorStatus(const char* text) {
	SDL_snprintf(editorStatusBuf, sizeof(editorStatusBuf), "%s", text ? text : "");
}

static bool Kotonoha_IsValidGameplayIndex(Kotonoha::Kotonoha* game, int index) {
	return game != nullptr && index >= 0 && index < (int)game->gameplays.size();
}

static Kotonoha::Gameplay* Kotonoha_GetGameplayAt(Kotonoha::Kotonoha* game, int index) {
	if (!Kotonoha_IsValidGameplayIndex(game, index)) {
		return nullptr;
	}
	return game->gameplays[index];
}

static std::string Kotonoha_JoinPath(const std::string& dir, const std::string& name) {
	if (dir.empty()) {
		return name;
	}

	const char last = dir.back();
	if (last == '/' || last == '\\') {
		return dir + name;
	}

#ifdef _WIN32
	return dir + "\\" + name;
#else
	return dir + "/" + name;
#endif
}

static bool Kotonoha_FinishFrameAndReturn(Kotonoha_Game& context, ImGuiIO& io) {
	ImGui::Render();
	SDL_SetRenderScale(context.render, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
	return true;
}

void Kotonoha_BasicGuiInit(Kotonoha_Game& gameContext) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	ImGui::StyleColorsDark();
	ImGui_ImplSDL3_InitForSDLRenderer(gameContext.window, gameContext.render);
	ImGui_ImplSDLRenderer3_Init(gameContext.render);
}

static std::string Kotonoha_GetTemporaryScenePath(const std::string& path) {
	if (path.empty()) {
		return "";
	}

	char* prefPath = SDL_GetPrefPath("Kotonoha", "Engine");
	if (prefPath == nullptr) {
		Kotonoha_SetEditorStatus(SDL_GetError());
		return "";
	}

	std::string baseDir(prefPath);
	SDL_free(prefPath);

	std::string fileName = path;
	const size_t slashPos = fileName.find_last_of("/\\");
	if (slashPos != std::string::npos) {
		fileName = fileName.substr(slashPos + 1);
	}

	const std::string keepFileName = fileName + ".tmp.ks";

	struct TempCleanupData {
		std::string directory;
		std::string keepFileName;
	};

	TempCleanupData cleanupData{ baseDir, keepFileName };

	const bool enumOk = SDL_EnumerateDirectory(
		baseDir.c_str(),
		[](void* userdata, const char* dirname, const char* fname) -> SDL_EnumerationResult {
			if (userdata == nullptr || dirname == nullptr || fname == nullptr) {
				return SDL_ENUM_CONTINUE;
			}

			TempCleanupData* data = static_cast<TempCleanupData*>(userdata);
			const std::string currentName = fname;

			if (currentName == "." || currentName == "..") {
				return SDL_ENUM_CONTINUE;
			}

			if (currentName == data->keepFileName) {
				return SDL_ENUM_CONTINUE;
			}

			if (currentName.size() < 7 || currentName.substr(currentName.size() - 7) != ".tmp.ks") {
				return SDL_ENUM_CONTINUE;
			}

			const std::string fullPath = Kotonoha_JoinPath(dirname, currentName);
			SDL_RemovePath(fullPath.c_str());
			return SDL_ENUM_CONTINUE;
		},
		&cleanupData);

	if (!enumOk) {
		Kotonoha_SetEditorStatus(SDL_GetError());
	}

	return Kotonoha_JoinPath(baseDir, keepFileName);
}

static bool Kotonoha_LoadTextFile(const std::string& path, std::string& outText) {
	if (path.empty()) {
		Kotonoha_SetEditorStatus("Empty path.");
		return false;
	}

	size_t dataSize = 0;
	void* data = SDL_LoadFile(path.c_str(), &dataSize);
	if (data == nullptr) {
		Kotonoha_SetEditorStatus(SDL_GetError());
		return false;
	}

	outText.assign(static_cast<const char*>(data), dataSize);
	SDL_free(data);
	return true;
}

static bool Kotonoha_SaveTextFile(const std::string& path, const std::string& text) {
	if (path.empty()) {
		Kotonoha_SetEditorStatus("Empty path.");
		return false;
	}

	SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "wb");
	if (io == nullptr) {
		Kotonoha_SetEditorStatus(SDL_GetError());
		return false;
	}

	const size_t written = SDL_WriteIO(io, text.data(), text.size());
	const bool closeOk = SDL_CloseIO(io);

	if (written != text.size()) {
		Kotonoha_SetEditorStatus("Failed to write full file.");
		return false;
	}

	if (!closeOk) {
		Kotonoha_SetEditorStatus(SDL_GetError());
		return false;
	}

	return true;
}

static bool Kotonoha_LoadSceneEditorFromDisk(Kotonoha::Gameplay* play) {
	if (play == nullptr) {
		Kotonoha_SetEditorStatus("Gameplay is null.");
		return false;
	}

	if (play->scriptPath.empty()) {
		Kotonoha_SetEditorStatus("Gameplay has no scriptPath.");
		return false;
	}

	std::string loadedText;
	if (!Kotonoha_LoadTextFile(play->scriptPath, loadedText)) {
		return false;
	}

	sceneEditorText = std::move(loadedText);
	editorDirty = false;
	Kotonoha_SetEditorStatus("Editor loaded from disk.");
	return true;
}

static bool Kotonoha_SaveSceneEditorToDisk(Kotonoha::Gameplay* play) {
	if (play == nullptr) {
		Kotonoha_SetEditorStatus("Gameplay is null.");
		return false;
	}

	if (play->scriptPath.empty()) {
		Kotonoha_SetEditorStatus("Gameplay has no scriptPath.");
		return false;
	}

	if (!Kotonoha_SaveTextFile(play->scriptPath, sceneEditorText)) {
		return false;
	}

	editorDirty = false;
	Kotonoha_SetEditorStatus("Saved to current file.");
	return true;
}

static bool Kotonoha_SaveSceneEditorToTemporary(Kotonoha::Gameplay* play, std::string& outTempPath) {
	if (play == nullptr) {
		Kotonoha_SetEditorStatus("Gameplay is null.");
		return false;
	}

	if (play->scriptPath.empty()) {
		Kotonoha_SetEditorStatus("Gameplay has no scriptPath.");
		return false;
	}

	outTempPath = Kotonoha_GetTemporaryScenePath(play->scriptPath);
	if (outTempPath.empty()) {
		Kotonoha_SetEditorStatus("Invalid temporary path.");
		return false;
	}

	if (!Kotonoha_SaveTextFile(outTempPath, sceneEditorText)) {
		return false;
	}

	editorDirty = false;
	Kotonoha_SetEditorStatus("Saved to temporary file.");
	return true;
}

static bool Kotonoha_OpenPaths(Kotonoha::Kotonoha* game, const char* buffer, bool append) {
	if (game == nullptr || buffer == nullptr) {
		return false;
	}

	bool ok = true;
	bool loadedAny = false;

	std::stringstream ss(buffer);
	std::string part;

	if (!append) {
		game->ClearGameplays();
	}

	while (std::getline(ss, part, ';')) {
		if (part.empty()) {
			continue;
		}

		if (!game->LoadScriptFile(part.c_str())) {
			ok = false;
		}
		else {
			loadedAny = true;
		}
	}

	if (!loadedAny && !append) {
		game->ClearGameplays();
	}

	return ok && loadedAny;
}

static bool Kotonoha_SelectScene(Kotonoha::Kotonoha* game, int index) {
	Kotonoha::Gameplay* play = Kotonoha_GetGameplayAt(game, index);
	if (play == nullptr) {
		Kotonoha_SetEditorStatus("Invalid scene selection.");
		return false;
	}

	std::string loadedText;
	if (!Kotonoha_LoadTextFile(play->scriptPath, loadedText)) {
		return false;
	}

	selectedSceneIndex = index;
	editorSceneIndex = index;
	sceneEditorText = std::move(loadedText);
	editorDirty = false;
	Kotonoha_SetEditorStatus("Editor loaded from disk.");
	return true;
}

static void Kotonoha_TryAutoLoadCurrentScene(Kotonoha::Kotonoha* game, Kotonoha_Game& context) {
	if (game == nullptr) {
		return;
	}

	if (context.scene >= game->gameplays.size()) {
		return;
	}

	if (editorSceneIndex >= 0 && editorSceneIndex < (int)game->gameplays.size()) {
		return;
	}

	Kotonoha_SelectScene(game, (int)context.scene);
}

static void Kotonoha_GoToScene(Kotonoha::Kotonoha* game, Kotonoha_Game& context, int index) {
	if (!Kotonoha_IsValidGameplayIndex(game, index)) {
		return;
	}

	Kotonoha::Gameplay* item = game->gameplays[index];
	if (item == nullptr) {
		return;
	}

	if (context.scene == (size_t)index) {
		item->Reset(true);
		return;
	}

	const size_t older = context.scene;
	context.scene = index;
	game->gameplays[context.scene]->Reset(true);

	if (older < game->gameplays.size() && game->gameplays[older] != nullptr) {
		game->gameplays[older]->Pause();
	}
}

static bool Kotonoha_RecreateGameplayAtPath(Kotonoha::Kotonoha* game,
	Kotonoha_Game& context,
	int index,
	const std::string& newPath) {
	if (game == nullptr) {
		Kotonoha_SetEditorStatus("Game is null.");
		return false;
	}

	if (!Kotonoha_IsValidGameplayIndex(game, index)) {
		Kotonoha_SetEditorStatus("Invalid gameplay index.");
		return false;
	}

	if (newPath.empty()) {
		Kotonoha_SetEditorStatus("Invalid new path.");
		return false;
	}

	Kotonoha::Gameplay* oldPlay = game->gameplays[index];
	if (oldPlay == nullptr) {
		Kotonoha_SetEditorStatus("Selected gameplay is null.");
		return false;
	}

	Kotonoha::Gameplay* newPlay = nullptr;

	try {
		newPlay = new Kotonoha::Gameplay(newPath.c_str(), &context);
	}
	catch (const std::exception& e) {
		Kotonoha_SetEditorStatus(e.what());
		return false;
	}
	catch (...) {
		Kotonoha_SetEditorStatus("Failed to recreate gameplay.");
		return false;
	}

	newPlay->SetTime(oldPlay->GetTime());
	game->gameplays[index] = newPlay;
	game->DeleteGameplay(oldPlay);

	if (context.scene == index) {
		context.scene = index;
	}

	if (selectedSceneIndex == index || editorSceneIndex == index) {
		selectedSceneIndex = index;
		editorSceneIndex = index;
		sceneEditorText.clear();
		editorDirty = false;
		Kotonoha_LoadSceneEditorFromDisk(newPlay);
	}

	return true;
}

static bool Kotonoha_SaveAndReloadReplace(Kotonoha::Kotonoha* game,
	Kotonoha_Game& context,
	int index) {
	Kotonoha::Gameplay* selected = Kotonoha_GetGameplayAt(game, index);
	if (selected == nullptr) {
		Kotonoha_SetEditorStatus("Selected gameplay is null.");
		return false;
	}

	const std::string targetPath =
		(temporaryGameplayActive && temporaryGameplayIndex == index)
		? originalScriptPathForTemporary
		: selected->scriptPath;

	if (targetPath.empty()) {
		Kotonoha_SetEditorStatus("No valid path to save.");
		return false;
	}

	if (!Kotonoha_SaveTextFile(targetPath, sceneEditorText)) {
		return false;
	}

	editorDirty = false;
	temporaryGameplayActive = false;
	temporaryGameplayIndex = -1;
	temporaryScriptPath.clear();
	originalScriptPathForTemporary.clear();

	if (!Kotonoha_RecreateGameplayAtPath(game, context, index, targetPath)) {
		return false;
	}

	Kotonoha_SetEditorStatus("Saved to original file and reloaded scene.");
	return true;
}

static bool Kotonoha_LoadTemporaryGameplayAt(Kotonoha::Kotonoha* game,
	Kotonoha_Game& context,
	int index) {
	if (game == nullptr) {
		Kotonoha_SetEditorStatus("Game is null.");
		return false;
	}

	if (!Kotonoha_IsValidGameplayIndex(game, index)) {
		Kotonoha_SetEditorStatus("Invalid gameplay index.");
		return false;
	}

	Kotonoha::Gameplay* selected = game->gameplays[index];
	if (selected == nullptr) {
		Kotonoha_SetEditorStatus("Selected gameplay is null.");
		return false;
	}

	const std::string baseForTempPath =
		(temporaryGameplayActive && temporaryGameplayIndex == index)
		? originalScriptPathForTemporary
		: selected->scriptPath;

	if (baseForTempPath.empty()) {
		Kotonoha_SetEditorStatus("Gameplay has no valid script path.");
		return false;
	}

	std::string tempPath = Kotonoha_GetTemporaryScenePath(baseForTempPath);
	if (tempPath.empty()) {
		Kotonoha_SetEditorStatus("Invalid temporary path.");
		return false;
	}

	if (!Kotonoha_SaveTextFile(tempPath, sceneEditorText)) {
		return false;
	}

	if (!temporaryGameplayActive) {
		originalScriptPathForTemporary = selected->scriptPath;
	}

	temporaryScriptPath = tempPath;
	temporaryGameplayIndex = index;
	temporaryGameplayActive = true;
	editorDirty = false;

	if (!Kotonoha_RecreateGameplayAtPath(game, context, index, tempPath)) {
		return false;
	}

	Kotonoha_SetEditorStatus("Temporary gameplay loaded from temporary file.");
	return true;
}

static bool Kotonoha_RestoreOriginalPath(Kotonoha::Kotonoha* game,
	Kotonoha_Game& context) {
	if (!temporaryGameplayActive) {
		Kotonoha_SetEditorStatus("No temporary gameplay active.");
		return false;
	}

	if (!Kotonoha_IsValidGameplayIndex(game, temporaryGameplayIndex)) {
		Kotonoha_SetEditorStatus("Invalid temporary gameplay index.");
		return false;
	}

	const int index = temporaryGameplayIndex;
	const std::string originalPath = originalScriptPathForTemporary;

	if (originalPath.empty()) {
		Kotonoha_SetEditorStatus("Original path not available.");
		return false;
	}

	temporaryGameplayActive = false;
	temporaryGameplayIndex = -1;
	temporaryScriptPath.clear();
	originalScriptPathForTemporary.clear();

	if (!Kotonoha_RecreateGameplayAtPath(game, context, index, originalPath)) {
		return false;
	}

	Kotonoha_SetEditorStatus("Original file restored in scene.");
	return true;
}

bool Kotonoha_BasicGuiRun(Kotonoha::Kotonoha* game,
	Kotonoha::Gameplay* play,
	Kotonoha_Game& context) {
	if (!Kotonoha_BasicGuiShow) {
		return true;
	}

	if (game == nullptr) {
		return true;
	}

	Kotonoha_TryAutoLoadCurrentScene(game, context);

	if (context.scene < game->gameplays.size()) {
		play = game->gameplays[context.scene];
	}
	else {
		play = nullptr;
	}

	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	ImGui_ImplSDLRenderer3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	if (play != nullptr) {
		ImGui::Begin("Kotonoha", &Kotonoha_BasicGuiShow);

		ImGui::Text("Current Scene: %d / %d", (int)context.scene + 1, (int)game->gameplays.size());
		ImGui::TextWrapped("Current File: %s",
			play->scriptPath.empty() ? "<no file path>" : play->scriptPath.c_str());

		if (ImGui::CollapsingHeader("Playback", ImGuiTreeNodeFlags_DefaultOpen)) {
			float newTime = play->GetTime();
			float lastTime = play->GetLastTime();

			ImGui::Text("Time: %.2f / %.2f", newTime, lastTime);

			if (ImGui::SliderFloat("Duration", &newTime, 0.0f, lastTime)) {
				play->SetTime(newTime);
			}

			if (ImGui::Button("Back 5s")) {
				play->SeekBackward(5000);
			}
			ImGui::SameLine();
			if (ImGui::Button("Forward 5s")) {
				play->SeekForward(5000);
			}
			ImGui::SameLine();
			ImGui::Checkbox("Paused", &context.paused);
			ImGui::SameLine();
			ImGui::Checkbox("Loop", &play->loop);

			if (ImGui::Button("Reset Scene")) {
				play->Reset(true);
			}
			ImGui::SameLine();
			if (ImGui::Button("Next Scene")) {
				context.next = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Previous Scene")) {
				context.back = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Exit")) {
				Kotonoha_GoToScene(game, context, (int)game->gameplays.size() - 1);
				context.next = true;
			}
		}

		if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
			Kotonoha::Sound* sound = static_cast<Kotonoha::Sound*>(context.sound);
			if (sound != nullptr) {
				ImGui::SliderFloat("Master", &sound->volume, 0.0f, 1.0f);

				size_t count = 0;
				for (Kotonoha::Sound::Channel* channel = sound->GetChannelByIndex(count);
					channel != nullptr;
					channel = sound->GetChannelByIndex(count)) {
					count++;
					ImGui::SliderFloat(channel->name.c_str(), &channel->volume, 0.0f, 1.0f);
				}
			}
			else {
				ImGui::TextUnformatted("Sound system unavailable.");
			}
		}

		if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Show Playlist", &show_playlist);
			ImGui::SameLine();
			if (ImGui::Checkbox("Show Scene Editor", &Kotonoha_BasicGuiEditorShow)) {
				if (Kotonoha_BasicGuiEditorShow) {
					Kotonoha_SelectScene(game, (int)context.scene);
				}
			}
		}

		ImGui::End();
	}
	else {
		ImGui::Begin("Open");

		if (ImGui::CollapsingHeader("Load Scenes", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::InputTextWithHint("##open_script_path",
				"file1.ks;file2.ks;folder/",
				openBuf,
				IM_ARRAYSIZE(openBuf));

			ImGui::SameLine();
			if (ImGui::Button("Open")) {
				showError = !Kotonoha_OpenPaths(game, openBuf, false);
				selectedSceneIndex = -1;
				editorSceneIndex = -1;
				sceneEditorText.clear();
				editorDirty = false;
				temporaryGameplayActive = false;
				temporaryGameplayIndex = -1;
				temporaryScriptPath.clear();
				originalScriptPathForTemporary.clear();

				if (!game->gameplays.empty()) {
					context.scene = 0;
					Kotonoha_TryAutoLoadCurrentScene(game, context);
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Append")) {
				showError = !Kotonoha_OpenPaths(game, openBuf, true);
				if (selectedSceneIndex < 0 && !game->gameplays.empty()) {
					Kotonoha_TryAutoLoadCurrentScene(game, context);
				}
			}

			if (showError) {
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to open one or more paths.");
			}

			if (!game->gameplays.empty()) {
				ImGui::Separator();
				ImGui::Text("Loaded scenes: %d", (int)game->gameplays.size());

				if (ImGui::Button("Start from First")) {
					context.scene = 0;
					Kotonoha_TryAutoLoadCurrentScene(game, context);
				}

				ImGui::SameLine();
				ImGui::Checkbox("Show Playlist", &show_playlist);
				ImGui::SameLine();
				ImGui::Checkbox("Show Scene Editor", &Kotonoha_BasicGuiEditorShow);
			}

			if (ImGui::Button("Exit")) {
				ImGui::End();
				ImGui::Render();
				SDL_SetRenderScale(context.render, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
				ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
				return false;
			}
		}

		ImGui::End();
	}

	if (show_playlist) {
		ImGui::Begin("Scenes", &show_playlist);

		if (context.scene < game->gameplays.size() && game->gameplays[context.scene] != nullptr) {
			ImGui::TextWrapped("Current File: %s",
				game->gameplays[context.scene]->scriptPath.empty()
				? "<no file path>"
				: game->gameplays[context.scene]->scriptPath.c_str());
		}
		else {
			ImGui::TextUnformatted("Current File: <none>");
		}

		ImGui::Text("Total: %d", (int)game->gameplays.size());

		if (ImGui::CollapsingHeader("Append", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::InputTextWithHint("##append_scene_path",
				"append file or folder",
				appendBuf,
				IM_ARRAYSIZE(appendBuf));
			ImGui::SameLine();
			if (ImGui::Button("Append##playlist")) {
				showError = !Kotonoha_OpenPaths(game, appendBuf, true);
			}
		}

		ImVec2 avail = ImGui::GetContentRegionAvail();
		if (avail.y < 120.0f) {
			avail.y = 120.0f;
		}
		avail.y -= 30.0f;

		if (ImGui::CollapsingHeader("Playlist", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::BeginListBox("##scene_playlist", avail)) {
				for (int i = 0; i < (int)game->gameplays.size(); i++) {
					Kotonoha::Gameplay* item = game->gameplays[i];
					if (item == nullptr) {
						continue;
					}

					std::string label;
					if (!item->scriptPath.empty()) {
						label = std::to_string(i) + " - " + item->scriptPath;
					}
					else {
						label = "Scene " + std::to_string(i);
					}

					const bool isSelected = (context.scene == (size_t)i);
					if (ImGui::Selectable(label.c_str(), isSelected)) {
						Kotonoha_GoToScene(game, context, i);
					}

					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}

				ImGui::EndListBox();
			}
		}

		ImGui::End();
	}

	if (Kotonoha_BasicGuiEditorShow) {
		if(context.scene != editorSceneIndex)
			Kotonoha_SelectScene(game, (int)context.scene);;

		ImGui::Begin("Scene Editor", &Kotonoha_BasicGuiEditorShow);

		if (editorSceneIndex < 0 && context.scene < game->gameplays.size()) {
			Kotonoha_SelectScene(game, (int)context.scene);
		}

		if (selectedSceneIndex >= 0 && selectedSceneIndex < (int)game->gameplays.size()) {
			Kotonoha::Gameplay* selected = game->gameplays[selectedSceneIndex];

			if (selected != nullptr) {
				ImGui::Text("Selected: %d", selectedSceneIndex);
				ImGui::TextWrapped("Current File: %s",
					selected->scriptPath.empty() ? "<no file path>" : selected->scriptPath.c_str());

				if (temporaryGameplayActive && temporaryGameplayIndex == selectedSceneIndex) {
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Temporary gameplay active.");
					ImGui::TextWrapped("Original File: %s",
						originalScriptPathForTemporary.empty()
						? "<unknown>"
						: originalScriptPathForTemporary.c_str());
					ImGui::TextWrapped("Temporary File: %s",
						temporaryScriptPath.empty()
						? "<unknown>"
						: temporaryScriptPath.c_str());
				}

				if (ImGui::Button("Load From Disk")) {
					if (Kotonoha_LoadSceneEditorFromDisk(selected)) {
						Kotonoha_RecreateGameplayAtPath(game, context, selectedSceneIndex, selected->scriptPath);
					}
				}

				ImVec2 avail2 = ImGui::GetContentRegionAvail();
				if (avail2.y < 120.0f) {
					avail2.y = 120.0f;
				}
				avail2.y -= 40.0f;

				if (ImGui::InputTextMultiline("##scene_editor", &sceneEditorText, avail2)) {
					editorDirty = true;
				}

				if (editorStatusBuf[0] != '\0') {
					ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", editorStatusBuf);
				}

				ImGui::TextUnformatted(editorDirty ? "* modified" : "saved");

				ImGui::SameLine();
				if (ImGui::Button("Save")) {
					if (Kotonoha_SaveAndReloadReplace(game, context, selectedSceneIndex)) {
						ImGui::End();
						return Kotonoha_FinishFrameAndReturn(context, io);
					}
				}

				ImGui::SameLine();
				if (ImGui::Button("Save Temporary")) {
					if (Kotonoha_LoadTemporaryGameplayAt(game, context, selectedSceneIndex)) {
						ImGui::End();
						return Kotonoha_FinishFrameAndReturn(context, io);
					}
				}

				if (temporaryGameplayActive && temporaryGameplayIndex == selectedSceneIndex) {
					ImGui::SameLine();
					if (ImGui::Button("Revert to original")) {
						if (Kotonoha_RestoreOriginalPath(game, context)) {
							ImGui::End();
							return Kotonoha_FinishFrameAndReturn(context, io);
						}
					}
				}
			}
			else {
				ImGui::TextUnformatted("Selected scene is null.");
			}
		}
		else {
			ImGui::TextUnformatted("No scene selected.");
			ImGui::TextUnformatted("Trying to load current scene automatically...");
		}

		ImGui::End();
	}

	ImGui::Render();
	SDL_SetRenderScale(context.render, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
	return true;
}

void Kotonoha_BasicGuiEvent(SDL_Event* event) {
	ImGui_ImplSDL3_ProcessEvent(event);
}
