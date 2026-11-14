# Script virtual machines

**English (US)** | [Português (Brasil)](README.pt-BR.md)

The modules in this directory execute scripts independently of game presentation and archive decoding.

| Directory | Runtime | CMake alias |
| --- | --- | --- |
| `amagami` | `Amagami::Vm` (SCF) | `Kotonoha::Vmamagami` |
| `snow-radish` | `SnowRadish::Vm` (ADV3) | `Kotonoha::Vmsnow-radish` |
| `summer-radish` | `SummerRadish::Machine` | `Kotonoha::Vmsummer-radish` |
| `renpy` | `Ddlc::Vm` | `Kotonoha::Vmrenpy` |
| `reallive` | `RealLive::Session` | `Kotonoha::Vmreallive` |
| `pmail` | `Pmail::Vm` | `Kotonoha::Vmpmail` |

Each VM is a static CMake library linked by its game target. The common base is `Kotonoha::Vm` (`lib/include/Kotonoha/core/Vm.hpp`); `execute(budget)` delegates to `dispatch(budget)`. Execution budgets are advisory for runtimes that yield at interaction boundaries.

Keep script execution in VM modules and presentation, parsers, and game-specific platform logic in the corresponding port. Avoid cyclic dependencies between format readers and script interpreters. See [asset providers](../providers/README.md) and [ports](../../games/README.md).
