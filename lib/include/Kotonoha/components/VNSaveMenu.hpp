#pragma once
#include <Kotonoha/components/TouchActivation.hpp>

#include <Kotonoha/components/VNSave.hpp>
#include <Kotonoha/core/Scene.hpp>
#include <Kotonoha/events/InputActions.hpp>

#include <cstddef>
#include <memory>

namespace Kotonoha {
class TextInputDialog;

class VNSaveMenu : public Scene {
protected:
  enum class Confirmation { None, Overwrite, Delete };
  // Optional grid skins reuse the native slot manager, confirmations and
  // focus. The same rectangles drive drawing and pointer hit tests.
  struct SlotGrid {
    std::size_t columns, rows;
    SDL_FRect first;
    float xStep, yStep;
    SDL_FRect back, previous, next, primary, secondary;
    std::size_t capacity() const { return columns * rows; }
    SDL_FRect card(std::size_t index) const {
      return {first.x + float(index % columns) * xStep,
              first.y + float(index / columns) * yStep, first.w, first.h};
    }
  };
  virtual const SlotGrid *slotGrid() const { return nullptr; }
  std::size_t count() const;
  std::size_t firstSlot() const { return first; }
  std::size_t focusedSlot() const { return selected; }
  bool loadingSlots() const { return loading; }
  bool hasSelectedSave() const;
  std::string slotLabel(std::size_t index) const;
  std::string slotPath(std::size_t index) const;
  std::string confirmationPrompt() const;
  Confirmation currentConfirmation() const { return confirmation; }
  const std::string &errorMessage() const { return error; }

private:
  TouchActivation touchActivation;
  bool opened = false, loading = false;
  ScrollGesture scrollGesture;
  float listScroll = 0;
  Confirmation confirmation = Confirmation::None;
  std::unique_ptr<TextInputDialog> renameDialog;
  std::string directory, error, suggestedName;
  std::function<void()> onCloseCb;
  void beginCreate();
  std::vector<VNSaveSlot> slots;
  std::size_t selected = 0, first = 0;
  void activate();
  void revealSelection();
  const VNSaveSlot &selectedSave() const;
  void refresh(const std::string &preferredPath = "");
  void beginRename();
  void finishRename();
  void deleteSelected();
  void reportError(const std::string &message);
  bool gridEvent(EventRef event, const SlotGrid &grid);

public:
  bool designSpace() const override { return true; }
  RenderLayer renderLayer() const override { return RenderLayer::Overlay; }
  explicit VNSaveMenu(Engine &engine);
  ~VNSaveMenu() override;
  void open(const std::string &directory, bool load,
            const std::string &suggestedName = {});
  void close();
  bool isOpen() const { return opened; }
  void SetOnClose(std::function<void()> cb) { onCloseCb = std::move(cb); }
  bool capturesInput() const override { return opened; }
  bool pausesStory() const override { return opened; }
  bool onEvent(EventRef event) override;
  void render(RendererRef renderer, float scale) override;
};

} // namespace Kotonoha
