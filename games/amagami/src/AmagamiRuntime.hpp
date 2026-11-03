#pragma once
#include "Assets.hpp"
#include <Amagami/Session.hpp>
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
namespace Amagami {
class AmagamiTextBox final : public Kotonoha::VNTextBox {
public:
  explicit AmagamiTextBox(Kotonoha::Engine &engine)
      : VNTextBox(engine, "", "", 10.0) {}
  bool designSpace() const override { return false; }
  void setOriginalKey(const std::string &key);
  void update() override;
  void completeDialogue() override;
  void translationChanged(const std::string &, const std::string &,
                          const std::string &) override;

private:
  std::string originalKey_, layoutKey_;
  float layoutFont_ = 24, layoutStep_ = 44;
  std::vector<std::pair<size_t, double>> waits_;
  size_t nextWait_ = 0;
  double pauseSeconds_ = 0;

protected:
  void recordDialogue(const std::string &, const std::string &) override {}
  void renderFrame(Kotonoha::RendererRef) override {}
  void renderSpeaker(Kotonoha::RendererRef) override;
  void renderDialogue(Kotonoha::RendererRef) override;
  void renderIndicator(Kotonoha::RendererRef) override;
};
class AmagamiChoices final : public Kotonoha::ChoiceQuestionScene {
public:
  AmagamiChoices(Kotonoha::Engine &engine, Assets &assets)
      : ChoiceQuestionScene(engine, "", {}, nullptr), assets_(assets) {}
  bool designSpace() const override { return false; }

protected:
  SDL_FRect listArea() const override { return {40, 284, 560, 132}; }
  float scrollInputHeight() const override { return 448; }
  void layout() override;
  void renderPanel(Kotonoha::RendererRef) override;
  void renderOption(Kotonoha::RendererRef, size_t, bool) override;

private:
  Assets &assets_;
};
class AmagamiRuntime final : public Kotonoha::Runtime {
public:
  explicit AmagamiRuntime(std::shared_ptr<Assets> assets,
                          std::string start = {});
  ~AmagamiRuntime() override;
  void shutdown();
  const Session *session() const { return session_.get(); }
  const std::string &error() const { return error_; }
  std::string diagnostic() const override { return error_; }
  std::optional<double> debugNodeSeconds() const override {
    return session_ ? std::optional<double>(presentationSeconds_)
                    : std::nullopt;
  }
  void initGlobal(Kotonoha::Engine &) override;
  void initMenu(Kotonoha::Engine &) override;
  void initGameplay(Kotonoha::Engine &) override;
  void render(Kotonoha::RendererRef, float) override;
  void onEvent(Kotonoha::EventRef) override;
  bool handleNewGameAction() override;
  bool handleAdvanceAction() override;
  bool advanceStoryTime(double) override;
  void update() override;
  void updatePresentation(double seconds);
  SDL_Point viewport() const override { return {960, 672}; }
  SDL_FPoint sceneSize(const Kotonoha::Scene &) const override;
  std::string debugNode() const override;
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  void debugJumpToNode(const std::string &) override;
  int translationRoute() const override;
  std::vector<std::string> activeTranslationKeys() const override;
  Kotonoha::Translation::SourceTexts translationSources() const override;
  std::vector<int> translationRoutes() const override;
  std::vector<std::string> translationTexts(int) const override;
  void saveGame(const std::string &) override;
  void loadGame(const std::string &) override;

private:
  std::shared_ptr<Assets> assets_;
  std::unique_ptr<Session> session_;
  std::unique_ptr<AmagamiTextBox> textBox_;
  std::unique_ptr<AmagamiChoices> choices_;
  Kotonoha::Clock frameClock_;
  bool titleMenu_ = false;
  void syncChoices();
  size_t displayed_ = 0;
  double presentationSeconds_ = 0;
  void syncDialogue();
  Kotonoha::Engine *engine_ = nullptr;
  Kotonoha::PcmPlayer music_, voice_;
  std::vector<float> voiceEnvelope_;
  std::string start_, savePath_, error_, notice_;
  bool paused_ = false, backlog_ = false;
  size_t focus_ = 0, voiced_ = 0;
  void start(const std::string &name);
  void advance();
  void audio(const std::string &, const std::string &, bool loop = false);
  void picture(SDL_Renderer *, const std::string &, SDL_FRect);
  void actor(SDL_Renderer *, const Visual &);
  void activate(size_t);
};
} // namespace Amagami
