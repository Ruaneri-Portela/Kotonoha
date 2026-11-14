# Máquinas virtuais de scripts

[English (US)](README.md) | **Português (Brasil)**

Os módulos desta pasta executam roteiros sem depender da apresentação visual do jogo ou da decodificação de pacotes.

| Pasta | Runtime | Alias CMake |
| --- | --- | --- |
| `amagami` | `Amagami::Vm` (SCF) | `Kotonoha::Vmamagami` |
| `snow-radish` | `SnowRadish::Vm` (ADV3) | `Kotonoha::Vmsnow-radish` |
| `summer-radish` | `SummerRadish::Machine` | `Kotonoha::Vmsummer-radish` |
| `renpy` | `Ddlc::Vm` | `Kotonoha::Vmrenpy` |
| `reallive` | `RealLive::Session` | `Kotonoha::Vmreallive` |
| `pmail` | `Pmail::Vm` | `Kotonoha::Vmpmail` |

Cada VM é uma biblioteca CMake estática ligada pelo respectivo jogo. A base comum é `Kotonoha::Vm` (`lib/include/Kotonoha/core/Vm.hpp`); `execute(budget)` delega a `dispatch(budget)`. O limite de instruções é indicativo quando o runtime pausa em interações.

Mantenha a execução do roteiro no módulo VM e a apresentação, parsers e integração específica em cada port. Evite dependências cíclicas entre leitores de formatos e intérpretes. Veja [provedores](../providers/README.pt-BR.md) e [ports](../../games/README.pt-BR.md).
