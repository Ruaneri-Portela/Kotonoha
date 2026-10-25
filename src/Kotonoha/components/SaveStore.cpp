#include <Kotonoha/components/SaveStore.hpp>

#include <SDL3/SDL.h>

#include <fstream>
#include <sstream>

namespace Kotonoha {

std::string SaveStore::Path(int slot) const {
  char *pref = SDL_GetPrefPath("Kotonoha", gameId_.c_str());
  std::string dir = pref != nullptr ? pref : "";
  SDL_free(pref);
  return dir + "slot" + std::to_string(slot) + ".sav";
}

bool SaveStore::Write(int slot, const std::string &label,
                      const std::vector<int> &inputs) const {
  if (slot < 0 || slot >= slots_) return false;
  std::ofstream f(Path(slot), std::ios::trunc);
  if (!f) return false;
  f << label << '\n';
  for (int v : inputs) f << v << ' ';
  f << '\n';
  return static_cast<bool>(f);
}

bool SaveStore::Read(int slot, std::string *label, std::vector<int> *inputs) const {
  if (slot < 0 || slot >= slots_) return false;
  std::ifstream f(Path(slot));
  std::string l, nums;
  if (!f || !std::getline(f, l)) return false;
  std::getline(f, nums);
  if (label != nullptr) *label = l;
  if (inputs != nullptr) {
    inputs->clear();
    std::istringstream in(nums);
    int v;
    while (in >> v) inputs->push_back(v);
  }
  return true;
}

bool SaveStore::Exists(int slot) const { return Read(slot, nullptr, nullptr); }

bool SaveStore::AnySave() const {
  for (int i = 0; i < slots_; ++i)
    if (Exists(i)) return true;
  return false;
}

std::string SaveStore::Label(int slot) const {
  std::string l;
  return Read(slot, &l, nullptr) ? l : std::string();
}

std::string SaveStore::MakeLabel(const std::string &text, size_t maxCodepoints) {
  std::string out;
  size_t count = 0;
  for (size_t i = 0; i < text.size() && text[i] != '\n'; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if ((c & 0xC0) != 0x80) {
      if (count == maxCodepoints) break;
      ++count;
    }
    out.push_back(static_cast<char>(c));
  }
  return out;
}

} // namespace Kotonoha
