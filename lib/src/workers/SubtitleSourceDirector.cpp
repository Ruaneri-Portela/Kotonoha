#include <Kotonoha/workers/SubtitleSourceDirector.hpp>

#include <algorithm>

namespace Kotonoha {
namespace workers {

void SubtitleSourceDirector::setForced(std::string script) {
  forced_.script = std::move(script);
  forced_.present = true;
  forced_.clock = 0.0;
}

void SubtitleSourceDirector::clearForced() { forced_ = Slot{}; }

void SubtitleSourceDirector::setSidecar(std::string script,
                                        double videoSeconds) {
  sidecar_.script = std::move(script);
  sidecar_.present = true;
  sidecar_.clock = videoSeconds;
}

void SubtitleSourceDirector::clearSidecar() { sidecar_ = Slot{}; }

void SubtitleSourceDirector::setEmbedded(std::string script,
                                         double videoSeconds) {
  embedded_.script = std::move(script);
  embedded_.present = true;
  embedded_.clock = videoSeconds;
}

void SubtitleSourceDirector::clearEmbedded() { embedded_ = Slot{}; }

void SubtitleSourceDirector::setDefault(std::string script) {
  default_.script = std::move(script);
  default_.present = true;
  default_.clock = 0.0;
}

void SubtitleSourceDirector::clearDefault() { default_ = Slot{}; }

void SubtitleSourceDirector::setVideoClock(double seconds) {
  sidecar_.clock = seconds;
  embedded_.clock = seconds;
}

SubtitleSourceSelection SubtitleSourceDirector::select() const {
  const Slot *slots[] = {&forced_, &sidecar_, &embedded_, &default_};
  const SubtitleSourceKind kinds[] = {
      SubtitleSourceKind::Forced, SubtitleSourceKind::Sidecar,
      SubtitleSourceKind::Embedded, SubtitleSourceKind::Default};
  for (size_t i = 0; i < 4; ++i) {
    if (!slots[i]->present)
      continue;
    return {kinds[i], slots[i]->script, slots[i]->clock};
  }
  return {};
}

bool SubtitleSourceDirector::hasAny() const {
  return forced_.present || sidecar_.present || embedded_.present ||
         default_.present;
}

std::vector<std::string>
SubtitleSourceDirector::sidecarIdsForVideo(const std::string &videoAssetId) {
  const auto slash = videoAssetId.find_last_of("/\\");
  const auto dot = videoAssetId.find_last_of('.');
  std::string base = videoAssetId;
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
    base.resize(dot);
  std::vector<std::string> result{base + ".ass", base + ".ASS"};
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

const char *subtitleSourceName(SubtitleSourceKind kind) {
  switch (kind) {
  case SubtitleSourceKind::Forced:
    return "forced";
  case SubtitleSourceKind::Sidecar:
    return "sidecar";
  case SubtitleSourceKind::Embedded:
    return "embedded";
  case SubtitleSourceKind::Default:
    return "default";
  case SubtitleSourceKind::None:
    return "none";
  }
  return "unknown";
}

} // namespace workers
} // namespace Kotonoha
