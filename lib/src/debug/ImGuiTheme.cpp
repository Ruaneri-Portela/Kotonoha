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
// Paleta "noir" moderna com acento violeta-vivo — fundos profundos de baixa
// saturação, contraste alto no texto e acentos vibrantes. Bordas arredondadas
// e sombreamento sutil dão aspecto de interface contemporânea.
constexpr ImVec4 Accent{0.90f, 0.60f, 1.00f, 1.00f};
constexpr ImVec4 AccentDim{0.55f, 0.30f, 0.70f, 1.00f};
constexpr ImVec4 AccentHover{1.00f, 0.75f, 1.00f, 1.00f};
constexpr ImVec4 Success{0.40f, 0.88f, 0.60f, 1.00f};
constexpr ImVec4 Warning{1.00f, 0.70f, 0.35f, 1.00f};
constexpr ImVec4 Error{1.00f, 0.45f, 0.50f, 1.00f};
constexpr ImVec4 WindowBg{0.070f, 0.060f, 0.095f, 1.00f};
constexpr ImVec4 ChildBg{0.090f, 0.080f, 0.120f, 1.00f};
constexpr ImVec4 PopupBg{0.085f, 0.075f, 0.115f, 1.00f};
constexpr ImVec4 Border{0.18f, 0.15f, 0.24f, 1.00f};
constexpr ImVec4 BorderHovered{0.35f, 0.28f, 0.45f, 1.00f};
constexpr ImVec4 FrameBg{0.12f, 0.10f, 0.16f, 1.00f};
constexpr ImVec4 FrameBgHovered{0.18f, 0.15f, 0.24f, 1.00f};
constexpr ImVec4 FrameBgActive{0.26f, 0.20f, 0.34f, 1.00f};
constexpr ImVec4 TitleBg{0.090f, 0.080f, 0.120f, 1.00f};
constexpr ImVec4 TitleBgActive{0.12f, 0.10f, 0.16f, 1.00f};
constexpr ImVec4 TitleBgCollapsed{0.070f, 0.060f, 0.095f, 1.00f};
constexpr ImVec4 MenuBarBg{0.090f, 0.080f, 0.120f, 1.00f};
constexpr ImVec4 ScrollbarBg{0.090f, 0.080f, 0.120f, 1.00f};
constexpr ImVec4 ScrollbarGrab{0.24f, 0.20f, 0.30f, 1.00f};
constexpr ImVec4 ScrollbarGrabHovered{0.38f, 0.30f, 0.45f, 1.00f};
constexpr ImVec4 ScrollbarGrabActive{0.52f, 0.40f, 0.60f, 1.00f};
constexpr ImVec4 Header{0.14f, 0.11f, 0.19f, 1.00f};
constexpr ImVec4 HeaderHovered{0.22f, 0.18f, 0.28f, 1.00f};
constexpr ImVec4 HeaderActive{0.32f, 0.25f, 0.42f, 1.00f};
constexpr ImVec4 Separator{0.18f, 0.15f, 0.24f, 1.00f};
constexpr ImVec4 SeparatorHovered{0.35f, 0.28f, 0.45f, 1.00f};
constexpr ImVec4 SeparatorActive{0.48f, 0.38f, 0.58f, 1.00f};
constexpr ImVec4 Text{0.96f, 0.96f, 1.00f, 1.00f};
constexpr ImVec4 TextDisabled{0.55f, 0.55f, 0.65f, 1.00f};
constexpr ImVec4 TextSelectedBg{0.45f, 0.30f, 0.60f, 0.35f};
constexpr ImVec4 Button{0.20f, 0.12f, 0.30f, 1.00f};
constexpr ImVec4 ButtonHovered{0.34f, 0.22f, 0.48f, 1.00f};
constexpr ImVec4 ButtonActive{0.50f, 0.32f, 0.66f, 1.00f};

// Card background (slightly lighter than window bg, subtle border).
constexpr ImVec4 CardBg{0.10f, 0.09f, 0.14f, 1.00f};
constexpr ImVec4 CardBorder{0.16f, 0.14f, 0.22f, 1.00f};

// Tab bar colors.
constexpr ImVec4 TabBg{0.090f, 0.080f, 0.120f, 1.00f};
constexpr ImVec4 TabHovered{0.16f, 0.13f, 0.22f, 1.00f};
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

  if (fontStart > size || size - fontStart < 12 || !hasSfntTag(data + fontStart))
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
      : io(target),
        enableAssert(target.ConfigErrorRecoveryEnableAssert),
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
      fileSize > static_cast<std::uintmax_t>((std::numeric_limits<int>::max)())) {
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
      bytes.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
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

  // Geometry (scales with UI scale).
  style.WindowPadding = {16.0f * scale, 14.0f * scale};
  style.WindowRounding = 12.0f * scale;
  style.WindowBorderSize = 1.0f * scale;
  style.ChildRounding = 10.0f * scale;
  style.ChildBorderSize = 1.0f * scale;
  style.PopupRounding = 10.0f * scale;
  style.PopupBorderSize = 1.0f * scale;
  style.FramePadding = {14.0f * scale, 8.0f * scale};
  style.FrameRounding = 8.0f * scale;
  style.FrameBorderSize = 0.0f;
  style.ItemSpacing = {12.0f * scale, 8.0f * scale};
  style.ItemInnerSpacing = {8.0f * scale, 6.0f * scale};
  style.IndentSpacing = 20.0f * scale;
  style.ScrollbarSize = 14.0f * scale;
  style.ScrollbarRounding = 7.0f * scale;
  style.GrabMinSize = 14.0f * scale;
  style.GrabRounding = 4.0f * scale;
  style.TabRounding = 8.0f * scale;
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
  style.Colors[ImGuiCol_BorderShadow] = {0.0f, 0.0f, 0.0f, 0.50f};
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
  style.Colors[ImGuiCol_SliderGrab] = Accent;
  style.Colors[ImGuiCol_SliderGrabActive] = AccentHover;
  style.Colors[ImGuiCol_Button] = Button;
  style.Colors[ImGuiCol_ButtonHovered] = ButtonHovered;
  style.Colors[ImGuiCol_ButtonActive] = ButtonActive;
  style.Colors[ImGuiCol_Header] = Header;
  style.Colors[ImGuiCol_HeaderHovered] = HeaderHovered;
  style.Colors[ImGuiCol_HeaderActive] = HeaderActive;
  style.Colors[ImGuiCol_Separator] = Separator;
  style.Colors[ImGuiCol_SeparatorHovered] = SeparatorHovered;
  style.Colors[ImGuiCol_SeparatorActive] = SeparatorActive;
  style.Colors[ImGuiCol_ResizeGrip] = {0.30f, 0.24f, 0.38f, 0.60f};
  style.Colors[ImGuiCol_ResizeGripHovered] = {0.44f, 0.34f, 0.52f, 0.70f};
  style.Colors[ImGuiCol_ResizeGripActive] = {0.58f, 0.44f, 0.68f, 0.80f};
  style.Colors[ImGuiCol_Tab] = TabBg;
  style.Colors[ImGuiCol_TabHovered] = TabHovered;
  style.Colors[ImGuiCol_PlotLines] = AccentDim;
  style.Colors[ImGuiCol_PlotLinesHovered] = Accent;
  style.Colors[ImGuiCol_PlotHistogram] = Accent;
  style.Colors[ImGuiCol_PlotHistogramHovered] = AccentDim;
  style.Colors[ImGuiCol_TableHeaderBg] = Header;
  style.Colors[ImGuiCol_TableBorderStrong] = BorderHovered;
  style.Colors[ImGuiCol_TableBorderLight] = Border;
  style.Colors[ImGuiCol_TableRowBg] = {0.0f, 0.0f, 0.0f, 0.0f};
  style.Colors[ImGuiCol_TableRowBgAlt] = {0.15f, 0.12f, 0.20f, 0.20f};
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
  ImGui::PushStyleColor(ImGuiCol_Button, Button);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ButtonHovered);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ButtonActive);
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(3);
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
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
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
                      ImGui::GetColorU32(color), 6.0f * scale);
  draw->AddText({pos.x + pad.x, pos.y + pad.y}, IM_COL32(255, 255, 255, 255),
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

  const ImVec4 bgColor = *v ? Accent : ImVec4{0.20f, 0.18f, 0.26f, 1.f};
  const ImVec4 thumbColor = {0.95f, 0.95f, 1.00f, 1.f};

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
  ImGui::PushStyleColor(ImGuiCol_Button, Button);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ButtonHovered);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ButtonActive);
  std::string fullLabel = std::string(icon) + "  " + label;
  const bool pressed = ImGui::Button(fullLabel.c_str(), size);
  ImGui::PopStyleColor(3);
  return pressed;
}

void kotonohaHeader(const char *title, const char *subtitle, float scale) {
  const float band = 80.f * scale;
  const ImVec2 origin = ImGui::GetWindowPos();
  auto *draw = ImGui::GetWindowDrawList();
  // Gradient background band.
  draw->AddRectFilledMultiColor(
      origin, {origin.x + ImGui::GetWindowSize().x, origin.y + band},
      IM_COL32(45, 20, 70, 255), IM_COL32(25, 12, 45, 255),
      IM_COL32(20, 10, 30, 255), IM_COL32(20, 10, 30, 255));
  draw->AddRectFilled({origin.x, origin.y + band - 2.f * scale},
                      {origin.x + ImGui::GetWindowSize().x, origin.y + band},
                      ImGui::GetColorU32(Accent));
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
  ImGui::TextColored(ImGuiTheme::accent(), "%s", title);
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
