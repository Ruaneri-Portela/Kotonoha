# Script virtual machines

**English (US)** | [Português (Brasil)](README.pt-BR.md)

The modules in this directory execute scripts independently of game presentation and archive decoding.

| Directory | Runtime | CMake target |
| --- | --- | --- |
| `amagami` | `Amagami::Vm` (SCF) | `kotonoha_vm_amagami` |
| `snow-radish` | `SnowRadish::Vm` (ADV3) | `kotonoha_vm_snow-radish` |
| `summer-radish` | `SummerRadish::Machine` | `kotonoha_vm_summer-radish` |
| `renpy` | `Ddlc::Vm` | `kotonoha_vm_renpy` |
| `reallive` | `RealLive::Session` | `kotonoha_vm_reallive` |
| `pmail` | `Pmail::Vm` | `kotonoha_vm_pmail` |
| `n2system` | `N2System::ScriptVm` | `kotonoha_vm_n2system` |

Each VM is a static CMake library linked by its game target. The common base is `Kotonoha::Vm` (`lib/include/Kotonoha/core/Vm.hpp`); `execute(budget)` delegates to `dispatch(budget)`. Execution budgets are advisory for runtimes that yield at interaction boundaries.

Keep script execution in VM modules and presentation, parsers, and game-specific platform logic in the corresponding port. Avoid cyclic dependencies between format readers and script interpreters. See [asset providers](../providers/README.md) and [ports](../../games/README.md).
