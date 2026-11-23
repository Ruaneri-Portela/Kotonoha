#include <Kotonoha/core/BuildVersion.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Kotonoha {
namespace {
std::string toLowerAscii(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

UpscaleBackend parseUpscaleBackend(const std::string &raw) {
  const auto key = toLowerAscii(raw);
  if (key == "none" || key == "off" || key == "0")
    return UpscaleBackend::None;
  if (key == "linear" || key == "1")
    return UpscaleBackend::Linear;
  if (key == "mpv" || key == "opengl" || key == "2")
    return UpscaleBackend::MPV;
  if (key == "metalfx" || key == "3")
    return UpscaleBackend::MetalFX;
  if (key == "auto" || key == "automatic" || key == "4")
    return UpscaleBackend::Auto;
  throw std::invalid_argument(
      "--upscaler must be one of: auto, none, linear, mpv, metalfx");
}

UpscalePreset parseUpscalePreset(const std::string &raw) {
  int numeric = -1;
  const auto parsed =
      std::from_chars(raw.data(), raw.data() + raw.size(), numeric);
  if (parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size()) {
    const auto &presets = UpscalePresets();
    if (numeric >= 0 && numeric < static_cast<int>(presets.size()))
      return presets[static_cast<std::size_t>(numeric)];
    throw std::invalid_argument("--upscaler-preset index out of range");
  }
  const auto key = toLowerAscii(raw);
  if (key == "anime4k")
    return UpscalePreset::Anime4K;
  if (key == "artcnn")
    return UpscalePreset::ArtCNN;
  if (key == "artcnn-denoise" || key == "artcnndenoise")
    return UpscalePreset::ArtCNNDenoise;
  if (key == "artcnn-large" || key == "artcnnlarge")
    return UpscalePreset::ArtCNNLarge;
  for (auto preset : UpscalePresets()) {
    if (toLowerAscii(UpscalePresetName(preset)) == key)
      return preset;
  }
  throw std::invalid_argument("Unknown --upscaler-preset: " + raw);
}
} // namespace

CommandLine::CommandLine(std::string program, EngineOptions &engine)
    : engine(engine), program(std::move(program)) {
  flag("--help", "Show engine and game options without opening the game",
       helpRequested);
  alias("--help", "-h");
  flag("--version", "Print Git/CMake build version and exit", versionRequested);
  alias("--version", "-V");
  option("--config", "FILE", "Load and save engine settings",
         engine.settingsPath);
  flag("--no-config", "Do not persist engine settings",
       [&engine] { engine.persistSettings = false; });
  option("--renderer", "NAME", "SDL renderer override (requires restart)",
         engine.rendererOverride);
  option("--upscaler", "BACKEND",
         "Upscaler backend override (auto, none, linear, mpv, metalfx)",
         [&engine](const std::string &value) {
           engine.upscalerBackendOverride = parseUpscaleBackend(value);
         });
  option("--upscaler-preset", "PRESET",
         "Upscaler shader preset index or name (Anime4K / ArtCNN)",
         [&engine](const std::string &value) {
           engine.upscalerPresetOverride = parseUpscalePreset(value);
         });
  option("--upscaler-factor", "FACTOR",
         "Upscaler scale factor in [1.0, 4.0] (1.0 disables upscaling)",
         [&engine](const std::string &value) {
           char *end = nullptr;
           const float factor = std::strtof(value.c_str(), &end);
           if (!end || *end != '\0' || !std::isfinite(factor) ||
               factor < 1.0f || factor > 4.0f)
             throw std::invalid_argument(
                 "--upscaler-factor must be a number between 1.0 and 4.0");
           engine.upscalerFactorOverride = factor;
         });
#if !defined(KOTONOHA_DISTRIBUTION_BUILD)
  option("--log", "FILE", "Write engine logs to this file", engine.logPath);
#if KOTONOHA_ENABLE_MCP
  flag("--mcp", "Enable the stdio MCP interface", engine.enableMCP);
  option("--mcp-root", "DIRECTORY", "Restrict MCP files to this root",
         engine.mcpFileRoot);
  option("--mcp-port", "PORT",
         "TCP loopback port for the MCP socket interface (0 = stdio)",
         [&engine](const std::string &value) {
           engine.mcpPort = std::stoi(value);
         });
#endif
#endif // !KOTONOHA_DISTRIBUTION_BUILD
  option("--asset-overlay", "DIRECTORY", "Override game assets from a folder",
         [&engine](const std::string &path) {
           engine.assetOverlayPath = path;
           engine.assetOverlaySpecified = true;
         });
  flag("--no-asset-overlay",
       "Use original assets, ignoring a remembered overlay", [&engine] {
         engine.assetOverlayPath.clear();
         engine.assetOverlaySpecified = true;
       });
  flag("--launcher", "Open the game selection launcher", engine.launcher);
  flag("--no-launcher", "Start the game directly",
       [&engine] { engine.launcher = false; });
  option("--dump-translation", "FILE", "Export original texts and exit",
         engine.dumpTranslationPath);
  option("--translation", "FILE", "Load a translation dictionary",
         engine.translationPath);
  option("--subtitle-style", "FILE.ass",
         "Use ASS script info/styles only; discard its dialogue events",
         engine.subtitleStylePath);
  alias("--subtitle-style", "--ass-style");
  option("--frames", "N", "Exit after N rendered frames (positive integer)",
         [this](const std::string &value) {
           long count = 0;
           const auto parsed = std::from_chars(
               value.data(), value.data() + value.size(), count);
           if (parsed.ec != std::errc{} ||
               parsed.ptr != value.data() + value.size() || count <= 0)
             throw std::invalid_argument("--frames must be a positive integer");
           frames = count;
         });
}

void CommandLine::option(std::string name, std::string value,
                         std::string description,
                         std::function<void(const std::string &)> action) {
  if (name.size() < 2 || name[0] != '-' ||
      name.find_first_of("= \t\r\n") != std::string::npos || !action ||
      std::any_of(options.begin(), options.end(),
                  [&](const Option &entry) { return entry.name == name; }))
    throw std::invalid_argument("Invalid or duplicate command-line option: " +
                                name);
  options.push_back({std::move(name), std::move(value), std::move(description),
                     std::move(action)});
}
void CommandLine::option(std::string name, std::string value,
                         std::string description, std::string &target) {
  if (value.empty())
    throw std::invalid_argument("A value option needs a value name");
  option(std::move(name), std::move(value), std::move(description),
         [&target](const std::string &text) { target = text; });
}
void CommandLine::flag(std::string name, std::string description,
                       std::function<void()> action) {
  if (!action)
    throw std::invalid_argument("A flag needs an action");
  option(std::move(name), {}, std::move(description),
         [action = std::move(action)](const std::string &) { action(); });
}
void CommandLine::flag(std::string name, std::string description,
                       bool &target) {
  flag(std::move(name), std::move(description), [&target] { target = true; });
}
void CommandLine::alias(const std::string &name, const std::string &alias) {
  const auto found =
      std::find_if(options.begin(), options.end(),
                   [&](const Option &entry) { return entry.name == name; });
  if (found == options.end())
    throw std::invalid_argument("Unknown aliased command-line option: " + name);
  const auto original = *found;
  option(alias, original.value, "Alias for " + name, original.action);
}
void CommandLine::parse(int argc, char **argv) {
  if (argc < 1 || !argv)
    throw std::invalid_argument("Invalid command-line arguments");
  for (int i = 1; i < argc; ++i) {
    if (!argv[i])
      throw std::invalid_argument("Null command-line argument");
    const std::string argument = argv[i];
    const auto equals = argument.find('=');
    const auto name = argument.substr(0, equals);
    const auto found =
        std::find_if(options.begin(), options.end(),
                     [&](const Option &entry) { return entry.name == name; });
    if (found == options.end())
      throw std::invalid_argument("Unknown option: " + name + "\n" + help());
    std::string value;
    if (found->value.empty()) {
      if (equals != std::string::npos)
        throw std::invalid_argument("Flag does not accept a value: " + name);
    } else {
      if (equals != std::string::npos)
        value = argument.substr(equals + 1);
      else if (++i < argc && argv[i])
        value = argv[i];
      if (value.empty() || value.front() == '-')
        throw std::invalid_argument("Missing value for " + name + " " +
                                    found->value);
    }
    found->action(value);
  }
  // Headless CLI operations must reach the game without opening an ImGui
  // launcher. This policy is shared by every port, including newly added
  // games, and is independent of the order of command-line switches.
  if (!engine.dumpTranslationPath.empty() || frames > 0 || engine.enableMCP)
    engine.launcher = false;
}
std::string CommandLine::help() const {
  std::ostringstream result;
  if (versionRequested) {
    result << program << " " << BuildVersion::Full << "\n"
           << "Branch: " << BuildVersion::Branch << "\n"
           << "Commit: " << BuildVersion::Commit << "\n"
           << "Source tree: " << (BuildVersion::Dirty ? "modified" : "clean")
           << "\n";
    return result.str();
  }
  result << "Usage: " << program << " [options]\n\n";
  for (const auto &entry : options) {
    const auto label =
        entry.name + (entry.value.empty() ? "" : " " + entry.value);
    result << "  " << std::left << std::setw(34) << label << entry.description
           << '\n';
  }
  return result.str();
}
} // namespace Kotonoha
