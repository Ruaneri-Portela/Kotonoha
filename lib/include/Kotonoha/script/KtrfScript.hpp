#pragma once

#include <Kotonoha/script/VNScript.hpp>

namespace Kotonoha {

struct KtrfScript {
  std::vector<ScriptNode> nodes;
  ScriptMode mode = ScriptMode::Standard;
  int entryNodeId = -1;

  void validate() const;
  static KtrfScript load(const std::string &path,
                         const std::vector<ScriptNode> &nativeBindings = {});
  void dump(const std::string &path) const;
};

} // namespace Kotonoha
