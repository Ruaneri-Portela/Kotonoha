# Virtual machines

Script execution lives here, separately from game presentation and asset
providers. Each module exports its existing namespace and headers:

| Module | Executor | CMake target |
| --- | --- | --- |
| amagami | `Amagami::Vm` (SCF) | `Kotonoha::Vmamagami` |
| snow-radish | `SnowRadish::Vm` (ADV3) | `Kotonoha::Vmsnow-radish` |
| summer-radish | `SummerRadish::Machine` | `Kotonoha::Vmsummer-radish` |
| renpy | `Ddlc::Vm` | `Kotonoha::Vmrenpy` |
| reallive | `RealLive::Session` | `Kotonoha::Vmreallive` |
| pmail | `Pmail::Vm` | `Kotonoha::Vmpmail` |

Each module is a static library built by its own `CMakeLists.txt` (e.g.
`modules/vms/amagami/CMakeLists.txt`) and exports the alias target
`Kotonoha::Vm<module>`. Game CMakeLists files `include()` the module and link
`Kotonoha::Vm<module>` into the runtime that drives the VM; tests that
instantiate a VM link it directly. The format library no longer links the VM,
which avoids static-library cycles while keeping the existing dependency graph.

The format libraries link the object modules, so existing applications and tests
keep their current dependencies. Standalone game builds use the same modules.
Parsers, renderers, sessions implementing game presentation, and native backends
remain with their games; the VM modules may consume their format declarations.

All executors derive from `Kotonoha::Vm` in
`lib/include/Kotonoha/core/Vm.hpp`. `execute(budget)` invokes the protected virtual
`dispatch(budget)`; zero is a no-op. An engine implements dispatch and a game can
override it, optionally invoking its parent's implementation. Existing `run`,
`resume`, native callbacks and host APIs remain available. KAG and Ren'Py yield
at interaction boundaries, so their budgets are advisory, not instruction caps.
RealLive command execution and KAG tag execution are also virtual extension
points for game-specific instructions.
