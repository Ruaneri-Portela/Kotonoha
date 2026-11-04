#pragma once
#include <Kotonoha/componentes/TouchActivation.hpp>
#include <Kotonoha/core/Scene.hpp>
#include <functional>
#include <string>
#include <vector>
namespace Kotonoha {
// Shared touch/keyboard form; submission is atomic across all fields.
class TextInputForm final : public Scene {
  TouchActivation touchActivation;
public:
  struct Field {
    std::string label, value, defaultValue;
  };
  using Confirm = std::function<std::string(const std::vector<std::string> &)>;
  explicit TextInputForm(Engine &engine) : Scene(engine) {}
  ~TextInputForm() override;
  void configure(std::string title, std::vector<Field> fields, Confirm confirm,
                 std::function<void()> cancel);
  void close();
  void submit(const std::vector<std::string> &values);
  bool designSpace() const override { return true; }
  bool capturesInput() const override { return active; }
  bool pausesStory() const override { return active; }
  bool handleEvent(EventRef event) override {
    if (!active)
      return false;
    onEvent(event);
    return true;
  }
  bool onEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, float) override;

private:
  std::string title, error;
  std::vector<Field> fields;
  Confirm confirm;
  std::function<void()> cancel;
  size_t focus = 0;
  bool active = false;
};
} // namespace Kotonoha
