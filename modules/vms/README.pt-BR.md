# Máquinas virtuais de scripts

[English (US)](README.md) | **Português (Brasil)**

Os módulos desta pasta executam roteiros sem depender da apresentação visual do jogo ou da decodificação de pacotes.

| Pasta | Runtime | Target CMake |
| --- | --- | --- |
| `amagami` | `Amagami::Vm` (SCF) | `kotonoha_vm_amagami` |
| `snow-radish` | `SnowRadish::Vm` (ADV3) | `kotonoha_vm_snow-radish` |
| `summer-radish` | `SummerRadish::Machine` | `kotonoha_vm_summer-radish` |
| `renpy` | `Ddlc::Vm` | `kotonoha_vm_renpy` |
| `reallive` | `RealLive::Session` | `kotonoha_vm_reallive` |
| `pmail` | `Pmail::Vm` | `kotonoha_vm_pmail` |
| `n2system` | `N2System::ScriptVm` | `kotonoha_vm_n2system` |

Cada VM é uma biblioteca CMake estática ligada pelo respectivo jogo. A base comum é `Kotonoha::Vm` (`lib/include/Kotonoha/core/Vm.hpp`); `execute(budget)` delega a `dispatch(budget)`. O limite de instruções é indicativo quando o runtime pausa em interações.

Mantenha a execução do roteiro no módulo VM e a apresentação, parsers e integração específica em cada port. Evite dependências cíclicas entre leitores de formatos e intérpretes. Veja [provedores](../providers/README.pt-BR.md) e [ports](../../games/README.pt-BR.md).
