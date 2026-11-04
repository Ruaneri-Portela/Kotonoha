#pragma once
#include <Kotonoha/componentes/TouchActivation.hpp>

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Scene.hpp>

#include <functional>
#include <string>

namespace Kotonoha {

class TextInputDialog final : public Scene {
  TouchActivation touchActivation;
private:
  std::string promptText;
  std::string input;
  bool editing = false;

  std::function<void(const std::string &)> onConfirm;
  std::function<void()> onCancel;

public:
  bool designSpace() const override { return true; }
  TextInputDialog(Engine &engineRef, std::string prompt,
                  std::function<void(const std::string &)> confirmCb,
                  std::function<void()> cancelCb,
                  const std::string &initialText = {});

  ~TextInputDialog() override = default;
  void configure(const std::string &prompt,
                 std::function<void(const std::string &)> confirmCb,
                 std::function<void()> cancelCb,
                 const std::string &initialText = {});
  bool capturesInput() const override { return true; }
  bool pausesStory() const override { return true; }
  void submitText(const std::string &text);

  bool onEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, float scale) override;
};

TextInputDialog &
GetTextInputDialogRuntime(Engine &engineRef, const std::string &prompt,
                          std::function<void(const std::string &)> confirmCb,
                          std::function<void()> cancelCb,
                          const std::string &initialText = {});

} // namespace Kotonoha