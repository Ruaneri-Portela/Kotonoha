#include <Pmail/TransitionTrace.hpp>
#include <sstream>
#include <stdexcept>
#include <string>

int main() {
  using namespace Kotonoha::PmailPort;
  TransitionTrace trace(3);
  const auto add = [&](uint64_t ms, std::string action, std::string resource,
                       int frame) {
    TransitionEvidence item;
    item.elapsedMs = ms;
    item.script = "PRO1";
    item.pc = 42;
    item.kind = std::move(action);
    item.resource = std::move(resource);
    item.frame = frame;
    item.x = 245;
    item.y = 152;
    trace.record(std::move(item));
  };
  add(0, "sys51", "osboot.anim", 0);
  add(2160, "frame", "osboot.anim", 40);
  add(4266, "frame", "osboot.anim", 79);
  if (trace.markerFound())
    throw std::runtime_error("OSBOOT was incorrectly identified as Overflow");
  add(4500, "background", "Overflow_Logo.CRGB", -1);
  add(4700, "background", "next.CRGB", -1);
  if (!trace.markerFound() || trace.markerResource() != "Overflow_Logo.CRGB" ||
      trace.beforeMarker().size() != 3 ||
      trace.beforeMarker()[0].resource != "osboot.anim")
    throw std::runtime_error("Pre-Overflow event window was not retained");
  std::ostringstream stream;
  trace.write(stream);
  const auto report = stream.str();
  if (report.find("PMAIL_TRACE\t1\t0\tPRO1\t42\tsys51\tosboot.anim") ==
          std::string::npos ||
      report.find("PMAIL_OVERFLOW_MARKER\tOverflow_Logo.CRGB") ==
          std::string::npos ||
      report.find("PMAIL_TRACE_HEADER") == std::string::npos)
    throw std::runtime_error("Transition evidence format changed");
  TransitionTrace bounded(2);
  for (int frame = 0; frame < 10; ++frame)
    bounded.record({0, uint64_t(frame), "PRO1", 1, "frame", "osboot.anim",
                    frame, 245, 152});
  if (bounded.recent().size() != 2 || bounded.recent().front().frame != 8)
    throw std::runtime_error("Trace did not enforce a bounded tail");
  return 0;
}
