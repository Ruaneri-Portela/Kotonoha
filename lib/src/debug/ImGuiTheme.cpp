#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/debug/ImGuiTheme.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace Kotonoha::ImGuiTheme {
namespace {
// Guia visual Kotonoha v2 — variante "Vivid" exclusiva do ImGui.
// Mesma hierarquia (fundo -> superficie -> acento) da web/Android/SwiftUI,
// agora com as cores oficiais de design/tokens.json: navy/raised nas
// superficies, indigo nos botoes comuns e violet/magenta/sakura nos acentos.
// Ciano aparece apenas no cursor de navegacao e magenta/sakura nos detalhes,
// para dar vida ao launcher sem competir com o monograma K e as sakura.
constexpr ImVec4 Accent{0.416f, 0.357f, 0.906f, 1.00f};        // #6A5BE7 violet
constexpr ImVec4 AccentSoft{0.557f, 0.510f, 0.941f, 1.00f};    // #8E82F0
constexpr ImVec4 AccentDim{0.318f, 0.278f, 0.659f, 1.00f};     // #5147A8
constexpr ImVec4 AccentHover{0.655f, 0.608f, 0.969f, 1.00f};   // #A79BF7
constexpr ImVec4 Magenta{0.784f, 0.420f, 0.851f, 1.00f};       // #C86BD9
constexpr ImVec4 Sakura{1.000f, 0.561f, 0.741f, 1.00f};        // #FF8FBD
constexpr ImVec4 Cyan{0.000f, 0.839f, 1.000f, 1.00f};          // #00D6FF
constexpr ImVec4 Success{0.278f, 0.843f, 0.631f, 1.00f};       // #47D7A1
constexpr ImVec4 Warning{1.000f, 0.745f, 0.408f, 1.00f};       // #FFBE68
constexpr ImVec4 Error{1.000f, 0.451f, 0.537f, 1.00f};         // #FF7389
constexpr ImVec4 WindowBg{0.063f, 0.094f, 0.192f, 1.00f};      // #101831
constexpr ImVec4 ChildBg{0.094f, 0.141f, 0.286f, 1.00f};       // #182449 raised
constexpr ImVec4 SurfaceRaised{0.125f, 0.180f, 0.337f, 1.00f}; // #202E56
constexpr ImVec4 PopupBg{0.078f, 0.125f, 0.263f, 1.00f};       // #142043
constexpr ImVec4 Border{0.227f, 0.227f, 0.420f, 1.00f};        // #3A3A6B
constexpr ImVec4 BorderHovered{0.420f, 0.420f, 0.678f, 1.00f}; // #6B6BAD
constexpr ImVec4 TableBorder{0.290f, 0.290f, 0.502f, 1.00f};   // #4A4A80
constexpr ImVec4 FrameBg{0.125f, 0.180f, 0.337f, 1.00f};       // #202E56
constexpr ImVec4 FrameBgHovered{0.169f, 0.227f, 0.412f, 1.00f}; // #2B3A69
constexpr ImVec4 FrameBgActive{0.212f, 0.278f, 0.498f, 1.00f};  // #36477F
constexpr ImVec4 TitleBg{0.063f, 0.094f, 0.192f, 1.00f};
constexpr ImVec4 TitleBgActive{0.094f, 0.141f, 0.286f, 1.00f};
constexpr ImVec4 TitleBgCollapsed{0.063f, 0.094f, 0.192f, 1.00f};
constexpr ImVec4 MenuBarBg{0.094f, 0.141f, 0.286f, 1.00f};
constexpr ImVec4 ScrollbarBg{0.063f, 0.094f, 0.192f, 1.00f};
constexpr ImVec4 ScrollbarGrab{0.212f, 0.278f, 0.498f, 1.00f};
constexpr ImVec4 ScrollbarGrabHovered{0.290f, 0.353f, 0.627f, 1.00f}; // #4A5AA0
constexpr ImVec4 ScrollbarGrabActive{0.416f, 0.357f, 0.906f, 1.00f};
constexpr ImVec4 Header{0.165f, 0.212f, 0.400f, 1.00f};        // #2A3666
constexpr ImVec4 HeaderHovered{0.212f, 0.267f, 0.494f, 1.00f}; // #36447E
constexpr ImVec4 HeaderActive{0.259f, 0.322f, 0.604f, 1.00f};  // #42529A
constexpr ImVec4 Separator{0.227f, 0.227f, 0.420f, 1.00f};
constexpr ImVec4 SeparatorHovered{0.318f, 0.278f, 0.659f, 1.00f};
constexpr ImVec4 SeparatorActive{0.416f, 0.357f, 0.906f, 1.00f};
constexpr ImVec4 Text{0.941f, 0.949f, 1.000f, 1.00f};          // #F0F2FF
constexpr ImVec4 TextDisabled{0.725f, 0.710f, 0.831f, 1.00f};  // #B9B5D4 muted
constexpr ImVec4 TextSelectedBg{0.416f, 0.357f, 0.906f, 0.28f};
constexpr ImVec4 Button{0.173f, 0.243f, 0.604f, 1.00f};        // #2C3E9A indigo
constexpr ImVec4 ButtonHovered{0.227f, 0.310f, 0.722f, 1.00f}; // #3A4FB8
constexpr ImVec4 ButtonActive{0.290f, 0.380f, 0.831f, 1.00f};  // #4A61D4
constexpr ImVec4 ButtonText{1.000f, 1.000f, 1.000f, 1.00f};    // branco
constexpr ImVec4 CardBg{0.102f, 0.149f, 0.286f, 1.00f};        // #1A2649
constexpr ImVec4 CardBorder{0.227f, 0.227f, 0.420f, 1.00f};
constexpr ImVec4 TabBg{0.102f, 0.149f, 0.286f, 1.00f};
constexpr ImVec4 TabHovered{0.165f, 0.212f, 0.400f, 1.00f};
constexpr std::size_t kMinFontBytes = 101;
constexpr std::size_t kMaxFontBytes = 256u << 20;

std::uint16_t readBe16(const std::uint8_t *data, std::size_t offset) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(data[offset]) << 8) |
      static_cast<std::uint16_t>(data[offset + 1]));
}

std::uint32_t readBe32(const std::uint8_t *data, std::size_t offset) {
  return (static_cast<std::uint32_t>(data[offset]) << 24) |
         (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
         static_cast<std::uint32_t>(data[offset + 3]);
}

bool tagEquals(const std::uint8_t *data, const char *tag) {
  return std::memcmp(data, tag, 4) == 0;
}

bool hasSfntTag(const std::uint8_t *data) {
  return tagEquals(data, "\x00\x01\x00\x00") || tagEquals(data, "OTTO") ||
         tagEquals(data, "true") || tagEquals(data, "1\x00\x00\x00");
}

// Guards stb_truetype against out-of-bounds reads on truncated files before
// letting ImGui's active font loader validate format compatibility.
bool hasValidSfntContainerBounds(const std::uint8_t *data, std::size_t size) {
  if (!data || size < kMinFontBytes)
    return false;

  std::size_t fontStart = 0;
  if (tagEquals(data, "ttcf")) {
    if (size < 16)
      return false;
    const std::uint32_t version = readBe32(data, 4);
    const std::uint32_t numFonts = readBe32(data, 8);
    if ((version != 0x00010000u && version != 0x00020000u) || numFonts == 0 ||
        numFonts > (size - 12) / 4)
      return false;
    fontStart = readBe32(data, 12);
  } else if (!hasSfntTag(data)) {
    return false;
  }

  if (fontStart > size || size - fontStart < 12 ||
      !hasSfntTag(data + fontStart))
    return false;

  const std::uint16_t numTables = readBe16(data, fontStart + 4);
  if (numTables == 0 ||
      static_cast<std::size_t>(numTables) * 16 > size - fontStart - 12)
    return false;

  for (std::uint16_t i = 0; i < numTables; ++i) {
    const std::size_t rec = fontStart + 12 + static_cast<std::size_t>(i) * 16;
    const std::uint32_t tableOffset = readBe32(data, rec + 8);
    const std::uint32_t tableLength = readBe32(data, rec + 12);
    if (tableOffset > size || tableLength > size - tableOffset)
      return false;
    if (tagEquals(data + rec, "head") && tableLength < 54)
      return false;
    if (tagEquals(data + rec, "maxp") && tableLength < 6)
      return false;
    if (tagEquals(data + rec, "hhea") && tableLength < 36)
      return false;
    if (tagEquals(data + rec, "cmap")) {
      if (tableLength < 4)
        return false;
      const std::uint16_t subtables = readBe16(data, tableOffset + 2);
      if (4 + static_cast<std::size_t>(subtables) * 8 > tableLength)
        return false;
      for (std::uint16_t j = 0; j < subtables; ++j) {
        const std::size_t enc =
            tableOffset + 4 + static_cast<std::size_t>(j) * 8;
        const std::uint32_t subOffset = readBe32(data, enc + 4);
        if (subOffset >= tableLength || tableLength - subOffset < 6)
          return false;
      }
    }
  }
  return true;
}

struct ErrorRecoveryGuard {
  ImGuiIO &io;
  bool enableAssert;
  bool enableDebugLog;
  bool enableTooltip;

  explicit ErrorRecoveryGuard(ImGuiIO &target)
      : io(target), enableAssert(target.ConfigErrorRecoveryEnableAssert),
        enableDebugLog(target.ConfigErrorRecoveryEnableDebugLog),
        enableTooltip(target.ConfigErrorRecoveryEnableTooltip) {
    io.ConfigErrorRecoveryEnableAssert = false;
    io.ConfigErrorRecoveryEnableDebugLog = false;
    io.ConfigErrorRecoveryEnableTooltip = false;
  }

  ~ErrorRecoveryGuard() {
    io.ConfigErrorRecoveryEnableAssert = enableAssert;
    io.ConfigErrorRecoveryEnableDebugLog = enableDebugLog;
    io.ConfigErrorRecoveryEnableTooltip = enableTooltip;
  }
};

ImFont *tryLoadFontFile(ImGuiIO &io, const std::filesystem::path &resolved) {
  std::error_code ec;
  if (resolved.empty() || !std::filesystem::exists(resolved, ec) ||
      !std::filesystem::is_regular_file(resolved, ec))
    return nullptr;

  const auto fileSize = std::filesystem::file_size(resolved, ec);
  if (ec || fileSize < kMinFontBytes || fileSize > kMaxFontBytes ||
      fileSize >
          static_cast<std::uintmax_t>((std::numeric_limits<int>::max)())) {
    SDL_LogWarn(LogUI, "Skipping invalid ImGui font file: %s",
                resolved.u8string().c_str());
    return nullptr;
  }

  std::vector<std::uint8_t> bytes;
  try {
    bytes = Filesystem::readLocalFile(resolved, kMaxFontBytes);
  } catch (const std::exception &e) {
    SDL_LogWarn(LogUI, "Failed to read ImGui font '%s': %s",
                resolved.u8string().c_str(), e.what());
    return nullptr;
  }

  if (bytes.size() < kMinFontBytes ||
      bytes.size() >
          static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
      !hasValidSfntContainerBounds(bytes.data(), bytes.size())) {
    SDL_LogWarn(LogUI, "Skipping truncated or malformed ImGui font: %s",
                resolved.u8string().c_str());
    return nullptr;
  }

  std::unique_ptr<void, void (*)(void *)> owned(ImGui::MemAlloc(bytes.size()),
                                                &ImGui::MemFree);
  if (!owned)
    return nullptr;
  std::memcpy(owned.get(), bytes.data(), bytes.size());

  ImFontConfig cfg;
  cfg.FontDataOwnedByAtlas = true;
  cfg.Flags |= ImFontFlags_NoLoadError;
  const std::string name = resolved.filename().u8string();
  std::snprintf(cfg.Name, sizeof(cfg.Name), "%s", name.c_str());

  ErrorRecoveryGuard guard(io);
  ImFont *loaded = io.Fonts->AddFontFromMemoryTTF(
      owned.release(), static_cast<int>(bytes.size()), 18.0f, &cfg);
  if (!loaded)
    SDL_LogWarn(LogUI, "Skipping ImGui-incompatible font: %s",
                resolved.u8string().c_str());
  return loaded;
}
} // namespace

bool loadFontsFromPaths(const std::filesystem::path &customPath,
                        const std::filesystem::path &defaultPath) {
  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->Clear();
  ImFont *font = nullptr;
  if (!customPath.empty())
    font = tryLoadFontFile(io, customPath);
  if (!font && !defaultPath.empty() && defaultPath != customPath)
    font = tryLoadFontFile(io, defaultPath);
  const bool loadedFromFile = (font != nullptr);
  if (!font)
    font = io.Fonts->AddFontDefaultVector();
  io.FontDefault = font;
  return loadedFromFile;
}

void loadFonts(const std::string &fontPath) {
  std::filesystem::path customResolved;
  std::filesystem::path defaultResolved;
  if (!fontPath.empty()) {
    try {
      customResolved = FileResolver::font(fontPath);
    } catch (...) {
      // A bad optional UI font must not make the engine unusable.
    }
  }
  try {
    defaultResolved = FileResolver::font(defaultFontName());
  } catch (...) {
    // A bad optional UI font must not make the engine unusable.
  }
  loadFontsFromPaths(customResolved, defaultResolved);
}

void apply(float value) {
  const float scale = std::clamp(value, 0.5f, 4.0f);
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();

  // Geometry (scales with UI scale). Cantos um pouco mais arredondados e
  // respiracao maior deixam o tema mais calmo sem perder densidade.
  style.WindowPadding = {18.0f * scale, 16.0f * scale};
  style.WindowRounding = 14.0f * scale;
  style.WindowBorderSize = 1.0f * scale;
  style.ChildRounding = 12.0f * scale;
  style.ChildBorderSize = 1.0f * scale;
  style.PopupRounding = 12.0f * scale;
  style.PopupBorderSize = 1.0f * scale;
  style.FramePadding = {16.0f * scale, 9.0f * scale};
  style.FrameRounding = 10.0f * scale;
  style.FrameBorderSize = 1.0f * scale;
  style.ItemSpacing = {14.0f * scale, 10.0f * scale};
  style.ItemInnerSpacing = {10.0f * scale, 8.0f * scale};
  style.IndentSpacing = 22.0f * scale;
  style.ScrollbarSize = 14.0f * scale;
  style.ScrollbarRounding = 8.0f * scale;
  style.GrabMinSize = 14.0f * scale;
  style.GrabRounding = 6.0f * scale;
  style.TabRounding = 10.0f * scale;
  style.SeparatorTextBorderSize = 1.0f * scale;
  style.SeparatorTextPadding = {28.0f * scale, 4.0f * scale};
  style.ButtonTextAlign = {0.5f, 0.5f};
  style.DisplayWindowPadding = {24.0f * scale, 24.0f * scale};
  style.DisplaySafeAreaPadding = {4.0f * scale, 4.0f * scale};
  style.FontSizeBase = 16.0f * scale;

  // Colors.
  style.Colors[ImGuiCol_Text] = Text;
  style.Colors[ImGuiCol_TextDisabled] = TextDisabled;
  style.Colors[ImGuiCol_TextSelectedBg] = TextSelectedBg;
  style.Colors[ImGuiCol_WindowBg] = WindowBg;
  style.Colors[ImGuiCol_ChildBg] = ChildBg;
  style.Colors[ImGuiCol_PopupBg] = PopupBg;
  style.Colors[ImGuiCol_Border] = Border;
  style.Colors[ImGuiCol_BorderShadow] = {0.0f, 0.0f, 0.0f, 0.25f};
  style.Colors[ImGuiCol_FrameBg] = FrameBg;
  style.Colors[ImGuiCol_FrameBgHovered] = FrameBgHovered;
  style.Colors[ImGuiCol_FrameBgActive] = FrameBgActive;
  style.Colors[ImGuiCol_TitleBg] = TitleBg;
  style.Colors[ImGuiCol_TitleBgActive] = TitleBgActive;
  style.Colors[ImGuiCol_TitleBgCollapsed] = TitleBgCollapsed;
  style.Colors[ImGuiCol_MenuBarBg] = MenuBarBg;
  style.Colors[ImGuiCol_ScrollbarBg] = ScrollbarBg;
  style.Colors[ImGuiCol_ScrollbarGrab] = ScrollbarGrab;
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = ScrollbarGrabHovered;
  style.Colors[ImGuiCol_ScrollbarGrabActive] = ScrollbarGrabActive;
  style.Colors[ImGuiCol_CheckMark] = Accent;
  style.Colors[ImGuiCol_CheckboxSelectedBg] = {Accent.x, Accent.y, Accent.z,
                                               0.24f};
  style.Colors[ImGuiCol_SliderGrab] = AccentSoft;
  style.Colors[ImGuiCol_SliderGrabActive] = Accent;
  style.Colors[ImGuiCol_Button] = Button;
  style.Colors[ImGuiCol_ButtonHovered] = ButtonHovered;
  style.Colors[ImGuiCol_ButtonActive] = ButtonActive;
  style.Colors[ImGuiCol_Header] = Header;
  style.Colors[ImGuiCol_HeaderHovered] = HeaderHovered;
  style.Colors[ImGuiCol_HeaderActive] = HeaderActive;
  style.Colors[ImGuiCol_Separator] = Separator;
  style.Colors[ImGuiCol_SeparatorHovered] = SeparatorHovered;
  style.Colors[ImGuiCol_SeparatorActive] = SeparatorActive;
  style.Colors[ImGuiCol_ResizeGrip] = {AccentDim.x, AccentDim.y, AccentDim.z,
                                       0.45f};
  style.Colors[ImGuiCol_ResizeGripHovered] = {Accent.x, Accent.y, Accent.z,
                                              0.60f};
  style.Colors[ImGuiCol_ResizeGripActive] = {Magenta.x, Magenta.y, Magenta.z,
                                             0.70f};
  style.Colors[ImGuiCol_InputTextCursor] = Accent;
  style.Colors[ImGuiCol_Tab] = TabBg;
  style.Colors[ImGuiCol_TabHovered] = TabHovered;
  style.Colors[ImGuiCol_TabSelected] = Accent;
  style.Colors[ImGuiCol_TabSelectedOverline] = Sakura;
  style.Colors[ImGuiCol_TabDimmed] = ChildBg;
  style.Colors[ImGuiCol_TabDimmedSelected] = AccentDim;
  style.Colors[ImGuiCol_TabDimmedSelectedOverline] = AccentSoft;
  style.Colors[ImGuiCol_PlotLines] = AccentSoft;
  style.Colors[ImGuiCol_PlotLinesHovered] = Magenta;
  style.Colors[ImGuiCol_PlotHistogram] = Accent;
  style.Colors[ImGuiCol_PlotHistogramHovered] = Magenta;
  style.Colors[ImGuiCol_TextLink] = Accent;
  style.Colors[ImGuiCol_TableHeaderBg] = Header;
  // Bordas navy/violet: o ciano fica restrito ao foco de navegacao.
  style.Colors[ImGuiCol_TableBorderStrong] = TableBorder;
  style.Colors[ImGuiCol_TableBorderLight] = Border;
  style.Colors[ImGuiCol_TableRowBg] = {0.0f, 0.0f, 0.0f, 0.0f};
  style.Colors[ImGuiCol_TableRowBgAlt] = {Accent.x, Accent.y, Accent.z, 0.10f};
  style.Colors[ImGuiCol_DragDropTarget] = Magenta;
  style.Colors[ImGuiCol_NavCursor] = Cyan;
}

ImVec4 accent() { return Accent; }
ImVec4 success() { return Success; }
ImVec4 warning() { return Warning; }
ImVec4 error() { return Error; }
ImVec4 muted() { return TextDisabled; }

void statusText(const char *text, bool isError) {
  ImGui::TextColored(isError ? Error : Success, "%s", text ? text : "");
}

bool primaryButton(const char *label, ImVec2 size) {
  // Violeta da marca com texto branco; magenta fica nos detalhes ao redor.
  ImGui::PushStyleColor(ImGuiCol_Button, Accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentSoft);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentHover);
  ImGui::PushStyleColor(ImGuiCol_Text, ButtonText);
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return pressed;
}

// Desenha um cabeçalho de seção com um pequeno ponto de acento à esquerda.
void sectionHeader(const char *label) {
  ImGui::SeparatorText(label);
  const auto &style = ImGui::GetStyle();
  const float x = ImGui::GetCursorScreenPos().x - style.ItemSpacing.x;
  const float y = ImGui::GetCursorScreenPos().y -
                  ImGui::GetTextLineHeight() * 0.5f -
                  style.ItemSpacing.y * 0.5f;
  ImGui::GetWindowDrawList()->AddRectFilled(
      ImVec2(x, y), ImVec2(x + 4.0f, y + ImGui::GetTextLineHeight()),
      ImGui::GetColorU32(Accent), 2.0f);
}

// Grupo com rótulo alinhado à esquerda e preenchimento à direita.
struct LabeledGroup {
  explicit LabeledGroup(const char *label) {
    ImGui::PushStyleColor(ImGuiCol_Text, TextDisabled);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::BeginGroup();
  }
  ~LabeledGroup() { ImGui::EndGroup(); }
};

// --- Modern UI helpers ---

void cardBegin(const char *id, bool collapsible, bool *open) {
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, CardBg);
  ImGui::PushStyleColor(ImGuiCol_Border, CardBorder);
  ImGuiWindowFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_FrameStyle;
  if (collapsible && open)
    flags |= ImGuiChildFlags_None;
  ImGui::BeginChild(id, ImVec2(0, 0), flags);
}

void cardEnd() {
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

void badge(const char *label, ImVec4 color) {
  // Draw the actual pill rather than pushing ImGuiCol_FrameBg around plain
  // text (TextUnformatted never uses FrameBg). Keep it sized to its label.
  const char *value = label ? label : "";
  const auto text = ImGui::CalcTextSize(value);
  const float scale = ImGui::GetStyle().FontSizeBase / 16.0f;
  const ImVec2 pad{8.f * scale, 4.f * scale};
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy({text.x + 2 * pad.x, text.y + 2 * pad.y});
  auto *draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(pos,
                      {pos.x + text.x + 2 * pad.x, pos.y + text.y + 2 * pad.y},
                      ImGui::GetColorU32(color), 7.0f * scale);
  // Texto escuro sobre o preenchimento pastel: legivel sem branco puro.
  draw->AddText({pos.x + pad.x, pos.y + pad.y}, ImGui::GetColorU32(WindowBg),
                value);
}

void valueRow(const char *label, const char *value, ImVec4 valueColor) {
  ImGui::PushStyleColor(ImGuiCol_Text, TextDisabled);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  ImGui::SameLine();
  if (valueColor.x == 0.f && valueColor.y == 0.f && valueColor.z == 0.f &&
      valueColor.w == 0.f)
    ImGui::TextUnformatted(value);
  else
    ImGui::TextColored(valueColor, "%s", value);
}

bool toggleSwitch(const char *label, bool *v) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0.f, 0.f});
  const float height = 20.f;
  const float width = 40.f;
  const float thumbSize = 16.f;
  const ImVec2 labelSize = ImGui::CalcTextSize(label);
  ImVec2 cursor = ImGui::GetCursorScreenPos();
  ImDrawList *draw = ImGui::GetWindowDrawList();

  // InvisibleButton returns true for mouse, keyboard and gamepad nav
  // activation, unlike the previous mouse-only hover/click check.
  const bool clicked =
      ImGui::InvisibleButton(label, ImVec2(width + 10.f + labelSize.x, height));
  if (clicked)
    *v = !*v;

  // Violeta no estado ligado; polegar branco sobre o violeta, claro no slate.
  const bool hovered = ImGui::IsItemHovered();
  const ImVec4 bgColor =
      *v ? (hovered ? Accent : AccentSoft) : (hovered ? FrameBgHovered : FrameBg);
  const ImVec4 thumbColor = *v ? ButtonText : TextDisabled;

  draw->AddRectFilled(cursor, {cursor.x + width, cursor.y + height},
                      ImGui::GetColorU32(bgColor), height * 0.5f);
  const float thumbX = cursor.x + (*v ? width - thumbSize - 2.f : 2.f);
  draw->AddRectFilled({thumbX, cursor.y + 2.f},
                      {thumbX + thumbSize, cursor.y + height - 2.f},
                      ImGui::GetColorU32(thumbColor), (height - 4.f) * 0.5f);

  ImGui::SetCursorScreenPos({cursor.x + width + 10.f, cursor.y - 2.f});
  ImGui::TextUnformatted(label);
  ImGui::PopStyleVar();
  return clicked;
}

bool iconButton(const char *icon, const char *label, ImVec2 size) {
  ImGui::PushStyleColor(ImGuiCol_Button, SurfaceRaised);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HeaderHovered);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, HeaderActive);
  std::string fullLabel = std::string(icon) + "  " + label;
  const bool pressed = ImGui::Button(fullLabel.c_str(), size);
  ImGui::PopStyleColor(3);
  return pressed;
}

void kotonohaHeader(const char *title, const char *subtitle, float scale) {
  const float band = 80.f * scale;
  const ImVec2 origin = ImGui::GetWindowPos();
  auto *draw = ImGui::GetWindowDrawList();
  // Faixa plana em raised; um fio violet -> magenta -> sakura da o tom vivo
  // sem transformar o cabecalho em um gradiente competitivo.
  draw->AddRectFilled(origin,
                      {origin.x + ImGui::GetWindowSize().x, origin.y + band},
                      ImGui::GetColorU32(SurfaceRaised));
  draw->AddRectFilledMultiColor(
      {origin.x, origin.y + band - 2.0f * scale},
      {origin.x + ImGui::GetWindowSize().x, origin.y + band},
      ImGui::GetColorU32(ImVec4(Accent.x, Accent.y, Accent.z, 0.85f)),
      ImGui::GetColorU32(ImVec4(Magenta.x, Magenta.y, Magenta.z, 0.85f)),
      ImGui::GetColorU32(ImVec4(Sakura.x, Sakura.y, Sakura.z, 0.85f)),
      ImGui::GetColorU32(ImVec4(Accent.x, Accent.y, Accent.z, 0.85f)));
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
  ImGui::TextColored(Text, "%s", title);
  ImGui::PopFont();
  ImGui::TextDisabled("%s", subtitle ? subtitle : "");
  ImGui::SetCursorScreenPos(
      {ImGui::GetCursorScreenPos().x,
       std::max(ImGui::GetCursorScreenPos().y, origin.y + band) +
           10.f * scale});
}

#if KOTONOHA_ENABLE_UPSCALER
struct ShaderFilePicker::State {
  std::mutex mutex;
  bool done = false;
  std::vector<std::string> files;
  std::string error;
};

void SDLCALL ShaderFilePicker::finished(void *userdata,
                                        const char *const *files, int) {
  std::unique_ptr<std::shared_ptr<State>> owner(
      static_cast<std::shared_ptr<State> *>(userdata));
  auto state = *owner;
  std::lock_guard lock(state->mutex);
  if (!files) {
    state->error = SDL_GetError();
  } else {
    for (size_t i = 0; files[i]; ++i)
      state->files.emplace_back(files[i]);
  }
  state->done = true;
}

void ShaderFilePicker::browse(SDL_Window *window) {
  if (state)
    return;
  auto next = std::make_shared<State>();
  state = next;
  error.clear();
  static const SDL_DialogFileFilter filters[] = {
      {"MPV shader files", "glsl;hook"}, {"All files", "*"}};
  SDL_ShowOpenFileDialog(finished, new std::shared_ptr<State>(next), window,
                         filters, 2, nullptr, true);
}

void ShaderFilePicker::poll(UpscaleSettings &settings) {
  if (!state)
    return;
  bool done = false;
  std::vector<std::string> files;
  std::string failure;
  {
    std::lock_guard lock(state->mutex);
    done = state->done;
    if (done) {
      files = state->files;
      failure = state->error;
    }
  }
  if (!done)
    return;
  state.reset();
  error = std::move(failure);
  if (!error.empty() || files.empty())
    return;
  try {
    settings.shaderFiles = UpscaleShaderFileList(files);
  } catch (const std::exception &e) {
    error = e.what();
  }
}

bool ShaderFilePicker::draw(SDL_Window *window, UpscaleSettings &settings) {
  const std::string before = settings.shaderFiles;
  poll(settings);
  const auto files = UpscaleShaderFiles(settings.shaderFiles);
  ImGui::Text("Custom shaders: %zu", files.size());
  for (size_t i = 0; i < files.size(); ++i)
    ImGui::BulletText("%s", files[i].c_str());
  if (ImGui::Button("Choose shader files..."))
    browse(window);
  if (!files.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Clear shaders"))
      settings.shaderFiles.clear();
  }
  if (!error.empty())
    ImGui::TextColored(Error, "%s", error.c_str());
  return settings.shaderFiles != before;
}

#endif // KOTONOHA_ENABLE_UPSCALER

} // namespace Kotonoha::ImGuiTheme
