#include <Amagami/VitaVmBridge.hpp>
#include <cmath>
#include <ctime>
#include <stdexcept>

namespace Amagami::Vita {
namespace {
double viewArgument(const std::vector<VmValue> &args) {
  if (args.size() != 1 || args[0].kind != VmValue::Number ||
      !std::isfinite(args[0].number))
    throw std::runtime_error("Amagami Vita: invalid screen native argument");
  return args[0].number;
}
} // namespace

std::optional<VmValue>
VitaVmBridge::primitive(const std::string &owner, const std::string &method,
                        bool classMethod, const VmValue &receiver,
                        unsigned number, const std::vector<VmValue> &args) {
  if (owner != "K2X_Script" || !classMethod)
    return std::nullopt; // Do not hide unrelated PS2 or Vita primitives.

  if (number == 5 && method == "getClock") {
    if (!args.empty())
      throw std::runtime_error("Amagami Vita: getClock arguments");
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &now) != 0)
      throw std::runtime_error("Amagami Vita: getClock failed");
#else
    if (!localtime_r(&now, &local))
      throw std::runtime_error("Amagami Vita: getClock failed");
#endif
    auto object = std::make_shared<VmObject>();
    object->type = "Array";
    for (int value : {local.tm_mon + 1, local.tm_mday,
                      local.tm_hour, local.tm_min})
      object->fields.push_back(VmValue::num(value));
    return VmValue::ref(std::move(object), VmValue::Array);
  }
  if (number == 6 && method == "setScrScl") {
    const double scale = viewArgument(args);
    if (scale <= 0 || scale > 16)
      throw std::runtime_error("Amagami Vita: invalid screen scale");
    view_.scale = scale;
    return receiver;
  }
  if (number == 7 && method == "setScrOrgY") {
    const double y = viewArgument(args);
    if (std::abs(y) > 8192)
      throw std::runtime_error("Amagami Vita: invalid screen origin");
    view_.originY = y;
    return receiver;
  }
  if (number == 8 && method == "unlockTrophy") {
    return receiver; // Platform-neutral: no trophy service is present.
  }
  return std::nullopt; // Unknown Vita primitive: surface a VM error.
}
} // namespace Amagami::Vita
