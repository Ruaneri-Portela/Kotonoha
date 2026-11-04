#pragma once
#include "Assets.hpp"
#include <Amagami/Campaign.hpp>
#include <Amagami/Session.hpp>
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
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
// Original BackLog/LogLine presentation backed by Kotonoha's scroll gestures.
class AmagamiBacklog final : public Kotonoha::VNBacklog {
public:
  explicit AmagamiBacklog(Kotonoha::Engine &engine) : VNBacklog(engine) {}
  bool designSpace() const override { return false; }
  void setHistory(const std::vector<Dialogue> &, int route);
  void open() override;
  void close() override;
  bool onEvent(Kotonoha::EventRef) override;
  void translationChanged(const std::string &, const std::string &,
                          const std::string &) override {
    layoutDirty_ = true;
  }
  std::function<void()> onClose;
  std::function<void(int)> soundEffect;
  size_t focusedEntry() const { return focused_; }

protected:
  SDL_FRect closeButtonArea() const override { return {0, 0, 0, 0}; }
  float scrollInputHeight() const override { return 448; }
  void renderFrame(Kotonoha::RendererRef) override;
  void renderEntries(Kotonoha::RendererRef) override;

private:
  struct Row {
    std::string text;
    std::vector<std::string> lines;
    float top = 0, height = 132;
  };
  std::vector<std::string> originals_;
  std::vector<Row> rows_;
  int route_ = -1;
  size_t focused_ = 0;
  bool layoutDirty_ = true;
  std::string fontKey_;
  void ensureLayout();
  void focus(size_t);
};
// Selects only EventNode.isExecutable entries. The original map assets/grid
// are retained; full native icon overlays and transition choreography are
// still separate presentation work, not inferred replacement route rules.
class AmagamiEventMap final : public Kotonoha::ChoiceQuestionScene {
public:
  AmagamiEventMap(Kotonoha::Engine &engine, Assets &assets)
      : ChoiceQuestionScene(engine, "", {}, nullptr), assets_(assets) {}
  bool designSpace() const override { return false; }
  void setEvents(std::vector<Campaign::Event> available,
                 std::vector<Campaign::Event> map, int day, int period,
                 std::function<void(int, const std::string &)> callback);
  bool onEvent(Kotonoha::EventRef) override;
  void render(Kotonoha::RendererRef, float) override;
  std::vector<std::string> sourceKeys() const;

protected:
  SDL_FRect listArea() const override { return {40, 16, 580, 416}; }
  void layout() override;

private:
  Assets &assets_;
  std::vector<Campaign::Event> events_, map_;
  int day_ = 0, period_ = 0;
  int originX_ = 0, originY_ = 0;
  void move(int dx, int dy);
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
  bool continuousTouch() const override { return true; }
  void render(Kotonoha::RendererRef, float) override;
  bool onEvent(Kotonoha::EventRef) override;
  bool handleNewGameAction() override;
  bool handleBacklogAction() override;
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
  std::unique_ptr<AmagamiBacklog> backlogView_;
  std::unique_ptr<AmagamiEventMap> eventMap_;
  std::string campaignMapKey_;
  void syncCampaignMap();
  Kotonoha::Clock frameClock_;
  bool titleMenu_ = false;
  void syncChoices();
  size_t displayed_ = 0;
  double presentationSeconds_ = 0;
  void syncDialogue();
  void bindSessionAudio();
  void updateModes(double seconds);
  void renderModeIndicators(Kotonoha::RendererRef);
  double autoElapsed_ = 0, speedGaugeSeconds_ = 0;
  float autoIndicator_ = 0, skipIndicator_ = 0;
  int lastMessageSpeed_ = -1;
  Kotonoha::Engine *engine_ = nullptr;
  Kotonoha::PcmPlayer music_, voice_, backlogVoice_, ambience_;
  std::vector<std::unique_ptr<Kotonoha::PcmPlayer>> soundEffects_;
  size_t nextSoundEffect_ = 0;
  std::vector<float> voiceEnvelope_;
  std::string start_, savePath_, error_, notice_;
  bool paused_ = false, backlog_ = false;
  size_t focus_ = 0, voiced_ = 0;
  void start(const std::string &name, bool campaign = false);
  void advance();
  void audio(const std::string &, const std::string &, bool loop = false,
             bool historyReplay = false);
  void picture(SDL_Renderer *, const std::string &, SDL_FRect);
  void actor(SDL_Renderer *, const Visual &);
  void activate(size_t);
};
} // namespace Amagami
