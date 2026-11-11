#include <Kotonoha/core/Vm.hpp>
#include <memory>
#include <type_traits>

namespace {
class ScriptVm : public Kotonoha::Vm {
public:
  std::size_t executed = 0;
protected:
  void dispatch(std::size_t budget) override { executed += budget; }
};
class GameVm : public ScriptVm {
protected:
  void dispatch(std::size_t budget) override {
    ScriptVm::dispatch(budget);
    ++executed; // A game extends rather than replaces the engine dispatcher.
  }
};
}
int main() {
  static_assert(std::is_abstract_v<Kotonoha::Vm>);
  static_assert(std::has_virtual_destructor_v<Kotonoha::Vm>);
  auto game = std::make_unique<GameVm>();
  Kotonoha::Vm &vm = *game;
  vm.execute(0);
  if (game->executed != 0) return 1;
  vm.execute(7);
  if (game->executed != 8) return 2;
  vm.execute();
  return game->executed == 10009 ? 0 : 3;
}
