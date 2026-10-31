#pragma once

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Scene.hpp>

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Kotonoha {

struct ChoiceOption {
  std::string label;
  int optionId;
  SDL_FRect hitBox{0.0f, 0.0f, 0.0f, 0.0f};
};

// Question with a scrollable list of options. Subclasses override the layout,
// drawing, hit area and selection hooks to restyle it or add choice kinds.
class ChoiceQuestionScene : public Scene {
protected:
  std::string questionText;
  std::vector<ChoiceOption> options;
  std::function<void(int optionId, const std::string &label)> onChoiceSelected;
  bool active = false;
  std::size_t selected = 0;
  float scroll = 0;
  // Area the options scroll and receive clicks in.
  virtual SDL_FRect listArea() const { return {230, 190, 820, 440}; }
  virtual void layout();
  virtual void select(std::size_t index);
  virtual void openMenu();
  virtual void openBacklog();
  virtual void renderPanel(RendererRef renderer);
  virtual void renderOption(RendererRef renderer, std::size_t index,
                            bool focused);

public:
  bool designSpace() const override { return true; }
  ChoiceQuestionScene(Engine &engineRef, std::string question,
                      std::vector<ChoiceOption> choices,
                      std::function<void(int, const std::string &)> callback);

  virtual void setQuestion(const std::string &question,
                   const std::vector<ChoiceOption> &choices,
                   std::function<void(int, const std::string &)> callback) {
    if (choices.empty())
      throw std::invalid_argument(
          "Choice component requires at least one option");
    questionText = question;
    options = choices;
    onChoiceSelected = std::move(callback);
    active = true;
    selected = 0;
    scroll = 0;
  }

  virtual void hide() { active = false; }
  bool isActive() const { return active; }
  const std::string &getQuestion() const { return questionText; }
  const std::vector<ChoiceOption> &getOptions() const { return options; }
  void chooseOption(std::size_t index) {
    if (!active)
      throw std::logic_error("No question is active");
    select(index);
  }
  bool capturesInput() const override { return active; }

  void onEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, float scale) override;
};

ChoiceQuestionScene &GetChoiceQuestionRuntime(
    Engine &engineRef, const std::string &question = "",
    const std::vector<ChoiceOption> &options = {},
    std::function<void(int, const std::string &)> callback = nullptr);

} // namespace Kotonoha