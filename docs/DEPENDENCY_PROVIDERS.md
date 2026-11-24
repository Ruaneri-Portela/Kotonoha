# Dependency providers (legacy guide)

**English (US)** | [Português (Brasil)](DEPENDENCY_PROVIDERS.pt-BR.md)

The full, up-to-date provider matrix, installation commands per package
manager (Homebrew, pacman/MSYS2 UCRT64, apt), presets and ABI notes live in
[Native dependency providers](DEPENDENCIES.md).

Summary of the current switch:

| Library | Override | Allowed providers |
| --- | --- | --- |
| SDL3 / SDL3_ttf | `KOTONOHA_SDL_PROVIDER` | `VCPKG`, `SUBMODULES`, `SYSTEM` |
| Dear ImGui | `KOTONOHA_IMGUI_PROVIDER` | `SUBMODULES`, `VCPKG` — never `SYSTEM` |
| FFmpeg / libass / json-c | — | `VCPKG` or `SYSTEM` |

- `KOTONOHA_DEPS_PROVIDER=AUTO|VCPKG|SYSTEM|SUBMODULES` is the master option;
  both overrides default to `AUTO` and then follow it.
- `SYSTEM` mode: SDL3/SDL3_ttf from Homebrew/apt/MSYS2 UCRT, ImGui from the
  pinned submodule. `SUBMODULES` mode: SDL3/SDL3_ttf and ImGui from pinned
  submodules. `VCPKG` mode: all three from vcpkg (`imgui-vcpkg` feature).
- `KOTONOHA_IMGUI_PROVIDER=SYSTEM` is a configure-time `FATAL_ERROR`: ImGui is
  never searched in system packages.
- `SYSTEM` is no longer blocked on MSVC, but mixing MSYS2/UCRT and MSVC ABIs
  is unsupported; see the warning documented in `DEPENDENCIES.md`.
