#pragma once

extern "C" {
#include <Kotonoha/parsers/Ors.h>
}

#include <unordered_set>

namespace Kotonoha {

// SOMCON was optional retail hardware.  The authenticated game-side contract
// still consumes MOVE_SOM over its authored half-open window, but creates no
// serial/USB/media/render state when that peripheral is absent.
class SchoolDaysMoveSomNoOp {
public:
  void Start(const Kotonoha_orsEvent *event) {
    if (event != nullptr && event->command == MOVE_SOM)
      active_.insert(event);
  }

  void AdvanceTo(Kotonoha_SceneTick tick) {
    for (auto it = active_.begin(); it != active_.end();) {
      if (tick >= (*it)->endTick)
        it = active_.erase(it);
      else
        ++it;
    }
  }

  bool Active(const Kotonoha_orsEvent *event) const {
    return active_.find(event) != active_.end();
  }

  void Reset() { active_.clear(); }

private:
  std::unordered_set<const Kotonoha_orsEvent *> active_;
};

} // namespace Kotonoha
