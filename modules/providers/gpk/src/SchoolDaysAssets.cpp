#include <SchoolDays/SchoolDaysAssets.hpp>

#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <algorithm>
#include <cctype>
#include <functional>
#include <stdexcept>

namespace Kotonoha::Providers {
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
  if (requested.empty() || requested.is_absolute())
    return {};

  std::filesystem::path current = root;
  std::error_code ec;
  for (const auto &part : requested) {
    const auto name = part.string();
    if (name.empty() || name == ".")
      continue;
    if (name == "..")
      return {};
    std::filesystem::path match;
    for (const auto &entry : std::filesystem::directory_iterator(current, ec)) {
      if (ec)
        return {};
      if (!Kotonoha::SchoolDaysAsciiCaseEqual(entry.path().filename().string(),
                                              name))
        continue;
      if (!match.empty())
        return {};
      match = entry.path();
    }
    if (match.empty())
      return {};
    current = std::move(match);
  }
  return std::filesystem::is_regular_file(current, ec) && !ec
             ? current
             : std::filesystem::path{};
}

} // namespace

SchoolDaysAssets::SchoolDaysAssets(std::filesystem::path root,
                                   const std::string &overlay)
    : directory(std::move(root)) {
  const std::string effectiveOverlay = overlay;
  if (!effectiveOverlay.empty() &&
      !std::filesystem::is_directory(effectiveOverlay))
    throw std::runtime_error("School Days overlay directory does not exist");
  setOverlay(effectiveOverlay);
  std::string info;
  if (!checkDirectory(directory, info))
    throw std::runtime_error(info);
  if (ScriptRoot(directory).empty()) {
    if (!Kotonoha::MountSchoolDaysRetailGpks(directory, &info))
      throw std::runtime_error(info);
    mounted = true;
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

std::filesystem::path SchoolDaysAssets::orsRoot() const {
  const auto scripts = ScriptRoot(directory);
  return scripts.empty() ? directory : scripts;
}

SchoolDaysAssets::~SchoolDaysAssets() {
  clearCache();
  if (mounted) {
    Kotonoha::UninstallMountedSchoolDaysAssetResolver();
    Kotonoha::UnmountSchoolDaysRetailGpks();
  }
  Kotonoha::SetSchoolDaysAssetOverlay({});
}

std::string SchoolDaysAssets::resolve(const std::string &logical,
                                      bool image) const {
  if (Kotonoha::IsSchoolDaysGpkUri(logical))
    return logical;
  auto found = image ? Kotonoha::ResolveSchoolDaysImageAsset(
                           directory.string().c_str(), logical)
                     : Kotonoha::ResolveSchoolDaysAssetPath(
                           directory.string().c_str(), logical);
  // The retail ORS 03-BK-H00 contains one accidental space in this voice path.
  // Its correctly named file exists in Voice03.GPK. Keep an exact file or
  // overlay with the scripted spelling if one is supplied.
  if (!image &&
      found.status == Kotonoha::SchoolDaysAssetResolutionStatus::Missing &&
      logical == "Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG")
    found = Kotonoha::ResolveSchoolDaysAssetPath(
        directory.string().c_str(),
        "Voice03/03-BK/03-BK-H00/03-BK-H00-0410.OGG");
  if (!found.present || found.collision) {
    const char *reason = "Invalid";
    switch (found.status) {
    case Kotonoha::SchoolDaysAssetResolutionStatus::Missing:
      reason = "Missing";
      break;
    case Kotonoha::SchoolDaysAssetResolutionStatus::Collision:
      reason = "Ambiguous";
      break;
    case Kotonoha::SchoolDaysAssetResolutionStatus::Invalid:
      reason = "Invalid";
      break;
    case Kotonoha::SchoolDaysAssetResolutionStatus::Present:
      reason = "Unreadable";
      break;
    }
    throw std::runtime_error(std::string(reason) +
                             " School Days asset: " + logical);
  }
  return found.physical;
}

std::string SchoolDaysAssets::overlayId(const std::string &id) const {
  std::string logical;
  if (Kotonoha::SchoolDaysLogicalFromGpkUri(id, &logical))
    return logical;
  std::filesystem::path file(id);
  if (file.is_absolute()) {
    auto rel = file.lexically_relative(directory);
    return rel.generic_string();
  }
  return id;
}

Kotonoha::AudioClip
SchoolDaysAssets::loadAudioAsset(const std::string &id) const {
  // Runtime playback resolves logical ORS paths before handing them to the
  // media player. Debug/audio tools operate on the logical ID, so normalize it
  // through the exact same resolver before the base decoder reads GPK bytes.
  return Kotonoha::AssetProvider::loadAudioAsset(resolveOriginal(id));
}

std::string
SchoolDaysAssets::resolveOriginal(const std::string &logical) const {
  if (Kotonoha::IsSchoolDaysGpkUri(logical))
    return logical;

  const std::filesystem::path supplied(logical);
  if (supplied.is_absolute()) {
    std::error_code ec;
    const auto relative = supplied.lexically_relative(directory);
    if (!relative.empty() && *relative.begin() != ".." &&
        std::filesystem::is_regular_file(supplied, ec) && !ec)
      return supplied.string();
    throw std::runtime_error("Invalid original School Days asset: " + logical);
  }

  // Never call ResolveSchoolDaysAssetPath here: its process-wide resolver is
  // intentionally overlay-aware. Debug dubbing needs the immutable retail
  // side even while an overlay is mounted.
  if (mounted) {
    auto originalLogical = logical;
    if (Kotonoha::LookupMountedSchoolDaysAsset(originalLogical, true) !=
            Kotonoha::SchoolDaysGpkLookupStatus::Present &&
        originalLogical == "Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG")
      originalLogical = "Voice03/03-BK/03-BK-H00/03-BK-H00-0410.OGG";
    if (Kotonoha::LookupMountedSchoolDaysAsset(originalLogical, true) ==
        Kotonoha::SchoolDaysGpkLookupStatus::Present)
      return Kotonoha::MakeSchoolDaysGpkUri(originalLogical);
  }

  auto file = ResolveLooseOriginal(directory, logical);
  if (file.empty() && logical == "Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG")
    file = ResolveLooseOriginal(directory,
                                "Voice03/03-BK/03-BK-H00/03-BK-H00-0410.OGG");
  if (!file.empty())
    return file.string();
  throw std::runtime_error("Missing original School Days asset: " + logical);
}

bool SchoolDaysAssets::containsAsset(const std::string &id) const {
  if (Kotonoha::IsSchoolDaysGpkUri(id)) {
    std::string logical;
    return Kotonoha::SchoolDaysLogicalFromGpkUri(id, &logical) &&
           Kotonoha::LookupMountedSchoolDaysAsset(logical) ==
               Kotonoha::SchoolDaysGpkLookupStatus::Present;
  }
  std::error_code ec;
  return std::filesystem::is_regular_file(id, ec);
}

bool SchoolDaysAssets::containsOriginalAsset(const std::string &id) const {
  try {
    return containsAsset(resolveOriginal(id));
  } catch (const std::exception &) {
    return false;
  }
}

std::vector<Uint8> SchoolDaysAssets::readAsset(const std::string &id) const {
  std::vector<Uint8> bytes;
  std::string error;
  if (!Kotonoha::ReadSchoolDaysAssetRef(id, &bytes, &error))
    throw std::runtime_error(error + ": " + id);
  return bytes;
}

std::vector<Uint8>
SchoolDaysAssets::readOriginalAsset(const std::string &id) const {
  return readAsset(resolveOriginal(id));
}

std::vector<std::string>
SchoolDaysAssets::listAsset(const std::string &prefix) const {
  std::vector<std::string> result;

  const auto appendVirtualTree = [&result](const std::string &root,
                                           const std::string &filter) {
    std::function<void(const std::string &)> walk;
    walk = [&](const std::string &directoryRef) {
      std::vector<std::string> children;
      if (!Kotonoha::ListSchoolDaysAssetDirectoryReference(directoryRef,
                                                           &children))
        return;
      for (const auto &child : children) {
        std::vector<std::string> descendants;
        if (Kotonoha::ListSchoolDaysAssetDirectoryReference(child,
                                                            &descendants)) {
          walk(child);
          continue;
        }
        std::string logical;
        if (!Kotonoha::SchoolDaysLogicalFromGpkUri(child, &logical))
          continue;
        if (HasAsciiCaseInsensitivePrefix(logical, filter))
          result.push_back(child);
      }
    };
    walk(root);
  };

  if (Kotonoha::IsSchoolDaysGpkUri(prefix)) {
    std::string logical;
    if (!Kotonoha::SchoolDaysLogicalFromGpkUri(prefix, &logical))
      return result;
    while (!logical.empty() && logical.back() == '/')
      logical.pop_back();
    std::string directoryLogical = logical;
    std::vector<std::string> probe;
    if (!Kotonoha::ListSchoolDaysAssetDirectoryReference(
            Kotonoha::MakeSchoolDaysGpkUri(directoryLogical), &probe)) {
      const auto slash = directoryLogical.rfind('/');
      if (slash == std::string::npos)
        return result;
      directoryLogical.resize(slash);
    }
    appendVirtualTree(Kotonoha::MakeSchoolDaysGpkUri(directoryLogical),
                      logical);
    std::sort(result.begin(), result.end());
    return result;
  }

  if (mounted) {
    std::string normalized = prefix;
    for (char &ch : normalized)
      if (ch == '\\')
        ch = '/';
    while (!normalized.empty() && normalized.front() == '/')
      normalized.erase(normalized.begin());
    std::string directoryLogical = normalized;
    while (!directoryLogical.empty() && directoryLogical.back() == '/')
      directoryLogical.pop_back();
    std::vector<std::string> probe;
    bool foundDirectory =
        !directoryLogical.empty() &&
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
      appendVirtualTree(Kotonoha::MakeSchoolDaysGpkUri(directoryLogical),
                        normalized);
    } else if (normalized.empty()) {
      for (const auto &spec : Kotonoha::SchoolDaysRetailGpkSpecs())
        appendVirtualTree(Kotonoha::MakeSchoolDaysGpkUri(spec.logicalPrefix),
                          normalized);
    }
  }

  std::error_code ec;
  const auto absolutePrefix = std::filesystem::path(prefix).is_absolute();
  std::string normalizedAbsolutePrefix;
  if (absolutePrefix) {
    const bool directoryPrefix =
        !prefix.empty() && (prefix.back() == '/' || prefix.back() == '\\');
    normalizedAbsolutePrefix =
        std::filesystem::path(prefix).lexically_normal().string();
    if (directoryPrefix && !normalizedAbsolutePrefix.empty() &&
        normalizedAbsolutePrefix.back() !=
            std::filesystem::path::preferred_separator) {
      normalizedAbsolutePrefix.push_back(
          std::filesystem::path::preferred_separator);
    }
  }
  for (std::filesystem::recursive_directory_iterator it(directory, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec))
      continue;
    const auto physical = it->path().lexically_normal().string();
    const auto relative =
        it->path().lexically_relative(directory).generic_string();
    const auto &candidate = absolutePrefix ? physical : relative;
    const auto &filter = absolutePrefix ? normalizedAbsolutePrefix : prefix;
    if (HasAsciiCaseInsensitivePrefix(candidate, filter))
      result.push_back(physical);
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::string SchoolDaysAssets::localPathAsset(const std::string &id) const {
  return Kotonoha::IsSchoolDaysGpkUri(id) ? std::string() : id;
}

std::unique_ptr<Kotonoha::AssetStream>
SchoolDaysAssets::openStream(const std::string &id) const {
  std::string logical;
  if (!Kotonoha::SchoolDaysLogicalFromGpkUri(id, &logical))
    return {};
  // A GPK URI denotes the immutable retail archive entry. Overlay-aware
  // logical resolution happens before a URI is produced.
  return Kotonoha::OpenMountedSchoolDaysAssetStream(logical, true);
}

std::unique_ptr<Kotonoha::AssetStream>
SchoolDaysAssets::openOriginalStream(const std::string &id) const {
  const auto original = resolveOriginal(id);
  std::string logical;
  if (!Kotonoha::SchoolDaysLogicalFromGpkUri(original, &logical))
    return {};
  return Kotonoha::OpenMountedSchoolDaysAssetStream(logical, true);
}

bool SchoolDaysAssets::checkDirectory(const std::filesystem::path &root,
                                      std::string &info) {
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec)) {
    info = "School Days directory not found: " + root.string();
    return false;
  }
  if (!ScriptRoot(root).empty()) {
    info = "Extracted School Days Script/English directory found";
    return true;
  }
  for (const auto &candidate : {root, root / "Packs"}) {
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
      info = "School Days HQ retail 29-pack set found (validated on launch)";
      return true;
    }
  }
  info = "Select the game directory with Script/English or the complete retail "
         "Packs directory";
  return false;
}

std::filesystem::path SchoolDaysAssets::defaultDirectory() {
  std::string info;
  for (const auto &candidate :
       Kotonoha::applicationDirectories("school-days").gameAssets)
    if (checkDirectory(candidate, info))
      return candidate;
  return Kotonoha::gameAssetDirectory("school-days");
}

std::filesystem::path SchoolDaysAssets::findDefaultRoute() {
  const auto route = Kotonoha::FileResolver::findGameAsset(
      "school-days", "school-days-hq.ktnroute");
  return route ? *route : std::filesystem::path();
}

std::filesystem::path SchoolDaysAssets::defaultRoute() {
  const auto route = findDefaultRoute();
  if (!route.empty())
    return route;
  throw std::runtime_error("school-days-hq.ktnroute not found; supply --ktrf");
}

} // namespace Kotonoha::Providers
