#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Kotonoha::PmailPort {

// Resource/VM evidence only. Dialogue text is deliberately excluded so
// reports can be shared without copying the commercial script.
struct TransitionEvidence {
  uint64_t sequence = 0;
  uint64_t elapsedMs = 0;
  std::string script;
  uint32_t pc = 0;
  std::string kind;
  std::string resource;
  int frame = -1;
  int x = 0;
  int y = 0;
};

class TransitionTrace {
public:
  explicit TransitionTrace(size_t samples = 160)
      : capacity_(std::max<size_t>(samples, 1)) {}

  void record(TransitionEvidence event) {
    event.sequence = ++nextSequence_;
    if (first_.size() < capacity_)
      first_.push_back(event);
    if (recent_.size() >= capacity_)
      recent_.pop_front();
    recent_.push_back(event);
    if (!markerFound_ && namesOverflow(event.resource)) {
      markerFound_ = true;
      markerResource_ = event.resource;
      beforeMarker_.assign(recent_.begin(), recent_.end());
    }
  }

  bool markerFound() const { return markerFound_; }
  const std::string &markerResource() const { return markerResource_; }
  const std::vector<TransitionEvidence> &beforeMarker() const {
    return beforeMarker_;
  }
  const std::deque<TransitionEvidence> &recent() const { return recent_; }

  static bool namesOverflow(std::string_view resource) {
    constexpr std::string_view needle = "overflow";
    return std::search(
               resource.begin(), resource.end(), needle.begin(), needle.end(),
               [](char a, char b) {
                 return std::tolower(static_cast<unsigned char>(a)) ==
                        std::tolower(static_cast<unsigned char>(b));
               }) != resource.end();
  }

  // Deterministic TSV for comparing native/ported resource order. This does
  // not claim frame-exact timing: elapsedMs is measured by the test runner.
  void write(std::ostream &out) const {
    out << "PMAIL_TRACE_HEADER\tseq\tms\tscript\tpc\tkind\tresource"
           "\tframe\tx\ty\n";
    std::set<uint64_t> written;
    const auto emit = [&](const TransitionEvidence &item) {
      if (!written.insert(item.sequence).second)
        return;
      out << "PMAIL_TRACE\t" << item.sequence << '\t' << item.elapsedMs
          << '\t' << sanitize(item.script) << '\t' << item.pc << '\t'
          << sanitize(item.kind) << '\t' << sanitize(item.resource)
          << '\t' << item.frame << '\t' << item.x << '\t' << item.y << '\n';
    };
    for (const auto &item : first_)
      emit(item);
    for (const auto &item : beforeMarker_)
      emit(item);
    for (const auto &item : recent_)
      emit(item);
    out << "PMAIL_OVERFLOW_MARKER\t"
        << (markerFound_ ? sanitize(markerResource_) : "not-found") << '\n';
    if (nextSequence_ > written.size())
      out << "PMAIL_TRACE_OMITTED\t" << (nextSequence_ - written.size())
          << "\n";
  }

private:
  static std::string sanitize(std::string_view text) {
    std::string result;
    result.reserve(std::min<size_t>(text.size(), 256));
    for (char c : text) {
      if (result.size() >= 256)
        break;
      result.push_back(c == '\t' || c == '\r' || c == '\n' ? '_' : c);
    }
    return result;
  }

  size_t capacity_;
  uint64_t nextSequence_ = 0;
  bool markerFound_ = false;
  std::string markerResource_;
  std::vector<TransitionEvidence> first_;
  std::deque<TransitionEvidence> recent_;
  std::vector<TransitionEvidence> beforeMarker_;
};

} // namespace Kotonoha::PmailPort
