#pragma once
#include <string>
#include <vector>

namespace Kotonoha {

// Per-game save slots. A save is a label plus the list of player inputs that
// reproduce the position from a fresh start (deterministic replay), which
// keeps the format independent of each game's internal VM state.
class SaveStore {
public:
  static constexpr int kSlots = 3;

  explicit SaveStore(std::string gameId) : gameId_(std::move(gameId)) {}

  bool Write(int slot, const std::string &label, const std::vector<int> &inputs) const;
  bool Read(int slot, std::string *label, std::vector<int> *inputs) const;
  bool Exists(int slot) const;
  bool AnySave() const;
  std::string Label(int slot) const; // empty when the slot is free
  // First line of text cut to a number of UTF-8 code points, for slot labels.
  static std::string MakeLabel(const std::string &text, size_t maxCodepoints);

private:
  std::string Path(int slot) const;

  std::string gameId_;
};

} // namespace Kotonoha
