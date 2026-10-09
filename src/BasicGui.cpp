#include <cstddef>
#include <cstdio>
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
#include <vector>

bool Kotonoha_BasicGuiShow = false;
bool Kotonoha_BasicGuiEditorShow = false;
static bool textEditorShow = false;
bool showError = false;

static char openBuf[4096] = { 0 };
static char appendBuf[4096] = { 0 };
static bool show_playlist = false;

static int selectedSceneIndex = -1;
static int editorSceneIndex = -1;
static bool editorDirty = false;

static std::string sceneEditorText;
static char editorStatusBuf[512] = { 0 };

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

static std::vector<std::string> textEditorLines;
static std::vector<TextEditorEntry> textEditorEntries;
static std::string textEditorSyncedScript;
static bool textEditorFinalNewline = false;
static bool textEditorDirty = false;

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

static bool Kotonoha_FinishFrameAndReturn(Kotonoha_Game& context) {
	ImGui::Render();
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
	return true;
}

static std::vector<std::string> Kotonoha_SplitTabs(const std::string& text) {
	std::vector<std::string> fields;
	size_t start = 0;
	while (true) {
		const size_t tab = text.find('\t', start);
		if (tab == std::string::npos) {
			fields.push_back(text.substr(start));
			break;
		}
		fields.push_back(text.substr(start, tab - start));
		start = tab + 1;
	}
	return fields;
}

static std::string Kotonoha_JoinLines(const std::vector<std::string>& lines,
	bool finalNewline) {
	std::string result;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i != 0) {
			result.push_back('\n');
		}
		result += lines[i];
	}
	if (finalNewline) {
		result.push_back('\n');
	}
	return result;
}

static std::string Kotonoha_FormatTime(Uint64 milliseconds);

static std::string Kotonoha_FormatScriptTime(const std::string& timestamp) {
	Uint64 parts[3] = { 0, 0, 0 };
	size_t cursor = 0;
	for (size_t part = 0; part < SDL_arraysize(parts); ++part) {
		const size_t start = cursor;
		while (cursor < timestamp.size() &&
			timestamp[cursor] >= '0' && timestamp[cursor] <= '9') {
			const Uint64 digit = static_cast<Uint64>(timestamp[cursor] - '0');
			if (parts[part] > (SDL_MAX_UINT64 - digit) / 10) {
				return timestamp;
			}
			parts[part] = parts[part] * 10 + digit;
			++cursor;
		}
		if (cursor == start ||
			(part < 2 && (cursor >= timestamp.size() || timestamp[cursor++] != ':')) ||
			(part == 2 && cursor != timestamp.size())) {
			return timestamp;
		}
	}

	if (parts[0] > SDL_MAX_UINT64 / 60000) {
		return timestamp;
	}
	Uint64 milliseconds = parts[0] * 60000;
	if (parts[1] > (SDL_MAX_UINT64 - milliseconds) / 1000) {
		return timestamp;
	}
	milliseconds += parts[1] * 1000;
	if (parts[2] > (SDL_MAX_UINT64 - milliseconds) / 10) {
		return timestamp;
	}
	return Kotonoha_FormatTime(milliseconds + parts[2] * 10);
}

static void Kotonoha_LoadTextEditor(const std::string& source) {
	textEditorSyncedScript = source;
	textEditorLines.clear();
	textEditorEntries.clear();
	textEditorFinalNewline = !source.empty() && source.back() == '\n';

	std::stringstream stream(source);
	std::string line;
	while (std::getline(stream, line)) {
		textEditorLines.push_back(line);
	}

	for (size_t i = 0; i < textEditorLines.size(); ++i) {
		const std::string& rawLine = textEditorLines[i];
		const size_t contentEnd = rawLine.find('\r');
		std::string content = rawLine.substr(0, contentEnd);
		const bool hasTerminator = !content.empty() && content.back() == ';';
		if (hasTerminator) {
			content.pop_back();
		}
		if (!hasTerminator) {
			continue;
		}
		const size_t equals = content.find('=');
		if (equals == std::string::npos) {
			continue;
		}

		const bool isPrintText = content.compare(0, equals, "[PrintText]") == 0;
		const bool isSelection = content.compare(0, equals, "[SetSELECT]") == 0;
		if (!isPrintText && !isSelection) {
			continue;
		}

		const std::string payload = content.substr(equals + 1);
		const std::vector<std::string> fields = Kotonoha_SplitTabs(payload);
		if ((isPrintText && fields.size() != 4) ||
			(isSelection && fields.size() < 3)) {
			continue;
		}

		TextEditorEntry entry;
		entry.isSelection = isSelection;
		entry.lineIndex = i;
		entry.startTime = Kotonoha_FormatScriptTime(fields.front());
		entry.endTime = Kotonoha_FormatScriptTime(fields.back());

		const size_t firstTab = content.find('\t', equals + 1);
		const size_t lastTab = content.rfind('\t');
		if (firstTab == std::string::npos || lastTab == std::string::npos ||
			lastTab <= firstTab) {
			continue;
		}
		entry.prefix = content.substr(0, firstTab);
		entry.suffix = content.substr(lastTab) + ";";

		if (isPrintText) {
			entry.character = fields[1];
			entry.text = fields[2];
		}
		else {
			for (size_t field = 1; field + 1 < fields.size(); ++field) {
				entry.options.push_back(fields[field]);
			}
		}
		textEditorEntries.push_back(std::move(entry));
	}
}

static void Kotonoha_SyncTextEditorToScript() {
	for (const TextEditorEntry& entry : textEditorEntries) {
		std::string replacement = entry.prefix;
		if (entry.isSelection) {
			for (const std::string& option : entry.options) {
				replacement.push_back('\t');
				replacement += option;
			}
		}
		else {
			replacement += '\t';
			replacement += entry.character;
			replacement += '\t';
			replacement += entry.text;
		}
		replacement += entry.suffix;

		const size_t carriageReturn = textEditorLines[entry.lineIndex].find('\r');
		if (carriageReturn != std::string::npos) {
			replacement += textEditorLines[entry.lineIndex].substr(carriageReturn);
		}
		textEditorLines[entry.lineIndex] = std::move(replacement);
	}
	sceneEditorText = Kotonoha_JoinLines(textEditorLines, textEditorFinalNewline);
	textEditorSyncedScript = sceneEditorText;
	textEditorDirty = true;
}

static std::string Kotonoha_FormatTime(Uint64 milliseconds) {
	const Uint64 totalCentiseconds = milliseconds / 10;
	const Uint64 minutes = totalCentiseconds / 6000;
	const Uint64 seconds = (totalCentiseconds / 100) % 60;
	const Uint64 centiseconds = totalCentiseconds % 100;
	char formatted[64];
	SDL_snprintf(formatted, sizeof(formatted), "%02llu:%02llu:%02llu",
		static_cast<unsigned long long>(minutes),
		static_cast<unsigned long long>(seconds),
		static_cast<unsigned long long>(centiseconds));
	return formatted;
}

void Kotonoha_BasicGuiInit(Kotonoha_Game& gameContext) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImFont* minhaFonte = io.Fonts->AddFontFromFileTTF(
        "assets/fonts/NotoSans-Regular.ttf",
        18.0f
    );

    IM_ASSERT(minhaFonte != nullptr);
    io.FontDefault = minhaFonte;

    ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 8.0f;
	style.ChildRounding = 6.0f;
	style.FrameRounding = 5.0f;
	style.PopupRounding = 5.0f;
	style.ScrollbarRounding = 8.0f;
	style.GrabRounding = 5.0f;
	style.WindowPadding = ImVec2(14.0f, 12.0f);
	style.FramePadding = ImVec2(8.0f, 6.0f);
	style.ItemSpacing = ImVec2(9.0f, 8.0f);
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.085f, 0.11f, 0.98f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.30f, 0.43f, 0.75f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.27f, 0.42f, 0.59f, 0.85f);
	style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.30f, 0.44f, 0.90f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.42f, 0.60f, 1.00f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.15f, 0.20f, 1.00f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.21f, 0.29f, 1.00f);
	style.Colors[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.75f, 0.96f, 1.00f);

    ImGui_ImplSDL3_InitForSDLRenderer(
        gameContext.window,
        gameContext.render
    );
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
	Kotonoha_LoadTextEditor(sceneEditorText);
	editorDirty = false;
	textEditorDirty = false;
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
	Kotonoha_LoadTextEditor(sceneEditorText);
	editorDirty = false;
	textEditorDirty = false;
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

			ImGui::Text("Tempo: %s / %s",
				Kotonoha_FormatTime(static_cast<Uint64>(newTime * 1000.0f)).c_str(),
				Kotonoha_FormatTime(static_cast<Uint64>(lastTime * 1000.0f)).c_str());

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
			ImGui::Checkbox("Playlist", &show_playlist);
			ImGui::SameLine();
			if (ImGui::Checkbox("Editor de script", &Kotonoha_BasicGuiEditorShow)) {
				if (Kotonoha_BasicGuiEditorShow) {
					Kotonoha_SelectScene(game, (int)context.scene);
				}
			}
			ImGui::SameLine();
			if (ImGui::Checkbox("Editor de texto", &textEditorShow) && textEditorShow) {
				Kotonoha_SelectScene(game, (int)context.scene);
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
				ImGui::Checkbox("Playlist", &show_playlist);
				ImGui::SameLine();
				ImGui::Checkbox("Editor de script", &Kotonoha_BasicGuiEditorShow);
				ImGui::SameLine();
				ImGui::Checkbox("Editor de texto", &textEditorShow);
			}

			if (ImGui::Button("Exit")) {
				ImGui::End();
				ImGui::Render();
				ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
				return false;
			}
		}

		ImGui::End();
	}

	if (show_playlist) {
		ImGui::Begin("Playlist", &show_playlist);

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

		ImGui::Begin("Editor de script", &Kotonoha_BasicGuiEditorShow);

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
					Kotonoha_LoadTextEditor(sceneEditorText);
				}

				if (editorStatusBuf[0] != '\0') {
					ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", editorStatusBuf);
				}

				ImGui::TextUnformatted(editorDirty ? "* modified" : "saved");

				ImGui::SameLine();
				if (ImGui::Button("Save")) {
					if (Kotonoha_SaveAndReloadReplace(game, context, selectedSceneIndex)) {
						ImGui::End();
						return Kotonoha_FinishFrameAndReturn(context);
					}
				}

				ImGui::SameLine();
				if (ImGui::Button("Save Temporary")) {
					if (Kotonoha_LoadTemporaryGameplayAt(game, context, selectedSceneIndex)) {
						ImGui::End();
						return Kotonoha_FinishFrameAndReturn(context);
					}
				}

				if (temporaryGameplayActive && temporaryGameplayIndex == selectedSceneIndex) {
					ImGui::SameLine();
					if (ImGui::Button("Revert to original")) {
						if (Kotonoha_RestoreOriginalPath(game, context)) {
							ImGui::End();
							return Kotonoha_FinishFrameAndReturn(context);
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

	if (textEditorShow) {
		if (context.scene != editorSceneIndex) {
			Kotonoha_SelectScene(game, static_cast<int>(context.scene));
		}
		else if (sceneEditorText != textEditorSyncedScript) {
			Kotonoha_LoadTextEditor(sceneEditorText);
		}

		ImGui::Begin("Editor de texto", &textEditorShow);
		if (selectedSceneIndex >= 0 &&
			selectedSceneIndex < static_cast<int>(game->gameplays.size()) &&
			game->gameplays[selectedSceneIndex] != nullptr) {
			Kotonoha::Gameplay* selected = game->gameplays[selectedSceneIndex];
			ImGui::Text("Cena %d", selectedSceneIndex + 1);
			ImGui::TextWrapped("%s", selected->scriptPath.empty()
				? "<sem caminho de arquivo>"
				: selected->scriptPath.c_str());
			ImGui::Separator();
			ImGui::TextWrapped("Edite somente os campos de personagem, fala e opções. Os tempos e os demais comandos permanecem inalterados.");
			ImGui::TextUnformatted((textEditorDirty || editorDirty)
				? "Alterações não salvas"
				: "Sincronizado com o script");

			ImVec2 listSize = ImGui::GetContentRegionAvail();
			listSize.y = SDL_max(listSize.y - 48.0f, 120.0f);
			if (ImGui::BeginChild("##text_event_list", listSize, ImGuiChildFlags_Borders)) {
				if (textEditorEntries.empty()) {
					ImGui::TextDisabled("Nenhum PRINT_TEXT ou SetSELECT nesta cena.");
				}

				bool changed = false;
				for (size_t i = 0; i < textEditorEntries.size(); ++i) {
					TextEditorEntry& entry = textEditorEntries[i];
					ImGui::PushID(static_cast<int>(i));
					ImGui::TextColored(ImVec4(0.55f, 0.76f, 0.96f, 1.0f),
						"%s  %s - %s",
						entry.isSelection ? "ESCOLHA" : "FALA",
						entry.startTime.c_str(),
						entry.endTime.c_str());

					if (entry.isSelection) {
						for (size_t option = 0; option < entry.options.size(); ++option) {
							ImGui::SetNextItemWidth(-1.0f);
							std::string label = "Opção " + std::to_string(option + 1);
							if (ImGui::InputText(label.c_str(), &entry.options[option])) {
								changed = true;
							}
						}
					}
					else {
						ImGui::SetNextItemWidth(-1.0f);
						if (ImGui::InputText("Personagem", &entry.character)) {
							changed = true;
						}
						ImGui::SetNextItemWidth(-1.0f);
						if (ImGui::InputText("Fala", &entry.text)) {
							changed = true;
						}
					}
					ImGui::Separator();
					ImGui::PopID();
				}
				if (changed) {
					Kotonoha_SyncTextEditorToScript();
					editorDirty = true;
				}
				ImGui::EndChild();
			}

			if (ImGui::Button("Salvar e recarregar")) {
				if (Kotonoha_SaveAndReloadReplace(game, context, selectedSceneIndex)) {
					ImGui::End();
					return Kotonoha_FinishFrameAndReturn(context);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Salvar temporariamente")) {
				if (Kotonoha_LoadTemporaryGameplayAt(game, context, selectedSceneIndex)) {
					ImGui::End();
					return Kotonoha_FinishFrameAndReturn(context);
				}
			}
			if (temporaryGameplayActive && temporaryGameplayIndex == selectedSceneIndex) {
				ImGui::SameLine();
				if (ImGui::Button("Reverter")) {
					if (Kotonoha_RestoreOriginalPath(game, context)) {
						ImGui::End();
						return Kotonoha_FinishFrameAndReturn(context);
					}
				}
			}
			if (editorStatusBuf[0] != '\0') {
				ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", editorStatusBuf);
			}
		}
		else {
			ImGui::TextUnformatted("Nenhuma cena selecionada.");
		}
		ImGui::End();
	}

	ImGui::Render();
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
	return true;
}

void Kotonoha_BasicGuiEvent(SDL_Event* event) {
	ImGui_ImplSDL3_ProcessEvent(event);
}
