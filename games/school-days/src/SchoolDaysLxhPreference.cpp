#include <Kotonoha/routing/SchoolDaysLxhPreference.hpp>

#include <Kotonoha/SchoolDaysGpkArchive.hpp>
#include <Kotonoha/routing/SchoolDaysAtomicFile.hpp>
#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>

#include <cctype>
#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace Kotonoha {
namespace {

constexpr std::uintmax_t kMaxIniBytes = 131072u;

std::string TrimAscii(std::string_view value) {
  std::size_t begin = 0u;
  std::size_t end = value.size();
  while (begin < end &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0)
    ++begin;
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1u])) != 0)
    --end;
  return std::string(value.substr(begin, end - begin));
}

std::filesystem::path ResolvePacksDir(
    const std::filesystem::path &gameOrPacksRoot) {
  std::filesystem::path packs = gameOrPacksRoot;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(packs / "BGM.GPK", ec)) {
    ec.clear();
    packs /= "Packs";
  }
  return packs;
}

} // namespace

std::optional<bool> SchoolDaysLxhPreference::Read(
    const std::filesystem::path &documentsRoot) {
  if (documentsRoot.empty())
    return std::nullopt;
  const auto path = documentsRoot / "Kotonoha.ini";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec)
    return std::nullopt;
  if (std::filesystem::file_size(path, ec) > kMaxIniBytes || ec)
    return std::nullopt;
  Kotonoha::Filesystem::InputFile input(path);
  if (!input)
    return std::nullopt;
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind(IniKey.data(), 0) != 0)
      continue;
    const std::string value = TrimAscii(std::string_view(line).substr(IniKey.size()));
    if (value == "1" || value == "true" || value == "TRUE")
      return true;
    if (value == "0" || value == "false" || value == "FALSE")
      return false;
    return std::nullopt;
  }
  return std::nullopt;
}

void SchoolDaysLxhPreference::Write(const std::filesystem::path &documentsRoot,
                                    bool enabled) {
  if (documentsRoot.empty())
    return;
  std::error_code ec;
  std::filesystem::create_directories(documentsRoot, ec);
  if (ec)
    throw std::runtime_error(
        "Cannot create School Days preference directory: " + ec.message());
  const auto path = documentsRoot / "Kotonoha.ini";
  std::string contents;
  if (std::filesystem::is_regular_file(path, ec) && !ec) {
    if (std::filesystem::file_size(path, ec) > kMaxIniBytes || ec)
      throw std::runtime_error("Oversized School Days Kotonoha.ini file");
    Kotonoha::Filesystem::InputFile input(path, std::ios::binary);
    if (!input)
      throw std::runtime_error("Cannot open School Days Kotonoha.ini");
    contents.assign(std::istreambuf_iterator<char>(input), {});
  }
  const std::string replacement =
      std::string(IniKey) + (enabled ? "1" : "0");
  bool replaced = false;
  std::size_t cursor = 0u;
  while (cursor < contents.size()) {
    if (contents.compare(cursor, IniKey.size(), IniKey) == 0) {
      const std::size_t end = contents.find_first_of("\r\n", cursor);
      contents.replace(cursor,
                       end == std::string::npos ? contents.size() - cursor
                                                : end - cursor,
                       replacement);
      replaced = true;
      break;
    }
    const auto newline = contents.find('\n', cursor);
    if (newline == std::string::npos)
      break;
    cursor = newline + 1u;
  }
  if (!replaced) {
    if (!contents.empty() && contents.back() != '\n')
      contents.push_back('\n');
    if (contents.empty())
      contents = "[KotonohaConfig][Settings]\n";
    contents += replacement + "\n";
  }
  std::vector<uint8_t> bytes(contents.begin(), contents.end());
  std::string writeError;
  if (!SchoolDaysAtomicFile::Write(path, bytes, SchoolDaysAtomicFile::Fault::None,
                                   &writeError))
    throw std::runtime_error(
        writeError.empty() ? "Cannot write School Days Kotonoha.ini"
                           : writeError);
}

SchoolDaysLxhPreference::Availability SchoolDaysLxhPreference::ProbePack(
    const std::filesystem::path &gameOrPacksRoot,
    std::shared_ptr<const Filesystem::IMount> source) {
  Availability result{};
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  (void)gameOrPacksRoot;
  (void)source;
  result.reason = "Retail-only build excludes LxH endings";
  return result;
#else
  std::filesystem::path lxhPath;
  if (source) {
    lxhPath = std::filesystem::path("LxH.GPK");
    result.packPresent = source->exists("LxH.GPK");
  } else {
    if (gameOrPacksRoot.empty()) {
      result.reason = "Packs directory is not configured";
      return result;
    }
    const auto packs = ResolvePacksDir(gameOrPacksRoot);
    lxhPath = packs / "LxH.GPK";
    std::error_code ec;
    result.packPresent = std::filesystem::is_regular_file(lxhPath, ec) && !ec;
  }
  if (!result.packPresent) {
    result.reason = "Packs/LxH.GPK is not installed";
    return result;
  }
  SchoolDaysGpkArchive archive;
  std::string error;
  if (!archive.Open(lxhPath, &error, source)) {
    result.reason = "Packs/LxH.GPK is invalid: " + error;
    return result;
  }
  const auto expected = SchoolDaysLxhContent::PackEntries();
  if (archive.EntryCount() != expected.size()) {
    result.reason = "Packs/LxH.GPK must contain exactly 28 files";
    return result;
  }
  for (const auto &logical : expected) {
    size_t entryIndex = 0u;
    if (archive.Lookup(logical, &entryIndex) !=
            SchoolDaysGpkLookupStatus::Present ||
        entryIndex >= archive.Index().Entries().size()) {
      result.reason = "Packs/LxH.GPK missing or ambiguous entry: " + logical;
      return result;
    }
    const auto &entry = archive.Index().Entries()[entryIndex];
    if (!entry.IsRaw() || entry.uncompressedLength != entry.storedLength ||
        entry.storedLength == 0u) {
      result.reason =
          "Packs/LxH.GPK entry is not seekable non-empty raw: " + logical;
      return result;
    }
  }
  result.packValid = true;
  result.reason = "Packs/LxH.GPK validated (28/28 entries)";
  return result;
#endif
}

SchoolDaysLxhPreference::Resolution SchoolDaysLxhPreference::Resolve(
    std::optional<bool> savedPreference, const Availability &availability,
    bool translationReady, const std::string &translationReason) {
  Resolution out{};
  out.explicitPreference = savedPreference.has_value();
  out.packPresent = availability.packPresent;
  out.packValid = availability.packValid;
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  out.userPreference = false;
  out.effectiveEnabled = false;
  out.statusReason = "Retail-only edition: playing original School Days HQ.";
  (void)translationReady;
  (void)translationReason;
  return out;
#else
  out.userPreference =
      out.explicitPreference ? *savedPreference : availability.packValid;
  if (!out.userPreference) {
    out.effectiveEnabled = false;
    out.statusReason =
        "LxH endings disabled (playing original School Days HQ).";
    return out;
  }
  if (!availability.packValid) {
    out.effectiveEnabled = false;
    out.statusReason =
        availability.packPresent
            ? ("LxH endings unavailable (" + availability.reason +
               "); playing original School Days HQ.")
            : "LxH endings unavailable (Packs/LxH.GPK not found); playing "
              "original School Days HQ.";
    return out;
  }
  if (!translationReady) {
    out.effectiveEnabled = false;
    out.statusReason =
        "LxH endings unavailable for the selected language" +
        (translationReason.empty() ? std::string{}
                                   : " (" + translationReason + ")") +
        "; playing original School Days HQ without LxH.";
    return out;
  }
  out.effectiveEnabled = true;
  out.statusReason = "LxH endings enabled (7 epilogues ready).";
  return out;
#endif
}

} // namespace Kotonoha
