#pragma once

#include <Amagami/Vm.hpp>
#include <optional>
#include <string>
#include <vector>

namespace Amagami::Vita {

// This is deliberately a narrow bridge, NOT a Vita gameplay emulator.
// Unimplemented native classes/primitives still fail with the VM location.
struct ViewState {
  double scale = 1.0;
  double originY = 0.0;
};

// Handles the Vita-specific K2X_Script class primitives 5-8. The view state
// can be consumed by a future 480x272 native scene renderer (2x textures).
class VitaVmBridge {
public:
  const ViewState &view() const { return view_; }
  std::optional<VmValue> primitive(const std::string &owner,
                                   const std::string &method,
                                   bool classMethod, const VmValue &receiver,
                                   unsigned number,
                                   const std::vector<VmValue> &arguments);

private:
  ViewState view_{};
};

} // namespace Amagami::Vita
