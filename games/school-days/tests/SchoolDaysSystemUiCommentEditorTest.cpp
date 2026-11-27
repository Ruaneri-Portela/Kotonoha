#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/routing/SchoolDaysSystemUi.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void PutBytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    Require(bool(output), "failed to write " + path.string());
}

void WriteCmap(const fs::path& path) {
    std::vector<uint8_t> bytes(9u, 0u);
    bytes[0] = 1u;
    bytes[4] = 1u;
    bytes[8] = 1u; // region 1 -> SaveLoad control 0 / first row
    PutBytes(path, bytes);
}

void WriteAssets(const fs::path& root) {
    fs::create_directories(root / "Script" / "English");
    const auto saveLoad = root / "System" / "SaveLoad";
    WriteCmap(saveLoad / "SaveLoad_Wide_Full.cmap");
    PutBytes(saveLoad / "Save.PNG", {0u});
    PutBytes(saveLoad / "SaveLoad_Chip.PNG", {0u});
    const std::vector<uint8_t> font(65536u * 4u, 0u);
    PutBytes(root / "System" / "System" / "FONTDATA.DAT", font);
    PutBytes(root / "System" / "System" / "FONTDATA_eng.DAT", font);
}

struct TempDir {
    fs::path path = fs::temp_directory_path() /
                    ("school-days-comment-editor-" +
                     std::to_string(SDL_GetPerformanceCounter()));
    TempDir() { Require(fs::create_directory(path), "failed to create temp directory"); }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

class FakeAssets final : public Kotonoha::AssetProvider {
public:
    std::string name() const override { return "school-days-comment-test"; }
    bool containsAsset(const std::string&) const override { return true; }
    std::vector<Uint8> readAsset(const std::string&) const override { return {}; }
    Kotonoha::SurfacePtr loadSurfaceAsset(const std::string&) const override {
        Kotonoha::SurfacePtr surface(
            SDL_CreateSurface(800, 450, SDL_PIXELFORMAT_RGBA32));
        Require(bool(surface), SDL_GetError());
        Require(SDL_FillSurfaceRect(surface.get(), nullptr, 0xff202020u),
                SDL_GetError());
        return surface;
    }
};

SDL_Event Motion(float x = 640.0f, float y = 360.0f) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

SDL_Event Click(float x = 640.0f, float y = 360.0f) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    return event;
}

SDL_Event Key(SDL_Keycode key) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    return event;
}

void SendText(Kotonoha::SchoolDaysSystemUi& ui, SDL_Renderer* renderer,
              const std::string& text) {
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = text.c_str();
    Kotonoha::SchoolDaysSystemUi::Output output;
    Require(ui.HandleEvent(renderer, &event, &output),
            "comment editor rejected SDL text input");
    Require(!output.hasSaveLoadOutput,
            "text input unexpectedly emitted a SaveLoad action");
}

void OpenEditor(Kotonoha::SchoolDaysSystemUi& ui, SDL_Renderer* renderer) {
    auto motion = Motion();
    Kotonoha::SchoolDaysSystemUi::Output hover;
    Require(ui.HandleEvent(renderer, &motion, &hover),
            "SaveLoad first-row hover failed");
    auto click = Click();
    Kotonoha::SchoolDaysSystemUi::Output activation;
    Require(ui.HandleEvent(renderer, &click, &activation),
            "SaveLoad first-row activation failed");
    Require(!activation.hasSaveLoadOutput,
            "CommentInputRequested leaked past SchoolDaysSystemUi");
    Require(ui.IsSaveLoadCommentEditorOpen(),
            "SaveLoad comment editor did not open asynchronously");
}

void ConfigureSaveLoad(Kotonoha::SchoolDaysSystemUi& ui, SDL_Renderer* renderer,
                       const fs::path& root, bool english,
                       const std::u16string& existing, bool occupied) {
    std::string error;
    Require(ui.OpenSaveLoad(
                renderer, root.string(),
                Kotonoha::SchoolDaysSaveLoadModel::Variant::WideFull,
                Kotonoha::SchoolDaysSaveLoadModel::Mode::Save, true, english,
                "System/System/FONTDATA.DAT", &error),
            error);
    ui.SetArea({1280, 720});
    std::vector<Kotonoha::SchoolDaysSaveService::SlotMetadata> metadata(10u);
    metadata[0].occupied = occupied;
    metadata[0].comment = existing;
    Require(ui.SetSaveLoadMetadata(metadata, &error), error);
}

void TestEnglishConfirm(Kotonoha::SchoolDaysSystemUi& ui, SDL_Renderer* renderer,
                        SDL_Window* window, const fs::path& root) {
    ConfigureSaveLoad(ui, renderer, root, true, u"old", true);
    OpenEditor(ui, renderer);
    Require(ui.SaveLoadCommentEditorText() == u"old",
            "editor did not seed occupied slot metadata.comment");
    Require(SDL_TextInputActive(window), "SDL text input did not start");
    SendText(ui, renderer, "!");
    Require(ui.SaveLoadCommentEditorText() == u"old!",
            "UTF-8 ASCII input was not appended");

    auto enter = Key(SDLK_RETURN);
    Kotonoha::SchoolDaysSystemUi::Output immediate;
    Require(ui.HandleEvent(renderer, &enter, &immediate),
            "Enter was not consumed by comment editor");
    Require(!immediate.hasSaveLoadOutput,
            "Enter emitted SaveSlotRequested synchronously");
    Require(!ui.IsSaveLoadCommentEditorOpen(), "Enter did not close editor");
    Require(!SDL_TextInputActive(window), "Enter did not stop SDL text input");
    Require(ui.SaveLoad().SavePending(),
            "Enter did not arm the deferred SaveLoad writer");

    Kotonoha::SchoolDaysSystemUi::Output next;
    ui.Update(0u, 0, 0, &next);
    Require(next.hasSaveLoadOutput &&
                next.saveLoadOutput.action ==
                    Kotonoha::SchoolDaysSaveLoadModel::Action::SaveSlotRequested &&
                next.saveLoadOutput.slot == 0 && next.saveLoadOutput.comment == u"old!",
            "confirmed comment did not emit SaveSlotRequested on next Update");
    Kotonoha::SchoolDaysSystemUi::Output following;
    ui.Update(1u, 0, 0, &following);
    Require(!following.hasSaveLoadOutput,
            "deferred SaveLoad writer emitted the same save twice");
}

void TestUtf16EditingAndCancel(Kotonoha::SchoolDaysSystemUi& ui,
                               SDL_Renderer* renderer, SDL_Window* window,
                               const fs::path& root) {
    ConfigureSaveLoad(ui, renderer, root, false, {}, false);
    OpenEditor(ui, renderer);
    SendText(ui, renderer, "\xf0\x9f\x98\x80"); // U+1F600, two UTF-16 units
    Require(ui.SaveLoadCommentEditorText().size() == 2u,
            "supplementary UTF-8 input did not become a surrogate pair");
    auto backspace = Key(SDLK_BACKSPACE);
    Kotonoha::SchoolDaysSystemUi::Output ignored;
    Require(ui.HandleEvent(renderer, &backspace, &ignored),
            "Backspace was not consumed by comment editor");
    Require(ui.SaveLoadCommentEditorText().empty(),
            "Backspace split a surrogate pair instead of deleting one code point");

    SendText(ui, renderer, std::string(80u, 'a'));
    Require(ui.SaveLoadCommentEditorText().size() == 60u,
            "JP comment editor did not enforce the 60 UTF-16-unit limit");
    for (int i = 0; i < 60; ++i) {
        auto key = Key(SDLK_BACKSPACE);
        ui.HandleEvent(renderer, &key, &ignored);
    }
    SendText(ui, renderer, std::string(59u, 'b'));
    SendText(ui, renderer, "\xf0\x9f\x98\x80");
    Require(ui.SaveLoadCommentEditorText().size() == 59u,
            "UTF-16 limit admitted half of a surrogate pair");
    SendText(ui, renderer, "c");
    Require(ui.SaveLoadCommentEditorText().size() == 60u,
            "single-unit input did not fill the final JP UTF-16 unit");

    auto escape = Key(SDLK_ESCAPE);
    Kotonoha::SchoolDaysSystemUi::Output cancelOutput;
    Require(ui.HandleEvent(renderer, &escape, &cancelOutput),
            "Escape was not consumed by comment editor");
    Require(!ui.IsSaveLoadCommentEditorOpen() && !ui.SaveLoad().SavePending() &&
                !ui.SaveLoad().CommentInputPending(),
            "Escape did not cancel without arming a save");
    Require(!SDL_TextInputActive(window), "Escape did not stop SDL text input");
    Kotonoha::SchoolDaysSystemUi::Output next;
    ui.Update(0u, 0, 0, &next);
    Require(!next.hasSaveLoadOutput,
            "cancelled comment emitted a save on following Update");
}

void TestEnglishLimitAndClose(Kotonoha::SchoolDaysSystemUi& ui,
                              SDL_Renderer* renderer, SDL_Window* window,
                              const fs::path& root) {
    ConfigureSaveLoad(ui, renderer, root, true, {}, false);
    OpenEditor(ui, renderer);
    SendText(ui, renderer, std::string(140u, 'x'));
    Require(ui.SaveLoadCommentEditorText().size() == 120u,
            "EN comment editor did not enforce the 120 UTF-16-unit limit");
    ui.Close();
    Require(!ui.IsSaveLoadCommentEditorOpen(), "Close retained comment editor state");
    Require(!SDL_TextInputActive(window), "Close did not stop SDL text input");
}

void TestMouseButtons(Kotonoha::SchoolDaysSystemUi& ui, SDL_Renderer* renderer,
                      SDL_Window* window, const fs::path& root) {
    ConfigureSaveLoad(ui, renderer, root, true, u"mouse", true);
    OpenEditor(ui, renderer);
    auto ok = Click(910.0f, 414.0f);
    Kotonoha::SchoolDaysSystemUi::Output ignored;
    Require(ui.HandleEvent(renderer, &ok, &ignored),
            "comment OK mouse button was not consumed");
    Require(!ui.IsSaveLoadCommentEditorOpen() && ui.SaveLoad().SavePending(),
            "comment OK mouse button did not arm deferred save");
    Require(!SDL_TextInputActive(window), "comment OK mouse button retained text input");
    Kotonoha::SchoolDaysSystemUi::Output save;
    ui.Update(0u, 0, 0, &save);
    Require(save.hasSaveLoadOutput && save.saveLoadOutput.comment == u"mouse",
            "comment OK mouse button did not publish the edited comment");

    ConfigureSaveLoad(ui, renderer, root, true, u"cancel", true);
    OpenEditor(ui, renderer);
    auto cancel = Click(1025.0f, 414.0f);
    Require(ui.HandleEvent(renderer, &cancel, &ignored),
            "comment Cancel mouse button was not consumed");
    Require(!ui.IsSaveLoadCommentEditorOpen() && !ui.SaveLoad().SavePending() &&
                !ui.SaveLoad().CommentInputPending(),
            "comment Cancel mouse button armed a save");
    Require(!SDL_TextInputActive(window),
            "comment Cancel mouse button retained text input");
}

} // namespace

int main() {
    try {
        Require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        {
            TempDir temp;
            WriteAssets(temp.path);
            FakeAssets assets;
            Kotonoha::WindowPtr window(
                SDL_CreateWindow("School Days comment editor test", 1280, 720,
                                 SDL_WINDOW_HIDDEN));
            Require(bool(window), SDL_GetError());
            Kotonoha::RendererPtr renderer(
                SDL_CreateRenderer(window.get(), "software"));
            Require(bool(renderer), SDL_GetError());

            Kotonoha::SchoolDaysSystemUi ui(&assets);
            TestEnglishConfirm(ui, renderer.get(), window.get(), temp.path);
            TestUtf16EditingAndCancel(ui, renderer.get(), window.get(), temp.path);
            TestEnglishLimitAndClose(ui, renderer.get(), window.get(), temp.path);
            TestMouseButtons(ui, renderer.get(), window.get(), temp.path);
        }

        std::cout << "SchoolDaysSystemUiCommentEditorTest PASS\n";
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SchoolDaysSystemUiCommentEditorTest: " << error.what() << '\n';
        SDL_Quit();
        return 2;
    }
}
