#pragma once

#include <Kotonoha/core/Clock.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Scene.hpp>

#include <string>

namespace Kotonoha {

// Typewriter dialogue box. Games derive from it and override the protected
// hooks to change how the box looks, paces text or reaches the menus.
class VNTextBox : public Scene {
protected:
  Clock clock;

  std::string speakerName;
  std::string fullDialogue;
  std::string visibleDialogue;

  double cps = 30.0; // Caracteres por segundo
  double visibleChars = 0.0;
  bool isComplete = false;
  bool visible = true;

  virtual double charactersPerSecond() const { return cps; }
  virtual void recordDialogue(const std::string &speaker,
                              const std::string &dialogue);
  virtual void openMenu();
  virtual void openBacklog();
  virtual void renderFrame(RendererRef renderer);
  virtual void renderSpeaker(RendererRef renderer);
  virtual void renderDialogue(RendererRef renderer);
  virtual void renderIndicator(RendererRef renderer);

public:
  bool designSpace() const override { return true; }
  VNTextBox(Engine &engineRef, std::string speaker, std::string dialogue,
            double charsPerSecond = 30.0);

  virtual void setDialogue(const std::string &speaker,
                           const std::string &dialogue);
  void setTextSpeed(double charsPerSecond);
  double getTextSpeed() const { return cps; }

  void onEvent(EventRef event) override;
  bool handleEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, float scale) override;

  bool isDialogueComplete() const { return isComplete; }

  virtual void completeDialogue() {
    visibleChars = static_cast<double>(fullDialogue.size());
    visibleDialogue = fullDialogue;
    isComplete = true;
  }

  void hide() { visible = false; }
  void show() { visible = true; }
  bool isVisible() const { return visible; }
  const std::string &getText() const { return fullDialogue; }
  const std::string &getSpeaker() const { return speakerName; }
  const std::string &getVisibleText() const { return visibleDialogue; }
};

VNTextBox &GetVNTextBoxRuntime(Engine &engineRef, const std::string &speaker,
                               const std::string &dialogue, double cps);

} // namespace Kotonoha