#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/parsers/Documents.hpp>
#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace Kotonoha {
namespace {

std::mutex gMountMutex;
std::shared_ptr<const SchoolDaysGpkAssetStore> gMount;
std::shared_ptr<const SchoolDaysGpkArchive> gIntegratedLxh;
std::optional<SchoolDaysTranslatedGpkSelection> gTranslatedSelection;
bool gTranslatedSelectionInvalid = false;
std::shared_ptr<const SchoolDaysGpkArchive> gTranslatedScript;
std::shared_ptr<const SchoolDaysGpkArchive> gTranslatedLxh;
std::shared_ptr<const AssetOverlay> gOverlay;

bool Fail(std::string *error, const char *text);
bool Fail(std::string *error, const std::string &text);

bool RejectTranslatedConfiguration(std::string *error,
                                   const std::string &reason) {
  // Even if a future caller accidentally ignores Configure's false return,
  // the mount cannot silently start with the previous English retail assets.
  std::lock_guard<std::mutex> guard(gMountMutex);
  if (!gMount && !gIntegratedLxh) gTranslatedSelectionInvalid = true;
  return Fail(error, reason);
}

struct TranslatedMountSnapshot {
  std::shared_ptr<const SchoolDaysGpkArchive> script;
  std::shared_ptr<const SchoolDaysGpkArchive> lxh;
};

TranslatedMountSnapshot CurrentTranslated() {
  std::lock_guard<std::mutex> guard(gMountMutex);
  return {gTranslatedScript, gTranslatedLxh};
}

bool AsciiStartsWith(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) return false;
  for (size_t i = 0u; i < prefix.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(value[i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
  return true;
}

bool AsciiEndsWith(std::string_view value, std::string_view suffix) {
  if (value.size() < suffix.size()) return false;
  return AsciiStartsWith(value.substr(value.size() - suffix.size()), suffix);
}

std::string NormalizedLogical(std::string_view logical) {
  std::string value(logical);
  std::replace(value.begin(), value.end(), '\\', '/');
  return value;
}

constexpr std::string_view kTranslatedScriptVirtualSuffix =
    "/__sd_localized_script__";

// A suffix preserves Script/English for Assets::listAsset prefix filtering,
// while preventing the AssetProvider's loose overlay from shadowing translated
// ORS: Assets::overlayId returns this marked logical path verbatim. The reader
// removes the suffix before any validated GPK access; it is never on disk.
std::string UnmarkTranslatedLogical(std::string_view logical) {
  std::string path = NormalizedLogical(logical);
  if (AsciiEndsWith(path, kTranslatedScriptVirtualSuffix))
    path.resize(path.size() - kTranslatedScriptVirtualSuffix.size());
  return path;
}

std::shared_ptr<const SchoolDaysGpkArchive> CurrentIntegratedLxh();

bool IsLxhMediaPath(std::string_view logical) {
  return AsciiStartsWith(UnmarkTranslatedLogical(logical), "LxH/");
}

bool IsHqScriptOrs(std::string_view logical) {
  const auto path = UnmarkTranslatedLogical(logical);
  if (!AsciiStartsWith(path, "Script/English/") || !AsciiEndsWith(path, ".ORS"))
    return false;
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  return true;
#else
  if (!CurrentIntegratedLxh())
    return true;
  return !SchoolDaysLxhContent::IsPackResource(path);
#endif
}

bool IsMarkedTranslatedScript(std::string_view logical) {
  return AsciiEndsWith(NormalizedLogical(logical), kTranslatedScriptVirtualSuffix);
}

bool SameSelection(const std::optional<SchoolDaysTranslatedGpkSelection> &a,
                   const std::optional<SchoolDaysTranslatedGpkSelection> &b) {
  if (a.has_value() != b.has_value()) return false;
  if (!a) return true;
  return a->locale == b->locale && a->scriptGpk == b->scriptGpk &&
         a->lxhGpk == b->lxhGpk && a->enableLxh == b->enableLxh;
}

bool IsLxhScriptOrs(std::string_view logical) {
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  (void)logical;
  return false;
#else
  if (!CurrentIntegratedLxh())
    return false;
  const auto path = UnmarkTranslatedLogical(logical);
  return SchoolDaysLxhContent::IsPackResource(path) &&
         AsciiEndsWith(path, ".ORS");
#endif
}

std::string ScriptRelative(std::string_view logical) {
  const std::string path = UnmarkTranslatedLogical(logical);
  return AsciiStartsWith(path, "Script/") ? path.substr(7u) : std::string();
}

bool IsOrsName(const std::u16string &name) {
  if (name.size() < 4u) return false;
  constexpr char16_t suffix[] = u".ORS";
  for (size_t i = 0u; i < 4u; ++i) {
    char16_t actual = name[name.size() - 4u + i];
    if (actual >= u'a' && actual <= u'z') actual -= (u'a' - u'A');
    if (actual != suffix[i]) return false;
  }
  return true;
}

bool SameOrsStructure(const std::vector<uint8_t> &reference,
                      const std::vector<uint8_t> &translation,
                      std::string *error,
                      bool allowKnownRussianVoiceSubtitle = false) {
  OrsDocument original(Kotonoha_OrsParserBytes(reference.data(), reference.size()));
  OrsDocument localized(Kotonoha_OrsParserBytes(translation.data(), translation.size()));
  if (!original.get().data || !original.get().size ||
      !localized.get().data || !localized.get().size)
    return Fail(error, "Translated ORS or its reference could not be parsed");
  // The known Russian fan patch includes one extra subtitle over Taisuke's
  // unchanged voice in the single retail scene 03-KB-D10. Permit that exact
  // text-only insertion; every original command/tick/media remains mandatory.
  const bool russianExtra = allowKnownRussianVoiceSubtitle &&
      localized.get().size == original.get().size + 1;
  if (!russianExtra && original.get().size != localized.get().size)
    return Fail(error, "Translated ORS event count differs from original");
  const auto samePath = [](const char *left, const char *right) {
    return left && right ? std::strcmp(left, right) == 0 : left == right;
  };
  const Kotonoha_orsEvent *a = original.get().data;
  const Kotonoha_orsEvent *b = localized.get().data;
  bool skippedRussianSubtitle = false;
  for (; a && b; a = a->next, b = b->next) {
    if (russianExtra && !skippedRussianSubtitle &&
        a->command == PLAY_VOICE && a->startTick == 403 &&
        a->endTick == 577 && b->command == PRINT_TEXT &&
        b->startTick == 403 && b->endTick == 577 &&
        b->data.print_text && b->data.print_text->text &&
        *b->data.print_text->text &&
        samePath(b->data.print_text->character, "Taisuke")) {
      skippedRussianSubtitle = true;
      b = b->next;
      if (!b) return Fail(error, "Russian extra subtitle displaced voice event");
    }
    if (a->command != b->command || a->startTick != b->startTick ||
        a->endTick != b->endTick)
      return Fail(error, "Translated ORS command sequence/timing differs from original");
    if (a->command == SetSELECT &&
        (!a->data.set_select || !b->data.set_select ||
         a->data.set_select->size != b->data.set_select->size))
      return Fail(error, "Translated ORS choice structure differs from original");
    // A localization may change dialogue, speaker labels and choice wording.
    // Timed commands, media filenames and non-text control arguments must
    // remain intact, or the language pack could change the game's behavior.
    switch (a->command) {
    case CREATE_BG:
      if (!a->data.create_bg || !b->data.create_bg ||
          !samePath(a->data.create_bg->a, b->data.create_bg->a) ||
          !samePath(a->data.create_bg->path, b->data.create_bg->path))
        return Fail(error, "Translated ORS background command changed");
      break;
    case PLAY_SE:
      if (!a->data.play_se || !b->data.play_se ||
          a->data.play_se->a != b->data.play_se->a ||
          !samePath(a->data.play_se->path, b->data.play_se->path))
        return Fail(error, "Translated ORS sound effect command changed");
      break;
    case PLAY_MOVIE:
      if (!a->data.play_movie || !b->data.play_movie ||
          a->data.play_movie->a != b->data.play_movie->a ||
          !samePath(a->data.play_movie->path, b->data.play_movie->path))
        return Fail(error, "Translated ORS movie command changed");
      break;
    case PLAY_BGM:
    case END_BGM:
      if (!a->data.path_end || !b->data.path_end ||
          !samePath(a->data.path_end->path, b->data.path_end->path))
        return Fail(error, "Translated ORS BGM command changed");
      break;
    case PLAY_VOICE:
      if (!a->data.play_voice || !b->data.play_voice ||
          a->data.play_voice->a != b->data.play_voice->a ||
          !samePath(a->data.play_voice->path, b->data.play_voice->path))
        return Fail(error, "Translated ORS voice resource changed");
      break;
    case MOVE_SOM:
      if (!a->data.move_som || !b->data.move_som ||
          a->data.move_som->numeric != b->data.move_som->numeric)
        return Fail(error, "Translated ORS animation command changed");
      break;
    case UNKNOWN:
      if (!a->data.unknown || !b->data.unknown ||
          !samePath(a->data.unknown->line, b->data.unknown->line))
        return Fail(error, "Translated ORS contains different unknown commands");
      break;
    default: break;
    }
  }
  return (!a && !b && (!russianExtra || skippedRussianSubtitle)) ? true :
      Fail(error, "Translated ORS linked event sequence differs from original");
}

bool InspectTranslatedEntry(const SchoolDaysGpkArchive &archive,
                            size_t index, std::string *error) {
  const auto &entry = archive.Index().Entries()[index];
  if (!IsOrsName(entry.filename)) return true;
  if (entry.storedLength == 0u ||
      (!entry.IsRaw() && !entry.IsDeflate()))
    return Fail(error, "Translated GPK contains an empty or unsupported ORS");
  std::vector<uint8_t> decoded;
  std::string failure;
  if (!archive.ReadEntry(index, &decoded, &failure) || decoded.empty())
    return Fail(error, "Translated GPK contains unreadable/empty ORS: " + failure);
  return true;
}

std::vector<std::u16string> SortedEntryNames(const SchoolDaysGpkArchive &archive) {
  std::vector<std::u16string> names;
  names.reserve(archive.EntryCount());
  for (const auto &entry : archive.Index().Entries())
    names.push_back(entry.filename);
  std::sort(names.begin(), names.end());
  return names;
}

bool ValidateTranslatedScript(const SchoolDaysGpkArchive &selected,
                              const SchoolDaysGpkArchive &retail,
                              const std::string &locale,
                              std::string *error) {
  // Compare the complete PIDX path set, including each original subdirectory
  // and filename. Matching only an arbitrary ORS subset would silently mix
  // language assets from the translated GPK and the original retail GPK.
  const auto expected = SortedEntryNames(retail);
  if (selected.EntryCount() != retail.EntryCount() ||
      SortedEntryNames(selected) != expected)
    return Fail(error, "Translated Script.GPK must match the complete retail "
                       "Script.GPK entry names and directory structure");
  std::unordered_map<std::u16string, size_t> retailIndex;
  const auto &retailEntries = retail.Index().Entries();
  retailIndex.reserve(retailEntries.size());
  for (size_t i = 0; i < retailEntries.size(); ++i)
    retailIndex.emplace(retailEntries[i].filename, i);
  bool foundOrs = false;
  for (size_t i = 0u; i < selected.EntryCount(); ++i) {
    const auto &entry = selected.Index().Entries()[i];
    if (!IsOrsName(entry.filename)) continue;
    foundOrs = true;
    if (entry.storedLength == 0u ||
        (!entry.IsRaw() && !entry.IsDeflate()))
      return Fail(error, "Translated GPK contains an empty or unsupported ORS");
    const auto it = retailIndex.find(entry.filename);
    if (it == retailIndex.end())
      return Fail(error, "Translated ORS is absent from retail index");
    std::vector<uint8_t> base, translated;
    std::string failure;
    if (!selected.ReadEntry(i, &translated, &failure) || translated.empty())
      return Fail(error, "Translated GPK contains unreadable/empty ORS: " + failure);
    if (!retail.ReadEntry(it->second, &base, &failure))
      return Fail(error, "Cannot inspect corresponding ORS files: " + failure);
    const bool knownRussianSubtitle = locale == "ru" &&
        entry.filename == u"ENGLISH/03/03-KB-D10.ENG.ORS";
    if (!SameOrsStructure(base, translated, &failure, knownRussianSubtitle))
      return Fail(error, "Translated Script.GPK ORS structure mismatch: " + failure);
  }
  return foundOrs ? true : Fail(error, "Translated Script.GPK contains no ORS");
}

bool ValidateTranslatedLxh(const SchoolDaysGpkArchive &selected,
                           const SchoolDaysGpkArchive &original,
                           std::string *error) {
  std::set<std::u16string> expectedOrs;
  std::set<std::u16string> expectedAll;
  for (const std::string &logical : SchoolDaysLxhContent::PackEntries()) {
    const std::u16string name(logical.begin(), logical.end()); // known ASCII paths
    expectedAll.insert(name);
    if (AsciiEndsWith(logical, ".ORS")) expectedOrs.insert(name);
  }
  std::set<std::u16string> actual;
  for (size_t i = 0u; i < selected.EntryCount(); ++i) {
    const auto &entry = selected.Index().Entries()[i];
    actual.insert(entry.filename);
    if (!InspectTranslatedEntry(selected, i, error)) return false;
  }
  // Language packs may ship just seven translated ORS, or an exact 28-entry
  // mirror. Even for a 28-entry mirror, all media remain owned by LxH.GPK.
  if (actual.size() != selected.EntryCount() ||
      (actual != expectedOrs && actual != expectedAll))
    return Fail(error, "Translated LxH GPK must contain exactly the seven "
                       "expected ORS (or the exact original 28-entry layout)");
  for (const auto &path : SchoolDaysLxhContent::PackEntries()) {
    if (!AsciiEndsWith(path, ".ORS")) continue;
    size_t selectedIndex = 0, originalIndex = 0;
    if (selected.Lookup(path, &selectedIndex) != SchoolDaysGpkLookupStatus::Present ||
        original.Lookup(path, &originalIndex) != SchoolDaysGpkLookupStatus::Present)
      return Fail(error, "Translated LxH GPK has an ambiguous or missing epilogue ORS: " + path);
    std::vector<uint8_t> base, translated;
    std::string failure;
    if (!original.ReadEntry(originalIndex, &base, &failure) ||
        !selected.ReadEntry(selectedIndex, &translated, &failure))
      return Fail(error, "Cannot compare LxH ORS structure " + path + ": " + failure);
    if (!SameOrsStructure(base, translated, &failure))
      return Fail(error, "Translated LxH ORS structure mismatch " + path + ": " + failure);
  }
  return true;
}

std::shared_ptr<const SchoolDaysGpkAssetStore> CurrentMount() {
  std::lock_guard<std::mutex> guard(gMountMutex);
  return gMount;
}

std::shared_ptr<const SchoolDaysGpkArchive> CurrentIntegratedLxh() {
  std::lock_guard<std::mutex> guard(gMountMutex);
  return gIntegratedLxh;
}

class MountedRawAssetStream final : public AssetStream {
    std::shared_ptr<const void> mount_;
    std::unique_ptr<std::istream> file_;
    std::vector<uint8_t> prefix_;
    uint64_t bodyOffset_ = 0u;
    uint64_t logicalSize_ = 0u;
    uint64_t position_ = 0u;

public:
  MountedRawAssetStream(std::shared_ptr<const void> mount,
                        std::unique_ptr<std::istream> file,
                        std::vector<uint8_t> prefix, uint64_t bodyOffset,
                        uint64_t logicalSize)
      : mount_(std::move(mount)), file_(std::move(file)),
        prefix_(std::move(prefix)), bodyOffset_(bodyOffset),
        logicalSize_(logicalSize) {
    if (!file_ || !*file_)
      throw std::runtime_error(
          "School Days GPK raw stream could not open archive");
  }

    uint64_t size() const override { return logicalSize_; }
    uint64_t tell() const override { return position_; }

    std::size_t read(Uint8* buffer, std::size_t size) override {
        if (size == 0u || position_ >= logicalSize_) return 0u;
        if (buffer == nullptr) throw std::invalid_argument("School Days GPK raw stream buffer is null");
        const uint64_t requested = std::min<uint64_t>(
            static_cast<uint64_t>(size), logicalSize_ - position_);
        uint64_t done = 0u;
        if (position_ < prefix_.size()) {
            const uint64_t prefixBytes = std::min<uint64_t>(
                requested, prefix_.size() - static_cast<std::size_t>(position_));
            std::memcpy(buffer, prefix_.data() + static_cast<std::size_t>(position_),
                        static_cast<std::size_t>(prefixBytes));
            position_ += prefixBytes;
            done += prefixBytes;
        }
        const uint64_t remaining = requested - done;
        if (remaining == 0u) return static_cast<std::size_t>(done);
        const uint64_t bodyPosition = position_ - prefix_.size();
        const uint64_t physical = bodyOffset_ + bodyPosition;
        if (physical > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
            remaining > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
            throw std::runtime_error("School Days GPK raw stream range exceeds host I/O limits");
        }
        file_->clear();
        file_->seekg(static_cast<std::streamoff>(physical), std::ios::beg);
        if (!*file_)
          throw std::runtime_error("School Days GPK raw stream seek failed");
        file_->read(reinterpret_cast<char *>(buffer + done),
                    static_cast<std::streamsize>(remaining));
        const std::streamsize got = file_->gcount();
        if (got <= 0) throw std::runtime_error("School Days GPK raw stream read failed");
        position_ += static_cast<uint64_t>(got);
        done += static_cast<uint64_t>(got);
        return static_cast<std::size_t>(done);
    }

    bool seek(uint64_t position) override {
        if (position > logicalSize_) return false;
        position_ = position;
        return true;
    }
};

std::shared_ptr<const AssetOverlay> CurrentOverlay() {
  std::lock_guard<std::mutex> guard(gMountMutex);
  return gOverlay;
}

// Translated scripts name resources with their original Kotonoha-side layout
// ("Se/Se00/...", "Music/Bgm/...", "Music/Vocal/..."); the retail packs expose
// them as "Se00/...", "BGM/SD_BGM/..." and "BGM/VOCAL/...".
std::string CanonicalGpkLogical(std::string_view logicalPath) {
  std::string path(logicalPath);
  for (char &ch : path)
    if (ch == '\\')
      ch = '/';
  const auto startsWith = [&path](std::string_view prefix) {
    if (path.size() < prefix.size())
      return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(path[i])) !=
          std::tolower(static_cast<unsigned char>(prefix[i])))
        return false;
    }
    return true;
  };
  if (startsWith("Music/Bgm/"))
    return "BGM/SD_BGM/" + path.substr(10);
  if (startsWith("Music/Vocal/"))
    return "BGM/VOCAL/" + path.substr(12);
  if (startsWith("Se/Se"))
    return path.substr(3);
  return path;
}

bool Fail(std::string *error, const char *text) {
  if (error != nullptr)
    *error = text != nullptr ? text : "School Days asset error";
  return false;
}

bool Fail(std::string *error, const std::string &text) {
  if (error != nullptr) *error = text;
  return false;
}

SchoolDaysAssetResolutionStatus
MountedResolverLookup(const std::string &logicalPath, std::string *reference) {
  if (reference != nullptr)
    reference->clear();
  // These authored paths belong exclusively to Packs/LxH.GPK; never a
  // leftover loose overlay or an original commercial archive.
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  if (CurrentIntegratedLxh() && SchoolDaysLxhContent::IsPackResource(logicalPath)) {
    const auto status = LookupMountedSchoolDaysAsset(logicalPath);
    if (status == SchoolDaysGpkLookupStatus::Present && reference != nullptr)
      *reference = MakeSchoolDaysGpkUri(CanonicalGpkLogical(logicalPath));
    return status == SchoolDaysGpkLookupStatus::Present
               ? SchoolDaysAssetResolutionStatus::Present
               : status == SchoolDaysGpkLookupStatus::Collision
                     ? SchoolDaysAssetResolutionStatus::Collision
                     : SchoolDaysAssetResolutionStatus::Invalid;
  }
#endif
  if (!CurrentIntegratedLxh() && IsLxhMediaPath(logicalPath))
    return SchoolDaysAssetResolutionStatus::Missing;
  // The locale's HQ scripts take priority even over leftover loose text
  // overlays. No readable fallback is silently substituted for a missing ORS.
  if (IsHqScriptOrs(logicalPath) && CurrentTranslated().script) {
    const auto status = LookupMountedSchoolDaysAsset(logicalPath);
    if (status == SchoolDaysGpkLookupStatus::Present && reference)
      *reference = MakeSchoolDaysGpkUri(CanonicalGpkLogical(logicalPath));
    return status == SchoolDaysGpkLookupStatus::Present
               ? SchoolDaysAssetResolutionStatus::Present
               : status == SchoolDaysGpkLookupStatus::Collision
                     ? SchoolDaysAssetResolutionStatus::Collision
                     : SchoolDaysAssetResolutionStatus::Invalid;
  }
  std::string physical;
  if (ResolveSchoolDaysOverlayFile(logicalPath, &physical)) {
    if (reference != nullptr)
      *reference = physical;
    return SchoolDaysAssetResolutionStatus::Present;
  }
  const auto status = LookupMountedSchoolDaysAsset(logicalPath);
  switch (status) {
  case SchoolDaysGpkLookupStatus::Present:
    if (reference != nullptr)
      *reference = MakeSchoolDaysGpkUri(CanonicalGpkLogical(logicalPath));
    return SchoolDaysAssetResolutionStatus::Present;
  case SchoolDaysGpkLookupStatus::Missing:
    return SchoolDaysAssetResolutionStatus::Missing;
  case SchoolDaysGpkLookupStatus::Collision:
    return SchoolDaysAssetResolutionStatus::Collision;
  case SchoolDaysGpkLookupStatus::Invalid:
    return SchoolDaysAssetResolutionStatus::Invalid;
  }
  return SchoolDaysAssetResolutionStatus::Invalid;
}

bool MountedResolverIsReference(const std::string &reference) {
  return IsSchoolDaysGpkUri(reference);
}

bool MountedResolverRead(const std::string &reference,
                         std::vector<uint8_t> *bytes, std::string *error) {
  std::string logical;
  if (!SchoolDaysLogicalFromGpkUri(reference, &logical))
    return Fail(error, "School Days GPK reference is invalid");
  return ReadMountedSchoolDaysAsset(logical, bytes, error);
}

bool MountedResolverListDirectory(const std::string &directoryReference,
                                  std::vector<std::string> *children,
                                  std::string *error) {
  if (children == nullptr)
    return Fail(error, "School Days GPK directory output is null");
  children->clear();
  std::string logical;
  if (!SchoolDaysLogicalFromGpkUri(directoryReference, &logical))
    return Fail(error, "School Days GPK directory reference is invalid");
  const auto mount = CurrentMount();
  const auto integrated = CurrentIntegratedLxh();
  if (mount == nullptr)
    return Fail(error, "School Days retail GPK set is not mounted");
  std::vector<std::string> logicalChildren;
  const auto status = mount->ListDirectory(logical, &logicalChildren);
  std::vector<std::string> integratedChildren;
  const auto integratedStatus =
      integrated != nullptr
          ? integrated->ListDirectory(logical, &integratedChildren)
          : SchoolDaysGpkLookupStatus::Missing;
  if (status == SchoolDaysGpkLookupStatus::Collision ||
      status == SchoolDaysGpkLookupStatus::Invalid ||
      integratedStatus == SchoolDaysGpkLookupStatus::Collision ||
      integratedStatus == SchoolDaysGpkLookupStatus::Invalid)
    return Fail(error, "School Days GPK directory path is invalid or ambiguous");
  if (status != SchoolDaysGpkLookupStatus::Present &&
      integratedStatus != SchoolDaysGpkLookupStatus::Present) {
    if (error != nullptr) {
      switch (status) {
      case SchoolDaysGpkLookupStatus::Missing:
        *error = "School Days GPK directory is missing";
        break;
      case SchoolDaysGpkLookupStatus::Collision:
        *error = "School Days GPK directory has a case-insensitive collision";
        break;
      default:
        *error = "School Days GPK directory is invalid";
        break;
      }
    }
    return false;
  }
  if (integratedStatus == SchoolDaysGpkLookupStatus::Present) {
    logicalChildren.insert(logicalChildren.end(), integratedChildren.begin(),
                           integratedChildren.end());
    std::sort(logicalChildren.begin(), logicalChildren.end());
    logicalChildren.erase(std::unique(logicalChildren.begin(), logicalChildren.end()),
                          logicalChildren.end());
  }
  children->reserve(logicalChildren.size());
  for (const auto &child : logicalChildren)
    children->push_back(MakeSchoolDaysGpkUri(child));
  return true;
}

bool ReadFilesystem(std::string_view reference, std::vector<uint8_t> *out,
                    std::string *error) {
  if (out == nullptr)
    return Fail(error, "School Days asset output is null");
  out->clear();
  std::ifstream file(std::filesystem::path(std::string(reference)),
                     std::ios::binary | std::ios::ate);
  if (!file)
    return Fail(error, "School Days asset file could not be opened");
  const std::streamoff end = file.tellg();
  if (end < 0 ||
      static_cast<uintmax_t>(end) >
          static_cast<uintmax_t>(std::numeric_limits<size_t>::max()) ||
      static_cast<uintmax_t>(end) >
          static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
    return Fail(error, "School Days asset file exceeds host read range");
  }
  try {
    out->resize(static_cast<size_t>(end));
  } catch (...) {
    return Fail(error, "School Days asset buffer allocation failed");
  }
  file.seekg(0, std::ios::beg);
  if (!out->empty() && !file.read(reinterpret_cast<char *>(out->data()),
                                  static_cast<std::streamsize>(out->size()))) {
    out->clear();
    return Fail(error, "School Days asset file read failed");
  }
  return true;
}

} // namespace

bool ConfigureSchoolDaysTranslatedGpks(
    const SchoolDaysTranslatedGpkSelection &selection,
    std::string *error) {
  if (error != nullptr) error->clear();
  if (selection.locale.empty() ||
      selection.locale.find_first_of("/\\\r\n") != std::string::npos ||
      selection.locale.find('\0') != std::string::npos)
    return RejectTranslatedConfiguration(
        error, "School Days GPK translation locale is invalid");
  // Only certified, complete native ORS archives may be mounted.
  // The raw Chinese GB18030 patch is rejected by its canonical filename.
  if (selection.locale != "en" && selection.locale != "pt-BR" &&
      selection.locale != "es" && selection.locale != "zh-CN" &&
      selection.locale != "ru")
    return RejectTranslatedConfiguration(
        error, "Unknown or unverified translated GPK locale");
  const bool english = selection.locale == "en";
  if (english && !selection.scriptGpk.empty())
    return RejectTranslatedConfiguration(
        error, "English HQ scripts must come exclusively from Packs/Script.GPK; "
               "do not select a translated English Script.GPK");
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  const bool wantLxh = false;
#else
  const bool wantLxh = selection.enableLxh;
#endif
  if (!wantLxh) {
    if (!selection.lxhGpk.empty())
      return RejectTranslatedConfiguration(
          error, "LxH translation GPK cannot be mounted while LxH endings are disabled");
    if (!english && selection.scriptGpk.empty())
      return RejectTranslatedConfiguration(
          error, "Non-English GPK locale requires its complete Script.GPK");
  } else {
    if (!english && (selection.scriptGpk.empty() || selection.lxhGpk.empty()))
      return RejectTranslatedConfiguration(
          error, "Non-English GPK locale requires both translated "
                 "Script.GPK and LxH GPK when LxH endings are enabled; "
                 "no retail text fallback");
  }
  if (selection.locale == "pt-BR" &&
      (selection.scriptGpk.filename().u8string() != "Script - Brazilian.GPK" ||
       (wantLxh &&
        selection.lxhGpk.filename().u8string() != "pt-BR-lxh.GPK")))
    return RejectTranslatedConfiguration(
        error, wantLxh
                   ? "pt-BR requires Script - Brazilian.GPK and pt-BR-lxh.GPK"
                   : "pt-BR requires Script - Brazilian.GPK");
  if (selection.locale == "es" &&
      (selection.scriptGpk.filename().u8string() != "Script - Spanish.GPK" ||
       (wantLxh &&
        selection.lxhGpk.filename().u8string() != "es-lxh.GPK")))
    return RejectTranslatedConfiguration(
        error, wantLxh
                   ? "es requires certified Script - Spanish.GPK and es-lxh.GPK"
                   : "es requires certified Script - Spanish.GPK");
  if (selection.locale == "zh-CN" &&
      (selection.scriptGpk.filename().u8string() !=
           "Script - chinese-compatible.GPK" ||
       (wantLxh &&
        selection.lxhGpk.filename().u8string() != "zh-CN-lxh.GPK")))
    return RejectTranslatedConfiguration(
        error,
        wantLxh
            ? "zh-CN requires certified Script - chinese-compatible.GPK and "
              "zh-CN-lxh.GPK; raw Chinese archive is unsupported"
            : "zh-CN requires certified Script - chinese-compatible.GPK; "
              "raw Chinese archive is unsupported");
  if (selection.locale == "ru" &&
      (selection.scriptGpk.filename().u8string() != "Script - Russian.GPK" ||
       (wantLxh &&
        selection.lxhGpk.filename().u8string() != "ru-lxh.GPK")))
    return RejectTranslatedConfiguration(
        error, wantLxh
                   ? "ru requires Script - Russian.GPK and ru-lxh.GPK"
                   : "ru requires Script - Russian.GPK");
  const auto validatePath = [&](const std::filesystem::path &path,
                                const char *kind) {
    if (path.empty()) return true;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec)
      return RejectTranslatedConfiguration(
          error, std::string("Selected translated ") + kind +
                     " GPK is missing or not a regular file");
    return true;
  };
  if (!validatePath(selection.scriptGpk, "Script") ||
      !validatePath(selection.lxhGpk, "LxH")) return false;
  std::lock_guard<std::mutex> guard(gMountMutex);
  if (gMount || gIntegratedLxh)
    return Fail(error, "Unmount retail GPKs before changing translated packs");
  gTranslatedSelection = selection;
  gTranslatedSelectionInvalid = false;
  gTranslatedScript.reset();
  gTranslatedLxh.reset();
  return true;
}

bool ClearSchoolDaysTranslatedGpks(std::string *error) {
  if (error != nullptr) error->clear();
  std::lock_guard<std::mutex> guard(gMountMutex);
  if (gMount || gIntegratedLxh)
    return Fail(error, "Unmount retail GPKs before clearing translated packs");
  gTranslatedSelection.reset();
  gTranslatedSelectionInvalid = false;
  gTranslatedScript.reset();
  gTranslatedLxh.reset();
  return true;
}

bool MountSchoolDaysRetailGpks(
    const std::filesystem::path &gameOrPacksRoot, std::string *error,
    std::shared_ptr<const Filesystem::IMount> source,
    SchoolDaysLxhMountMode lxhMode, SchoolDaysLxhMountStatus *lxhStatus) {
  if (error != nullptr)
    error->clear();
  if (lxhStatus != nullptr)
    *lxhStatus = {};
  std::filesystem::path packs = gameOrPacksRoot;
  std::error_code ec;
  if (!source && !std::filesystem::is_regular_file(packs / "BGM.GPK", ec)) {
    ec.clear();
    packs /= "Packs";
  }

  std::optional<SchoolDaysTranslatedGpkSelection> requested;
  {
    std::lock_guard<std::mutex> guard(gMountMutex);
    if (gMount || gIntegratedLxh)
      return Fail(error, "School Days GPKs are already mounted");
    if (gTranslatedSelectionInvalid)
      return Fail(error, "Selected translated GPK configuration failed; "
                         "select valid archives or explicitly clear selection");
    requested = gTranslatedSelection;
  }

  auto candidate = std::make_shared<SchoolDaysGpkAssetStore>();
  if (!candidate->OpenRetailDirectory(packs, error, source))
    return false;

  std::shared_ptr<SchoolDaysGpkArchive> integrated;
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  (void)lxhMode;
  if (lxhStatus != nullptr) {
    lxhStatus->requested = false;
    lxhStatus->mounted = false;
    lxhStatus->notice = "Retail-only build excludes LxH endings";
  }
#else
  const bool wantLxh = lxhMode != SchoolDaysLxhMountMode::Disabled;
  if (lxhStatus != nullptr)
    lxhStatus->requested = wantLxh;
  if (wantLxh) {
    auto candidateLxh = std::make_shared<SchoolDaysGpkArchive>();
    std::string lxhProblem;
    const auto lxhPath =
        source ? std::filesystem::path("LxH.GPK") : packs / "LxH.GPK";
    bool validLxh = candidateLxh->Open(lxhPath, &lxhProblem, source);
    if (!validLxh) {
      lxhProblem = "Packs/LxH.GPK missing or invalid: " + lxhProblem;
    } else {
      const auto expected = SchoolDaysLxhContent::PackEntries();
      if (candidateLxh->EntryCount() != expected.size()) {
        validLxh = false;
        lxhProblem = "Packs/LxH.GPK must contain exactly 28 files";
      } else {
        for (const auto &logical : expected) {
          size_t entryIndex = 0u;
          if (candidateLxh->Lookup(logical, &entryIndex) !=
                  SchoolDaysGpkLookupStatus::Present ||
              entryIndex >= candidateLxh->Index().Entries().size()) {
            validLxh = false;
            lxhProblem = "Packs/LxH.GPK missing or ambiguous file: " + logical;
            break;
          }
          const auto &entry = candidateLxh->Index().Entries()[entryIndex];
          if (!entry.IsRaw() || entry.uncompressedLength != entry.storedLength ||
              entry.storedLength == 0u) {
            validLxh = false;
            lxhProblem =
                "Packs/LxH.GPK must use non-empty seekable raw entry: " +
                logical;
            break;
          }
        }
      }
    }
    if (validLxh) {
      integrated = std::move(candidateLxh);
      if (lxhStatus != nullptr) {
        lxhStatus->mounted = true;
        lxhStatus->notice = "Packs/LxH.GPK mounted (7 epilogues active)";
      }
    } else if (lxhMode == SchoolDaysLxhMountMode::Required) {
      return Fail(error, "Mandatory integrated " + lxhProblem);
    } else if (lxhStatus != nullptr) {
      lxhStatus->mounted = false;
      lxhStatus->notice = lxhProblem;
    }
  } else if (lxhStatus != nullptr) {
    lxhStatus->mounted = false;
    lxhStatus->notice = "LxH endings disabled; mounted 29 original retail GPKs";
  }
#endif
  std::shared_ptr<SchoolDaysGpkArchive> translatedScript;
  std::shared_ptr<SchoolDaysGpkArchive> translatedLxh;
  if (requested) {
    const auto sameArchive = [&](const std::filesystem::path &selected,
                                 const std::filesystem::path &retail) {
      if (selected.empty()) return false;
      std::error_code sameError;
      return std::filesystem::equivalent(selected, retail, sameError) &&
             !sameError;
    };
    if (!requested->scriptGpk.empty()) {
      // The retail Script archive is the reference authority for filenames.
      const SchoolDaysGpkArchive *retail = nullptr;
      for (size_t i = 0; i < SchoolDaysRetailGpkSpecs().size(); ++i)
        if (std::string_view(SchoolDaysRetailGpkSpecs()[i].filename) ==
            "Script.GPK") {
          retail = candidate->Archive(i);
          break;
        }
      if (!retail)
        return Fail(error, "Mounted retail Script.GPK is unavailable");
      if (sameArchive(requested->scriptGpk, packs / "Script.GPK") &&
          requested->locale != "en")
        return Fail(error, "Translated Script.GPK must differ from the retail "
                           "Script.GPK for a non-English locale");
      translatedScript = std::make_shared<SchoolDaysGpkArchive>();
      std::string reason;
      if (!translatedScript->Open(requested->scriptGpk, &reason))
        return Fail(error, "Selected translated Script.GPK is invalid: " + reason);
      if (!ValidateTranslatedScript(*translatedScript, *retail, requested->locale, &reason))
        return Fail(error, "Selected translated Script.GPK rejected: " + reason);
    }
    if (integrated && requested->enableLxh && !requested->lxhGpk.empty()) {
      if (sameArchive(requested->lxhGpk, packs / "LxH.GPK") &&
          requested->locale != "en")
        return Fail(error, "Translated LxH GPK must differ from the original "
                           "LxH.GPK for a non-English locale");
      translatedLxh = std::make_shared<SchoolDaysGpkArchive>();
      std::string reason;
      if (!translatedLxh->Open(requested->lxhGpk, &reason))
        return Fail(error, "Selected translated LxH GPK is invalid: " + reason);
      if (!ValidateTranslatedLxh(*translatedLxh, *integrated, &reason))
        return Fail(error, "Selected translated LxH GPK rejected: " + reason);
    }
  }
  {
    std::lock_guard<std::mutex> guard(gMountMutex);
    // A concurrent configuration change invalidates the pending candidate.
    if (gMount || gIntegratedLxh || gTranslatedSelectionInvalid ||
        !SameSelection(gTranslatedSelection, requested))
      return Fail(error, "School Days GPK language selection changed during mount");
    gMount = std::move(candidate);
    gIntegratedLxh = std::move(integrated);
    gTranslatedScript = std::move(translatedScript);
    gTranslatedLxh = std::move(translatedLxh);
  }
  return true;
}

bool SetSchoolDaysAssetOverlay(const std::filesystem::path &directory,
                               std::string *error) {
  try {
    auto mounted = std::make_shared<AssetOverlay>(directory);
    std::lock_guard<std::mutex> guard(gMountMutex);
    gOverlay = std::move(mounted);
    return true;
  } catch (const std::exception &e) {
    return Fail(error, e.what());
  }
}
bool SchoolDaysAssetOverlaySet() {
  auto overlay = CurrentOverlay();
  return overlay && !overlay->root().empty();
}
bool ResolveSchoolDaysOverlayFile(std::string_view logical,
                                  std::string *physical) {
  // Keep all integrated epilogues in the extra GPK, even when a historical
  // display-options overlay still contains loose copies of those 28 files.
  // Only the packaged installation is authoritative. Extracted developer
  // fixtures can still supply synthetic grafts through a loose overlay.
  if (CurrentIntegratedLxh() && SchoolDaysLxhContent::IsPackResource(logical))
    return false;
  if (CurrentTranslated().script && IsHqScriptOrs(logical))
    return false;
  auto overlay = CurrentOverlay();
  if (!overlay)
    return false;
  auto file = overlay->resolve(std::string(logical));
  // Compatibility names are mapped onto the same indexed engine overlay;
  // there is no separate filesystem script mount.
  if (file.empty()) {
    auto key = AssetOverlay::key(std::string(logical));
    const std::string prefix = "script/english/";
    if (key.compare(0, prefix.size(), prefix) == 0) {
      std::filesystem::path relative(key.substr(prefix.size()));
      auto name = relative.filename().string();
      name.resize(name.find('.') == name.npos ? name.size() : name.find('.'));
      auto stem = (relative.parent_path() / name).generic_string();
      for (const auto &candidate : overlay->list(stem)) {
        std::filesystem::path path(candidate);
        if (path.parent_path() == relative.parent_path() &&
            path.extension() == ".txt") {
          file = overlay->resolve(candidate);
          break;
        }
      }
    }
  }

  if (file.empty())
    return false;
  if (physical)
    *physical = std::move(file);
  return true;
}

void UnmountSchoolDaysRetailGpks() {
  std::lock_guard<std::mutex> guard(gMountMutex);
  gMount.reset();
  gIntegratedLxh.reset();
  gTranslatedScript.reset();
  gTranslatedLxh.reset();
  gTranslatedSelection.reset();
  gTranslatedSelectionInvalid = false;
}

void InstallMountedSchoolDaysAssetResolver() {
  InstallSchoolDaysVirtualAssetLookup(&MountedResolverLookup);
  InstallSchoolDaysVirtualAssetIo(&MountedResolverIsReference,
                                  &MountedResolverRead,
                                  &MountedResolverListDirectory);
}

void UninstallMountedSchoolDaysAssetResolver() {
  if (gSchoolDaysVirtualAssetLookup == &MountedResolverLookup) {
    InstallSchoolDaysVirtualAssetLookup(nullptr);
    InstallSchoolDaysVirtualAssetIo(nullptr, nullptr, nullptr);
  }
}

bool SchoolDaysRetailGpksMounted() {
  const auto mount = CurrentMount();
  return mount != nullptr && mount->IsOpen();
}

bool SchoolDaysLxhMounted() {
  const auto integrated = CurrentIntegratedLxh();
  return integrated != nullptr && integrated->IsOpen();
}

size_t MountedSchoolDaysGpkEntryCount() {
  const auto mount = CurrentMount();
  if (mount == nullptr) return 0u;
  const auto integrated = CurrentIntegratedLxh();
  return mount->EntryCount() +
         (integrated != nullptr ? integrated->EntryCount() : 0u);
}

SchoolDaysGpkLookupStatus
LookupMountedSchoolDaysAsset(std::string_view logicalPath, bool ignoreOverlay) {
  const std::string logical = UnmarkTranslatedLogical(logicalPath);
  const auto translation = CurrentTranslated();
  if (IsMarkedTranslatedScript(logicalPath) && !translation.script)
    return SchoolDaysGpkLookupStatus::Missing;
  if (IsLxhScriptOrs(logical) && translation.lxh)
    return translation.lxh->Lookup(CanonicalGpkLogical(logical));
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  if (const auto integrated = CurrentIntegratedLxh();
      integrated && SchoolDaysLxhContent::IsPackResource(logical))
    return integrated->Lookup(CanonicalGpkLogical(logical));
#endif
  if (!CurrentIntegratedLxh() && IsLxhMediaPath(logical))
    return SchoolDaysGpkLookupStatus::Missing;
  if (IsHqScriptOrs(logical) && translation.script)
    return translation.script->Lookup(ScriptRelative(logical));
  if (IsMarkedTranslatedScript(logicalPath))
    return SchoolDaysGpkLookupStatus::Missing;
  if (!ignoreOverlay && ResolveSchoolDaysOverlayFile(logical, nullptr))
    return SchoolDaysGpkLookupStatus::Present;
  const auto mount = CurrentMount();
  return mount != nullptr ? mount->Lookup(CanonicalGpkLogical(logical))
                          : SchoolDaysGpkLookupStatus::Missing;
}

bool ReadMountedSchoolDaysAsset(std::string_view logicalPath,
                                std::vector<uint8_t> *out, std::string *error,
                                bool ignoreOverlay) {
  const std::string logical = UnmarkTranslatedLogical(logicalPath);
  const auto translation = CurrentTranslated();
  if (IsMarkedTranslatedScript(logicalPath) && !translation.script)
    return Fail(error, "Translated Script.GPK is unavailable; no retail fallback");
  if (IsLxhScriptOrs(logical) && translation.lxh)
    return translation.lxh->Read(CanonicalGpkLogical(logical), out, error);
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  if (const auto integrated = CurrentIntegratedLxh();
      integrated && SchoolDaysLxhContent::IsPackResource(logical))
    return integrated->Read(CanonicalGpkLogical(logical), out, error);
#endif
  if (!CurrentIntegratedLxh() && IsLxhMediaPath(logical))
    return Fail(error, "LxH endings are disabled or Packs/LxH.GPK is not mounted");
  if (IsHqScriptOrs(logical) && translation.script)
    return translation.script->Read(ScriptRelative(logical), out, error);
  if (IsMarkedTranslatedScript(logicalPath))
    return Fail(error, "Translated Script.GPK resource is invalid");
  std::string physical;
  if (!ignoreOverlay && ResolveSchoolDaysOverlayFile(logical, &physical))
    return ReadFilesystem(physical, out, error);
  const auto mount = CurrentMount();
  if (mount == nullptr)
    return Fail(error, "School Days retail GPKs are not mounted");
  return mount->Read(CanonicalGpkLogical(logical), out, error);
}

std::unique_ptr<AssetStream> OpenMountedSchoolDaysAssetStream(
    std::string_view logicalPath, bool ignoreOverlay) {
    const std::string logical = UnmarkTranslatedLogical(logicalPath);
    const auto translation = CurrentTranslated();
    const auto openRaw = [](const std::shared_ptr<const SchoolDaysGpkArchive> &archive,
                            const std::string &relative) -> std::unique_ptr<AssetStream> {
      if (!archive) return {};
      size_t idx = 0u;
      if (archive->Lookup(relative, &idx) != SchoolDaysGpkLookupStatus::Present ||
          idx >= archive->Index().Entries().size()) return {};
      const auto &entry = archive->Index().Entries()[idx];
      const uint64_t end = static_cast<uint64_t>(entry.offset) +
                           static_cast<uint64_t>(entry.PhysicalBodyLength());
      if (!entry.IsRaw() || end < entry.offset || end > archive->PidxOffset() ||
          entry.inlinePrefix.size() > entry.storedLength) return {};
      try {
        return std::make_unique<MountedRawAssetStream>(
            archive, archive->OpenStream(), entry.inlinePrefix, entry.offset,
            static_cast<uint64_t>(entry.storedLength));
      } catch (...) { return {}; }
    };
    if (IsMarkedTranslatedScript(logicalPath) && !translation.script) return {};
    if (IsLxhScriptOrs(logical) && translation.lxh)
      return openRaw(translation.lxh, CanonicalGpkLogical(logical));
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    if (const auto integrated = CurrentIntegratedLxh();
        integrated && SchoolDaysLxhContent::IsPackResource(logical))
      return openRaw(integrated, CanonicalGpkLogical(logical));
#endif
    if (!CurrentIntegratedLxh() && IsLxhMediaPath(logical))
      return {};
    if (IsHqScriptOrs(logical) && translation.script)
      return openRaw(translation.script, ScriptRelative(logical));
    if (IsMarkedTranslatedScript(logicalPath) ||
        (!ignoreOverlay && ResolveSchoolDaysOverlayFile(logical, nullptr))) return {};
    const auto mount = CurrentMount();
    if (mount == nullptr) return {};
    size_t archiveIndex = 0u;
    size_t entryIndex = 0u;
    const std::string canonical = CanonicalGpkLogical(logical);
    if (mount->Lookup(canonical, &archiveIndex, &entryIndex) !=
        SchoolDaysGpkLookupStatus::Present) {
        return {};
    }
    const SchoolDaysGpkArchive* archive = mount->Archive(archiveIndex);
    if (archive == nullptr || entryIndex >= archive->Index().Entries().size()) return {};
    const auto& entry = archive->Index().Entries()[entryIndex];
    if (!entry.IsRaw()) return {};
    const uint64_t bodySize = static_cast<uint64_t>(entry.PhysicalBodyLength());
    const uint64_t physicalEnd = static_cast<uint64_t>(entry.offset) + bodySize;
    if (physicalEnd < entry.offset || physicalEnd > archive->PidxOffset() ||
        entry.inlinePrefix.size() > entry.storedLength) {
        return {};
    }
    try {
      return std::make_unique<MountedRawAssetStream>(
          mount, archive->OpenStream(), entry.inlinePrefix, entry.offset,
          static_cast<uint64_t>(entry.storedLength));
    } catch (...) {
        return {};
    }
}

std::string MakeSchoolDaysGpkUri(std::string_view logicalPath) {
  std::string result(kSchoolDaysGpkUriPrefix);
  // Preserve the opaque "/__sd_localized_script__" marker through
  // SchoolDaysLogicalFromGpkUri so the AssetProvider cannot shadow selected
  // translated ORS with a loose Script/English file from an old overlay.
  const bool mark = IsHqScriptOrs(logicalPath) && CurrentTranslated().script &&
                    !IsMarkedTranslatedScript(logicalPath);
  result.reserve(result.size() + logicalPath.size() +
                 (mark ? kTranslatedScriptVirtualSuffix.size() : 0u));
  for (char ch : logicalPath)
    result.push_back(ch == '\\' ? '/' : ch);
  if (mark)
    result.append(kTranslatedScriptVirtualSuffix.data(),
                  kTranslatedScriptVirtualSuffix.size());
  return result;
}

bool IsSchoolDaysGpkUri(std::string_view reference) {
  return reference.size() > kSchoolDaysGpkUriPrefix.size() &&
         reference.substr(0u, kSchoolDaysGpkUriPrefix.size()) ==
             kSchoolDaysGpkUriPrefix;
}

bool SchoolDaysLogicalFromGpkUri(std::string_view reference,
                                 std::string *logicalPath) {
  if (logicalPath == nullptr || !IsSchoolDaysGpkUri(reference))
    return false;
  logicalPath->assign(reference.substr(kSchoolDaysGpkUriPrefix.size()));
  return !logicalPath->empty();
}

bool ReadSchoolDaysAssetRef(std::string_view reference,
                            std::vector<uint8_t> *out, std::string *error) {
  if (error != nullptr)
    error->clear();
  std::string logical;
  if (SchoolDaysLogicalFromGpkUri(reference, &logical))
    return ReadMountedSchoolDaysAsset(logical, out, error);
  return ReadFilesystem(reference, out, error);
}

} // namespace Kotonoha
