#pragma once

#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/core/Scene.hpp>
#include <memory>

namespace Kotonoha {
class TextInputDialog;

class VNSaveMenu final : public Scene {
  enum class Confirmation { None, Overwrite, Delete };
  bool opened = false, loading = false;
  Confirmation confirmation = Confirmation::None;
  std::unique_ptr<TextInputDialog> renameDialog;
  std::string directory, error;
  std::vector<VNSaveSlot> slots;
  std::size_t selected = 0, first = 0;
  std::size_t count() const;
  void activate();
  void revealSelection();
  bool hasSelectedSave() const;
  const VNSaveSlot &selectedSave() const;
  void refresh(const std::string &preferredPath = "");
  void beginRename();
  void finishRename();
  void deleteSelected();
  void reportError(const std::string &message);

public:
  bool designSpace() const override { return true; }
  explicit VNSaveMenu(Engine &engine);
  ~VNSaveMenu() override;
  void open(const std::string &directory, bool load);
  void close();
  bool isOpen() const { return opened; }
  bool capturesInput() const override { return opened; }
  bool pausesStory() const override { return opened; }
  void onEvent(EventRef event) override;
  void render(RendererRef renderer, float scale) override;
};

} // namespace Kotonoha
