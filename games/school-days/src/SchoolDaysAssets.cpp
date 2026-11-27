#include "SchoolDaysAssets.hpp"
#include "SchoolDaysOverlaySelection.hpp"

#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <algorithm>
#include <cctype>
#include <functional>
#include <stdexcept>

namespace SchoolDays {
namespace {
std::filesystem::path ScriptRoot(const std::filesystem::path &root) {
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
    if (!Kotonoha::SchoolDaysAsciiCaseEqual(entry.path().filename().string(),
                                            "Script") ||
        !entry.is_directory(ec))
      continue;
    for (const auto &language :
         std::filesystem::directory_iterator(entry.path(), ec))
      if (Kotonoha::SchoolDaysAsciiCaseEqual(
              language.path().filename().string(), "English") &&
          language.is_directory(ec))
        return language.path();
  }
  return {};
}

bool HasAsciiCaseInsensitivePrefix(const std::string &value,
                                   const std::string &prefix) {
  return value.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), value.begin(),
                    [](unsigned char a, unsigned char b) {
                      return std::tolower(a) == std::tolower(b);
                    });
}

std::filesystem::path ResolveLooseOriginal(const std::filesystem::path &root,
                                           const std::string &logical) {
  const std::filesystem::path requested(logical);
  if (requested.empty() || requested.is_absolute()) return {};
  std::filesystem::path current = root;
  std::error_code ec;
  for (const auto &part : requested) {
    const auto name = part.string();
    if (name.empty() || name == ".") continue;
    if (name == "..") return {};
    std::filesystem::path match;
    for (const auto &entry : std::filesystem::directory_iterator(current, ec)) {
      if (ec) return {};
      if (!Kotonoha::SchoolDaysAsciiCaseEqual(entry.path().filename().string(), name))
        continue;
      if (!match.empty()) return {};
      match = entry.path();
    }
    if (match.empty()) return {};
    current = std::move(match);
  }
  if (!std::filesystem::is_regular_file(current, ec) || ec) return {};
  const auto canonicalRoot = std::filesystem::canonical(root, ec);
  if (ec) return {};
  const auto canonicalFile = std::filesystem::canonical(current, ec);
  if (ec) return {};
  const auto relative = canonicalFile.lexically_relative(canonicalRoot);
  return relative.empty() || *relative.begin() == ".."
             ? std::filesystem::path{} : canonicalFile;
}

// Keep the launcher and gameplay on the same external overlay. Never rewrite
// the retail System.GPK, and always honor an explicitly selected directory.
std::string SelectDisplayOptionsOverlay(const std::string &requested,
                                        bool allowAutomaticOverlay) {
  std::filesystem::path executableRoot;
#if defined(_WIN32) && !defined(__ANDROID__)
  if (const char *base = SDL_GetBasePath(); base && *base)
    executableRoot = base;
#endif
  return OverlaySelection::Select(executableRoot, requested,
                                  allowAutomaticOverlay).string();
}
}

Assets::Assets(std::filesystem::path root, const std::string &scripts,
               bool allowAutomaticOverlay,
               std::shared_ptr<const Kotonoha::Filesystem::IMount> packs,
               Kotonoha::SchoolDaysLxhMountMode lxhMode)
    : directory(std::move(root)) {
  const std::string effectiveOverlay =
      SelectDisplayOptionsOverlay(scripts, allowAutomaticOverlay);
  if (!effectiveOverlay.empty() && !std::filesystem::is_directory(effectiveOverlay))
    throw std::runtime_error("School Days script directory does not exist");
  setOverlay(effectiveOverlay);
  std::string info;
  if (!packs && !checkDirectory(directory, info))
    throw std::runtime_error(info);
  if (packs || ScriptRoot(directory).empty()) {
    Kotonoha::SchoolDaysLxhMountStatus status;
    if (!Kotonoha::MountSchoolDaysRetailGpks(directory, &info,
                                             std::move(packs), lxhMode, &status))
      throw std::runtime_error(info);
    mounted = true;
    lxhActive = status.mounted;
    lxhNotice = std::move(status.notice);
    Kotonoha::InstallMountedSchoolDaysAssetResolver();
  }
  if (!effectiveOverlay.empty() &&
      !Kotonoha::SetSchoolDaysAssetOverlay(effectiveOverlay, &info)) {
    if (mounted) {
      Kotonoha::UninstallMountedSchoolDaysAssetResolver();
      Kotonoha::UnmountSchoolDaysRetailGpks();
    }
    throw std::runtime_error(info);
  }
}

std::filesystem::path Assets::orsRoot() const {
  const auto scripts = ScriptRoot(directory);
  return scripts.empty() ? directory : scripts;
}

Assets::~Assets() {
  clearCache();
  if (mounted) {
    Kotonoha::UninstallMountedSchoolDaysAssetResolver();
    Kotonoha::UnmountSchoolDaysRetailGpks();
  }
  Kotonoha::SetSchoolDaysAssetOverlay({});
}

std::string Assets::resolve(const std::string &logical, bool image) const {
  if (Kotonoha::IsSchoolDaysGpkUri(logical)) return logical;
  // Extracted developer fixtures may provide authored epilogues in their
  // selected script overlay. Packaged installations never use this path:
  // the mandatory GPK owns all 28 integrated LxH resources.
  std::string looseOverlay;
  if (!mounted && Kotonoha::ResolveSchoolDaysOverlayFile(logical, &looseOverlay))
    return looseOverlay;
  // ImageAssetResolver accepts a stem and appends .PNG. References that
  // already end in .png (including integrated cards and display options)
  // must resolve as exact logical paths rather than "name.png.PNG".
  const bool hasPngExtension = logical.size() >= 4u &&
      Kotonoha::SchoolDaysAsciiCaseEqual(logical.substr(logical.size() - 4u),
                                         ".png");
  auto found = image && !hasPngExtension
      ? Kotonoha::ResolveSchoolDaysImageAsset(directory.string().c_str(), logical)
      : Kotonoha::ResolveSchoolDaysAssetPath(directory.string().c_str(), logical);
  // The retail ORS 03-BK-H00 contains one accidental space in this voice path.
  // Its correctly named file exists in Voice03.GPK. Keep an exact file or
  // overlay with the scripted spelling if one is supplied.
  if (!image && found.status == Kotonoha::SchoolDaysAssetResolutionStatus::Missing &&
      logical == "Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG")
    found = Kotonoha::ResolveSchoolDaysAssetPath(
        directory.string().c_str(),
        "Voice03/03-BK/03-BK-H00/03-BK-H00-0410.OGG");
  if (!found.present || found.collision) {
    const char *reason = "Invalid";
    switch (found.status) {
    case Kotonoha::SchoolDaysAssetResolutionStatus::Missing: reason = "Missing"; break;
    case Kotonoha::SchoolDaysAssetResolutionStatus::Collision: reason = "Ambiguous"; break;
    case Kotonoha::SchoolDaysAssetResolutionStatus::Invalid: reason = "Invalid"; break;
    case Kotonoha::SchoolDaysAssetResolutionStatus::Present: reason = "Unreadable"; break;
    }
    throw std::runtime_error(std::string(reason) + " School Days asset: " + logical);
  }
  return found.physical;
}

std::string Assets::resolveOriginal(const std::string &logical) const {
  if (Kotonoha::IsSchoolDaysGpkUri(logical)) return logical;
  const std::filesystem::path supplied(logical);
  if (supplied.is_absolute()) {
    std::error_code ec;
    const auto root = std::filesystem::canonical(directory, ec);
    if (ec) throw std::runtime_error("Invalid original School Days root");
    const auto file = std::filesystem::canonical(supplied, ec);
    const auto relative = file.lexically_relative(root);
    if (!ec && !relative.empty() && *relative.begin() != ".." &&
        std::filesystem::is_regular_file(file, ec))
      return file.string();
    throw std::runtime_error("Invalid original School Days asset: " + logical);
  }
  if (mounted) {
    auto original = logical;
    if (Kotonoha::LookupMountedSchoolDaysAsset(original, true) !=
            Kotonoha::SchoolDaysGpkLookupStatus::Present &&
        original == "Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG")
      original = "Voice03/03-BK/03-BK-H00/03-BK-H00-0410.OGG";
    if (Kotonoha::LookupMountedSchoolDaysAsset(original, true) ==
        Kotonoha::SchoolDaysGpkLookupStatus::Present)
      return Kotonoha::MakeSchoolDaysGpkUri(original);
  }
  auto file = ResolveLooseOriginal(directory, logical);
  if (file.empty() && logical == "Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG")
    file = ResolveLooseOriginal(directory,
        "Voice03/03-BK/03-BK-H00/03-BK-H00-0410.OGG");
  if (!file.empty()) return file.string();
  throw std::runtime_error("Missing original School Days asset: " + logical);
}

Kotonoha::AudioClip Assets::loadAudioAsset(const std::string &id) const {
  return Kotonoha::MediaDecoder::decodeAudio(readOriginalAsset(id));
}

bool Assets::containsOriginalAsset(const std::string &id) const {
  try {
    return containsAsset(resolveOriginal(id));
  } catch (const std::exception &) {
    return false;
  }
}

std::vector<Uint8> Assets::readOriginalAsset(const std::string &id) const {
  const auto original = resolveOriginal(id);
  std::string logical;
  if (!Kotonoha::SchoolDaysLogicalFromGpkUri(original, &logical))
    return readAsset(original);
  std::vector<Uint8> bytes;
  std::string error;
  if (!Kotonoha::ReadMountedSchoolDaysAsset(logical, &bytes, &error, true))
    throw std::runtime_error(error + ": " + id);
  return bytes;
}

std::string Assets::overlayId(const std::string &id) const {
  std::string logical;
  if (Kotonoha::SchoolDaysLogicalFromGpkUri(id, &logical)) {
    // AssetProvider::read() checks its own overlay BEFORE readAsset().
    // When LxH is mounted, the mounted pack wins even if an old loose file
    // exists in a custom overlay. When LxH is disabled, never let a loose
    // overlay re-inject LxH media or epilogue ORS.
    if (lxhActive && Kotonoha::SchoolDaysLxhContent::IsPackResource(logical))
      return {};
    if (!lxhActive && HasAsciiCaseInsensitivePrefix(logical, "LxH/"))
      return {};
    return logical;
  }
  if (mounted &&
      ((lxhActive && Kotonoha::SchoolDaysLxhContent::IsPackResource(id)) ||
       (!lxhActive && HasAsciiCaseInsensitivePrefix(id, "LxH/"))))
    return {};
  std::filesystem::path file(id);
  if (file.is_absolute()) {
    auto rel = file.lexically_relative(directory);
    return rel.generic_string();
  }
  return id;
}
bool Assets::containsAsset(const std::string &id) const {
  if (Kotonoha::IsSchoolDaysGpkUri(id)) {
    std::string logical;
    return Kotonoha::SchoolDaysLogicalFromGpkUri(id, &logical) &&
           Kotonoha::LookupMountedSchoolDaysAsset(logical) ==
               Kotonoha::SchoolDaysGpkLookupStatus::Present;
  }
  std::error_code ec;
  return std::filesystem::is_regular_file(id, ec);
}

std::vector<Uint8> Assets::readAsset(const std::string &id) const {
  std::vector<Uint8> bytes;
  std::string error;
  if (!Kotonoha::ReadSchoolDaysAssetRef(id, &bytes, &error))
    throw std::runtime_error(error + ": " + id);
  return bytes;
}

std::vector<std::string> Assets::listAsset(const std::string &prefix) const {
  std::vector<std::string> result;

  const auto appendVirtualTree = [&result](const std::string &root,
                                            const std::string &filter) {
    std::function<void(const std::string &)> walk;
    walk = [&](const std::string &directoryRef) {
      std::vector<std::string> children;
      if (!Kotonoha::ListSchoolDaysAssetDirectoryReference(directoryRef, &children))
        return;
      for (const auto &child : children) {
        std::vector<std::string> descendants;
        if (Kotonoha::ListSchoolDaysAssetDirectoryReference(child, &descendants)) {
          walk(child);
          continue;
        }
        std::string logical;
        if (!Kotonoha::SchoolDaysLogicalFromGpkUri(child, &logical)) continue;
        if (HasAsciiCaseInsensitivePrefix(logical, filter))
          result.push_back(child);
      }
    };
    walk(root);
  };

  if (Kotonoha::IsSchoolDaysGpkUri(prefix)) {
    std::string logical;
    if (!Kotonoha::SchoolDaysLogicalFromGpkUri(prefix, &logical)) return result;
    while (!logical.empty() && logical.back() == '/') logical.pop_back();
    std::string directoryLogical = logical;
    std::vector<std::string> probe;
    if (!Kotonoha::ListSchoolDaysAssetDirectoryReference(
            Kotonoha::MakeSchoolDaysGpkUri(directoryLogical), &probe)) {
      const auto slash = directoryLogical.rfind('/');
      if (slash == std::string::npos) return result;
      directoryLogical.resize(slash);
    }
    appendVirtualTree(Kotonoha::MakeSchoolDaysGpkUri(directoryLogical), logical);
    std::sort(result.begin(), result.end());
    return result;
  }

  // An absolute path may point to a locally installed extension outside the
  // retail GPK root. Do not reinterpret it as a virtual GPK prefix.
  if (mounted && !std::filesystem::path(prefix).is_absolute()) {
    std::string normalized = prefix;
    for (char &ch : normalized)
      if (ch == '\\') ch = '/';
    while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
    std::string directoryLogical = normalized;
    while (!directoryLogical.empty() && directoryLogical.back() == '/')
      directoryLogical.pop_back();
    std::vector<std::string> probe;
    bool foundDirectory = !directoryLogical.empty() &&
        Kotonoha::ListSchoolDaysAssetDirectoryReference(
            Kotonoha::MakeSchoolDaysGpkUri(directoryLogical), &probe);
    if (!foundDirectory) {
      const auto slash = directoryLogical.rfind('/');
      if (slash != std::string::npos) {
        directoryLogical.resize(slash);
        foundDirectory = Kotonoha::ListSchoolDaysAssetDirectoryReference(
            Kotonoha::MakeSchoolDaysGpkUri(directoryLogical), &probe);
      }
    }
    if (foundDirectory) {
      appendVirtualTree(Kotonoha::MakeSchoolDaysGpkUri(directoryLogical), normalized);
    } else if (normalized.empty()) {
      for (const auto &spec : Kotonoha::SchoolDaysRetailGpkSpecs())
        appendVirtualTree(Kotonoha::MakeSchoolDaysGpkUri(spec.logicalPrefix), normalized);
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
      if (lxhActive)
        appendVirtualTree(Kotonoha::MakeSchoolDaysGpkUri("LxH"), normalized);
#endif
    }
  }

  std::error_code ec;
  const auto absolutePrefix = std::filesystem::path(prefix).is_absolute();
  std::string normalizedAbsolutePrefix;
  std::filesystem::path enumerationRoot = directory;
  if (absolutePrefix) {
    const bool directoryPrefix = !prefix.empty() &&
        (prefix.back() == '/' || prefix.back() == '\\');
    auto requested = std::filesystem::path(prefix).lexically_normal();
    requested.make_preferred();
    normalizedAbsolutePrefix = requested.string();
    if (directoryPrefix && !normalizedAbsolutePrefix.empty() &&
        normalizedAbsolutePrefix.back() != std::filesystem::path::preferred_separator) {
      normalizedAbsolutePrefix.push_back(std::filesystem::path::preferred_separator);
    }
    auto normalizedRetail = std::filesystem::absolute(directory).lexically_normal();
    normalizedRetail.make_preferred();
    std::string retailPrefix = normalizedRetail.string();
    const bool equalsRetail = Kotonoha::SchoolDaysAsciiCaseEqual(
        normalizedAbsolutePrefix, retailPrefix);
    if (!retailPrefix.empty() &&
        retailPrefix.back() != std::filesystem::path::preferred_separator)
      retailPrefix.push_back(std::filesystem::path::preferred_separator);
    if (!equalsRetail &&
        !HasAsciiCaseInsensitivePrefix(normalizedAbsolutePrefix, retailPrefix)) {
      // Overlay movies and audio can live next to the executable while the
      // original GPKs live elsewhere. Enumerate only the relevant subtree.
      enumerationRoot = requested;
      if (!std::filesystem::is_directory(enumerationRoot, ec))
        enumerationRoot = enumerationRoot.parent_path();
      ec.clear();
      if (!std::filesystem::is_directory(enumerationRoot, ec))
        return result;
    }
  }
  for (std::filesystem::recursive_directory_iterator it(enumerationRoot, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const auto physical = it->path().lexically_normal().string();
    const auto relative = it->path().lexically_relative(directory).generic_string();
    const auto &candidate = absolutePrefix ? physical : relative;
    const auto &filter = absolutePrefix ? normalizedAbsolutePrefix : prefix;
    if (HasAsciiCaseInsensitivePrefix(candidate, filter))
      result.push_back(physical);
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::string Assets::localPathAsset(const std::string &id) const {
  return Kotonoha::IsSchoolDaysGpkUri(id) ? std::string() : id;
}

std::unique_ptr<Kotonoha::AssetStream> Assets::openStream(const std::string &id) const {
  std::string logical;
  if (!Kotonoha::SchoolDaysLogicalFromGpkUri(id, &logical)) return {};
  return Kotonoha::OpenMountedSchoolDaysAssetStream(logical);
}

std::unique_ptr<Kotonoha::AssetStream> Assets::openOriginalStream(const std::string &id) const {
  const auto original = resolveOriginal(id);
  std::string logical;
  if (!Kotonoha::SchoolDaysLogicalFromGpkUri(original, &logical)) return {};
  return Kotonoha::OpenMountedSchoolDaysAssetStream(logical, true);
}

bool Assets::checkDirectory(const std::filesystem::path &root, std::string &info) {
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec)) {
    info = "School Days directory not found: " + root.string();
    return false;
  }
  if (!ScriptRoot(root).empty()) {
    info = "Extracted School Days Script/English directory found";
    return true;
  }
  if (Kotonoha::Filesystem::remoteGameAssets()) {
    auto vfs = Kotonoha::Filesystem::mountGameAssets("/game-assets");
    for (const char *prefix : {"", "Packs/"}) {
      const std::string name = std::string(prefix) + "BGM.GPK";
      if (vfs->exists(name)) {
        info = "School Days retail GPK data found via HTTP VFS (" + name +
               "); remaining packs validated on mount";
        return true;
      }
    }
  }
  std::vector<std::filesystem::path> candidates = {root};
  const auto resolvedPacks =
      Kotonoha::ResolveSchoolDaysAssetPath(root.string().c_str(), "Packs");
  if (resolvedPacks.present)
    candidates.emplace_back(resolvedPacks.physical);
  else
    candidates.push_back(root / "Packs");
  for (const auto &candidate : candidates) {
    if (Kotonoha::ResolveSchoolDaysAssetPath(candidate.string().c_str(),
                                             "BGM.GPK")
            .present) {
      for (const auto &spec : Kotonoha::SchoolDaysRetailGpkSpecs())
        if (!Kotonoha::ResolveSchoolDaysAssetPath(candidate.string().c_str(),
                                                  spec.filename)
                 .present) {
          info = std::string("Incomplete retail pack set: missing ") +
                 spec.filename;
          return false;
        }
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
      if (Kotonoha::ResolveSchoolDaysAssetPath(candidate.string().c_str(),
                                               "LxH.GPK")
              .present)
        info = "School Days HQ 29 original retail GPKs + optional LxH.GPK found";
      else
        info = "School Days HQ 29 original retail GPKs found (optional LxH.GPK not installed)";
#else
      info = "School Days HQ 29 original retail GPKs found (no LxH)";
#endif
      return true;
    }
  }
  info = "Select the game directory with Script/English or the complete retail "
         "Packs directory";
  return false;
}

std::filesystem::path Assets::defaultDirectory() {
  std::string info;
  for (const auto &candidate :
       Kotonoha::applicationDirectories("school-days").gameAssets)
    if (checkDirectory(candidate, info))
      return candidate;
  return Kotonoha::gameAssetDirectory("school-days");
}

std::filesystem::path Assets::findDefaultRoute() {
  const auto route = Kotonoha::FileResolver::findGameAsset(
      "school-days", "school-days-hq.ktnroute");
  return route ? *route : std::filesystem::path();
}

std::filesystem::path Assets::defaultRoute() {
  const auto route = findDefaultRoute();
  if (!route.empty())
    return route;
  throw std::runtime_error("school-days-hq.ktnroute not found; supply --ktrf");
}

} // namespace SchoolDays
