#include "SchoolDaysPresencePolicy.hpp"

#include <algorithm>
#include <array>
#include <iterator>

namespace SchoolDays {
namespace {
constexpr std::string_view kGeneric = "Kotonoha Engine";

#include "SchoolDaysPresenceSceneCatalog.inc"

void hashByte(std::uint64_t &value, unsigned char byte) noexcept {
  value = (value ^ byte) * UINT64_C(1099511628211);
}

void hashText(std::uint64_t &value, std::string_view text) noexcept {
  for (unsigned char c : text) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<unsigned char>(c - 'A' + 'a');
    hashByte(value, c == '\\' ? '/' : c);
  }
  hashByte(value, 0);
}

void hashTick(std::uint64_t &value, std::uint64_t tick) noexcept {
  for (unsigned i = 0; i < 8; ++i) {
    hashByte(value, static_cast<unsigned char>(tick & 255u));
    tick >>= 8;
  }
}

enum class Locale : unsigned { En, Pt, Es, Ru, Zh };
Locale localeFor(std::string_view locale) noexcept {
  if (locale == "pt-BR" || locale == "pt-br") return Locale::Pt;
  if (locale == "es") return Locale::Es;
  if (locale == "ru") return Locale::Ru;
  if (locale == "zh-CN" || locale == "zh-cn") return Locale::Zh;
  return Locale::En;
}

using Texts = std::array<std::string_view, 10>;
constexpr std::array<Texts, 5> kTexts{{
    {{"Main Menu", "Options", "Route Map", "Making choices",
      "Episode 01", "Episode 02", "Episode 03", "Episode 04",
      "Episode 05", "Episode 06"}},
    {{"Menu principal", "Opções", "Mapa de rotas", "Fazendo escolhas",
      "Episódio 01", "Episódio 02", "Episódio 03", "Episódio 04",
      "Episódio 05", "Episódio 06"}},
    {{"Menú principal", "Opciones", "Mapa de rutas", "Tomando decisiones",
      "Episodio 01", "Episodio 02", "Episodio 03", "Episodio 04",
      "Episodio 05", "Episodio 06"}},
    {{"Главное меню", "Настройки", "Карта маршрутов", "Принятие решений",
      "Эпизод 01", "Эпизод 02", "Эпизод 03", "Эпизод 04",
      "Эпизод 05", "Эпизод 06"}},
    {{"主菜单", "设置", "路线图", "正在做出选择",
      "第 01 话", "第 02 话", "第 03 话", "第 04 话",
      "第 05 话", "第 06 话"}},
}};

// The ENDG records themselves have no title labels. Publish only names
// independently authenticated by the existing QA records. Other codes stay
// generic until the complete localized title catalog is verified.
std::string_view endingName(int code, Locale locale) noexcept {
  if (locale != Locale::En)
    return kGeneric;
  switch (code) {
  case 5: return "Behind the Scenes";
  case 6: return "Blue Smile";
  case 7: return "Cold Scarf";
  case 8: return "Braided Hair";
  case 10: return "The End of Madness";
  case 15: return "Light";
  case 17: return "Innocent Sunset";
  default: return kGeneric;
  }
}
} // namespace

PresenceScene classifyPresenceScene(std::string_view key,
                                    const Kotonoha_orsData &script) noexcept {
  if (key.size() < 3 || key[0] != '0' || key[1] < '0' || key[1] > '5' ||
      key[2] != '/' || !script.data)
    return {};
  std::uint64_t value = UINT64_C(14695981039346656037);
  hashText(value, key);
  bool credits = false;
  bool visual = false;
  for (const auto *event = script.data; event; event = event->next) {
    const char *path = nullptr;
    unsigned char tag = 0;
    switch (event->command) {
    case CREATE_BG:
      tag = 'b';
      if (event->data.create_bg) path = event->data.create_bg->path;
      break;
    case PLAY_MOVIE:
      tag = 'm';
      if (event->data.play_movie) path = event->data.play_movie->path;
      break;
    case END_ROLL:
      tag = 'e';
      credits = true;
      if (event->data.path_end) path = event->data.path_end->path;
      break;
    case UNKNOWN:
      return {};
    default:
      continue;
    }
    if (!path || !*path)
      return {};
    visual = true;
    hashByte(value, tag);
    hashText(value, path);
    hashTick(value, event->startTick);
    hashTick(value, event->endTick);
  }
  if (!visual || !std::binary_search(std::begin(kPublicVisualSignatures),
                                     std::end(kPublicVisualSignatures), value))
    return {};
  return {true, key[1] - '0' + 1, credits};
}

std::string_view resolvePresence(const PresenceState &state,
                                 std::string_view locale) noexcept {
  if (state.protectedState)
    return kGeneric;
  const auto language = localeFor(locale);
  const auto &text = kTexts[static_cast<unsigned>(language)];
  if (state.menu)
    return state.options ? text[1] : text[0];
  if (!state.scene.verified || state.scene.episode < 1 ||
      state.scene.episode > 6)
    return kGeneric;
  if (state.finalWindow)
    return endingName(state.endingCode, language);
  if (state.scene.credits)
    return kGeneric;
  if (state.options) return text[1];
  if (state.routeMap) return text[2];
  if (state.choice) return text[3];
  return text[3 + state.scene.episode];
}
} // namespace SchoolDays
