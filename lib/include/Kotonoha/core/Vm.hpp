#pragma once
#include <cstddef>

namespace Kotonoha {
// Common execution entry point; engines and games provide their own dispatch.
class Vm {
public:
  virtual ~Vm() = default;
  void execute(std::size_t budget = 10000) { if (budget) dispatch(budget); }

protected:
  virtual void dispatch(std::size_t budget) = 0;
};
} // namespace Kotonoha
