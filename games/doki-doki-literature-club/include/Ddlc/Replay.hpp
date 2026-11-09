#pragma once
#include "Ddlc/Runtime.hpp"
#include <map>
#include <optional>
namespace Ddlc {
using RootFiles = std::map<std::string, std::string>;
using RootChanges = std::map<std::string, std::optional<std::string>>;
struct ReplayInput {
  double time = 0, windowTime = 0, mouseX = 0, mouseY = 0;
  bool skipping = false;
  std::map<std::string, double> positions;
  std::map<std::string, std::string> playing;
};
struct ReplayReply {
  // A value from the current interaction is rebound to its freshly replayed
  // object; Python/displayable objects are never serialized or evaluated.
  enum class Kind {
    None,
    Bool,
    Int,
    Float,
    String,
    Item,
    Argument,
    Keyword,
    Return
  };
  Kind kind = Kind::None;
  int64_t integer = 0;
  double number = 0;
  std::string text;
};
struct ReplayStep {
  ReplayInput input;
  RootChanges files;
  ReplayReply reply;
  std::string position;
  int interaction = 0;
};
struct ReplayTranslation {
  int route = 0;
  std::string original, translated;
};
struct ReplayAudio {
  std::string channel, spec, queued;
  bool loop = false, queuedLoop = false;
  double position = 0, gain = 1;
};
struct ReplayData {
  std::string fingerprint, label, persistent;
  RootFiles initialFiles;
  ReplayInput initial, current;
  std::vector<ReplayStep> steps;
  std::vector<ReplayTranslation> translations;
  std::vector<ReplayAudio> audio;
  std::string position;
  int interaction = 0;
};
std::string encodeReplay(const ReplayData &);
ReplayData decodeReplay(const std::string &);
ReplayReply encodeReply(const Value &, const Interaction &);
Value decodeReply(const ReplayReply &, const Interaction &);
std::string replayPosition(const Vm &);
} // namespace Ddlc
