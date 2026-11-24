# Provedores de dependências (guia legado)

[English (US)](DEPENDENCY_PROVIDERS.md) | **Português (Brasil)**

A matriz completa e atualizada de provedores, os comandos de instalação por
gerenciador (Homebrew, pacman/MSYS2 UCRT64, apt), presets e notas de ABI
estão em [Provedores de dependências nativas](DEPENDENCIES.pt-BR.md).

Resumo da opção atual:

| Biblioteca | Substituição | Provedores permitidos |
| --- | --- | --- |
| SDL3 / SDL3_ttf | `KOTONOHA_SDL_PROVIDER` | `VCPKG`, `SUBMODULES`, `SYSTEM` |
| Dear ImGui | `KOTONOHA_IMGUI_PROVIDER` | `SUBMODULES`, `VCPKG` — nunca `SYSTEM` |
| FFmpeg / libass / json-c | — | `VCPKG` ou `SYSTEM` |

- `KOTONOHA_DEPS_PROVIDER=AUTO|VCPKG|SYSTEM|SUBMODULES` é a opção principal;
  as duas substituições têm `AUTO` como padrão e então a seguem.
- Modo `SYSTEM`: SDL3/SDL3_ttf do Homebrew/apt/MSYS2 UCRT, ImGui do submódulo
  fixado. Modo `SUBMODULES`: SDL3/SDL3_ttf e ImGui dos submódulos fixados.
  Modo `VCPKG`: os três do vcpkg (feature `imgui-vcpkg`).
- `KOTONOHA_IMGUI_PROVIDER=SYSTEM` gera `FATAL_ERROR` na configuração: o ImGui
  nunca é buscado em pacotes do sistema.
- `SYSTEM` não é mais bloqueado no MSVC, mas misturar ABIs MSYS2/UCRT e MSVC
  não é suportado; veja o aviso documentado em `DEPENDENCIES.pt-BR.md`.
