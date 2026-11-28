# Provedores de dependências nativas

[English (US)](DEPENDENCIES.md) | **Português (Brasil)**

O Kotonoha resolve suas bibliotecas nativas (SDL3, SDL3_ttf, FFmpeg, libass,
json-c, FreeType, HarfBuzz, fontconfig, zlib) por meio de um dos quatro
provedores principais, com substituições opcionais por biblioteca. Nem toda
biblioteca aceita todo provedor:

| Biblioteca | Opção de substituição | Provedores permitidos |
| --- | --- | --- |
| SDL3 | `KOTONOHA_SDL_PROVIDER` | `VCPKG`, `SUBMODULES`, `SYSTEM` |
| SDL3_ttf | `KOTONOHA_SDL_PROVIDER` | `VCPKG`, `SUBMODULES`, `SYSTEM` |
| Dear ImGui | `KOTONOHA_IMGUI_PROVIDER` | `SUBMODULES`, `VCPKG` — **nunca** `SYSTEM` |
| FFmpeg / libass / json-c / FreeType / HarfBuzz / fontconfig / zlib | — | `VCPKG` ou `SYSTEM` |

`KOTONOHA_DEPS_PROVIDER` é a opção **principal**. O valor padrão das duas
substituições é `AUTO`, que segue a opção principal.

| `KOTONOHA_DEPS_PROVIDER` | SDL3 / SDL3_ttf | Dear ImGui | Bibliotecas de mídia |
| --- | --- | --- | --- |
| `AUTO` (padrão) | vcpkg se o toolchain estiver ativo; senão pacotes do sistema | submódulo fixado por padrão; vcpkg quando o toolchain e a feature `imgui-vcpkg` estão ativos | vcpkg ou sistema, conforme o toolchain |
| `VCPKG` | somente vcpkg (toolchain obrigatório na **primeira** configuração) | somente vcpkg (feature `imgui-vcpkg`) | vcpkg |
| `SYSTEM` | somente pacotes do sistema (Homebrew / apt / MSYS2 UCRT) | submódulo fixado em `lib/submodules/imgui`; vcpkg somente se pedido explicitamente | sistema |
| `SUBMODULES` | submódulos fixados de SDL3/SDL3_ttf | submódulo fixado em `lib/submodules/imgui` | sistema |

`KOTONOHA_SDL_PROVIDER` (`AUTO`, `VCPKG`, `SYSTEM`, `SUBMODULES`) fixa
SDL3/SDL3_ttf quando não é `AUTO`. `KOTONOHA_IMGUI_PROVIDER` (`AUTO`,
`SUBMODULES`, `VCPKG`) fixa o Dear ImGui quando não é `AUTO`.

**SDL3/SDL3_ttf devem vir de um único provedor.** Quando `SYSTEM` ou `VCPKG`
é selecionado (explicitamente ou via `AUTO`), a configuração falha com
`FATAL_ERROR` se alguma biblioteca estiver faltando ou se apenas uma das duas
for encontrada. Os submódulos fixados do SDL são inicializados **somente**
quando `SUBMODULES` é solicitado (no Android esse modo é sempre forçado); uma
instalação do sistema nunca é misturada com um build a partir de submódulos.

**O Dear ImGui nunca é resolvido por pacotes do sistema.** Passar
`-DKOTONOHA_IMGUI_PROVIDER=SYSTEM` falha a configuração com `FATAL_ERROR`
explicando a regra: não existe fallback `find_package(imgui)` nem
`pkg-config imgui` em nenhum caminho de dependências nativas. O motor precisa
dos backends SDL3 e SDL3 renderer compilados contra uma versão fixada do
ImGui; use o submódulo ou o pacote do vcpkg (`imgui` com `sdl3-binding` e
`sdl3-renderer-binding`). Um principal `VCPKG` explícito sempre exige o
pacote ImGui do vcpkg; no modo `AUTO`, o ImGui cai para o submódulo fixado
sempre que o pacote do vcpkg não estiver instalado.

## Comandos de instalação

### macOS — Homebrew

```sh
brew install cmake ninja pkgconf \
  sdl3 sdl3_ttf ffmpeg libass json-c \
  freetype harfbuzz fontconfig libunibreak plutosvg zlib
```

Os prefixes do Homebrew (`/opt/homebrew` em Apple Silicon, `/usr/local` em
Intel) são adicionados automaticamente a `CMAKE_PREFIX_PATH` e
`PKG_CONFIG_PATH` em builds `SYSTEM`.

### Linux — apt (Debian/Ubuntu)

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libsdl3-dev libsdl3-ttf-dev \
  libass-dev libavformat-dev libavcodec-dev libavutil-dev \
  libswscale-dev libswresample-dev libjson-c-dev \
  libfreetype-dev libharfbuzz-dev libfontconfig1-dev \
  libgl-dev zlib1g-dev fonts-dejavu-core
```

### Windows — MSYS2 UCRT64 (pacman)

Abra um shell **MSYS2 UCRT64** (não MINGW64, não MSYS):

```powershell
pacman -Syu
pacman -S mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-sdl3-ttf \
  mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-libass \
  mingw-w64-ucrt-x86_64-json-c mingw-w64-ucrt-x86_64-freetype \
  mingw-w64-ucrt-x86_64-harfbuzz mingw-w64-ucrt-x86_64-fontconfig \
  mingw-w64-ucrt-x86_64-libunibreak mingw-w64-ucrt-x86_64-plutosvg \
  mingw-w64-ucrt-x86_64-zlib
```

`CMAKE_PREFIX_PATH` e `PKG_CONFIG_PATH` capturam `$MSYSTEM_PREFIX`
(p.ex. `C:/msys64/ucrt64`) automaticamente. Sempre use um diretório de build
novo ao trocar de provedor. Builds MinGW cross a partir de macOS/Linux são
diferentes: **vcpkg é obrigatório**, pois apt/Homebrew não fornecem bibliotecas
para o alvo Windows.

## Exemplos de configuração

```sh
# SDL3/SDL3_ttf do sistema (apt/Homebrew/MSYS2 UCRT) + ImGui do submódulo fixado
cmake -S . -B build-system -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DKOTONOHA_DEPS_PROVIDER=SYSTEM \
  -DKOTONOHA_SDL_PROVIDER=SYSTEM \
  -DKOTONOHA_IMGUI_PROVIDER=SUBMODULES \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build-system --target Kotonoha

# vcpkg para tudo (SDL3, SDL3_ttf e ImGui)
cmake -S . -B build-vcpkg -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg' \
  -DKOTONOHA_DEPS_PROVIDER=VCPKG \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_IMGUI_PROVIDER=VCPKG

# Submódulos fixados para SDL3/SDL3_ttf e ImGui, mídia do sistema
git submodule update --init --recursive
cmake -S . -B build-submodules -G Ninja \
  -DKOTONOHA_DEPS_PROVIDER=SUBMODULES \
  -DKOTONOHA_SDL_PROVIDER=SUBMODULES \
  -DKOTONOHA_IMGUI_PROVIDER=SUBMODULES \
  -DKOTONOHA_BUILD_EXAMPLE=ON

# Rejeitado explicitamente: pacotes de sistema nunca são usados para ImGui.
cmake -S . -B build-bad -G Ninja -DKOTONOHA_IMGUI_PROVIDER=SYSTEM
# CMake Error: Dear ImGui cannot be resolved from SYSTEM packages.
```

Todos os builds com vcpkg compartilham o **cache binário padrão do vcpkg**
(`%LOCALAPPDATA%\vcpkg\archives` no Windows, `~/.cache/vcpkg/archives` no
Linux/macOS). Nenhum script do Kotonoha sobrescreve `VCPKG_DEFAULT_BINARY_CACHE`,
portanto um pacote compilado uma vez pela solução Visual Studio é reutilizado
pelos fluxos CMake/MSVC, MinGW e Xcode/CMake; apenas o diretório de instalação
é separado por triplet. `KOTONOHA_VS_VCPKG_ROOT` permite apontar a raiz de
instalação do Visual Studio para um local compartilhado, mantendo o cache padrão
do vcpkg.

Confira os provedores resolvidos no cache:

```sh
grep -E 'KOTONOHA_(DEPS_PROVIDER|MEDIA_PROVIDER|SDL_PROVIDER|IMGUI_PROVIDER)_RESOLVED' \
  build-system/CMakeCache.txt
```

Use um diretório de build **novo** ao trocar de provedor: entradas em cache
`*_DIR`, pkg-config e vcpkg continuariam apontando para a instalação anterior.

## Matriz provedor × plataforma

| Plataforma | `SYSTEM` | `VCPKG` | `SUBMODULES` | `AUTO` |
| --- | --- | --- | --- | --- |
| macOS | Homebrew | triplet vcpkg (`*-osx*`) | SDL do submódulo, mídia do Homebrew | vcpkg se o toolchain estiver ativo; senão Homebrew |
| Linux | apt | triplet vcpkg (`x64-linux*`) | SDL do submódulo, mídia do apt | vcpkg se o toolchain estiver ativo; senão apt |
| Windows MSYS2 UCRT64 (nativo) | pacman | triplet vcpkg (`*-mingw-dynamic`) | SDL do submódulo, mídia do pacman | vcpkg se o toolchain estiver ativo; senão pacman |
| Windows MinGW cross (host macOS/Linux) | **proibido (FATAL_ERROR)** | somente vcpkg `*-mingw-dynamic` | não usado | vcpkg |
| Windows MSVC | permitido com instalação nativa/pkg-config ABI-compatível (emite aviso) | triplet MSVC nativo (`x64-windows`, ...) | SDL do submódulo, mídia de instalação nativa | vcpkg se o toolchain estiver ativo; senão sistema |
| Android | SDL forçado para submódulo (mídia do vcpkg) | vcpkg por ABI | submódulo SDL (padrão) | mídia vcpkg + submódulo SDL |
| WebAssembly | emsdk oficial (`emsdk_env.sh`; sem pacote MSYS2) | vcpkg `wasm32-emscripten` (`sdl-vcpkg;imgui-vcpkg`) | não usado | vcpkg se o toolchain estiver ativo; senão emsdk |

Em todas as linhas, o Dear ImGui segue sua própria regra: `SUBMODULES` nos
builds nativos/`SYSTEM`; `VCPKG` em builds com provedor principal vcpkg (ou
quando `KOTONOHA_IMGUI_PROVIDER=VCPKG` é pedido explicitamente). Para builds
vcpkg, adicione a feature de manifesto `imgui-vcpkg` (inclusive em
`wasm32-emscripten`, cujo port vcpkg instala os bindings SDL3). No Android o
submódulo fixado continua sendo o padrão. `SYSTEM` nunca é um provedor válido
para ImGui.

## Notas de ABI e MSYS2

- **Nunca misture binários UCRT64, MINGW64, MSVCRT e MSVC.** Um build com
  `cl.exe` não consegue linkar bibliotecas `.dll.a`/`.a` do MSYS2, e um build
  GCC/UCRT não consegue linkar `.lib` do MSVC. Use um compilador, um runtime C
  e um conjunto de pacotes por diretório de build.
- No MSVC, `SYSTEM` é permitido, mas emite aviso: instale pacotes nativos
  (compilados para MSVC) ou prefira `VCPKG` com um triplet MSVC. O bloqueio
  rígido anterior na configuração foi removido para permitir tentativas com
  pacotes do sistema; erros de link por binários incompatíveis são de
  responsabilidade de quem configura.
- `VCPKG` busca apenas `${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}`,
  impedindo que uma instalação Homebrew/MSYS2 aleatória entre em um build vcpkg.
- Builds estáticos do SDL3_ttf via Homebrew podem precisar de
  `plutosvg`/`harfbuzz` na linkagem; instale a lista acima para evitar símbolos
  não resolvidos.

## Onde isso fica

- `CMakeLists.txt` — inicialização da opção, triplet vcpkg padrão, chamada de resolução.
- `buildsystems/cmake/dependencies/KotonohaDepsProvider.cmake` — opção principal, resolução e prefixes do sistema.
- `buildsystems/cmake/dependencies/KotonohaSDL.cmake` — descoberta de SDL3/SDL3_ttf.
- `buildsystems/cmake/dependencies/KotonohaImGui.cmake` — resolução do ImGui via submódulo/vcpkg; rejeita `SYSTEM`.
- `buildsystems/cmake/dependencies/KotonohaMedia.cmake` — descoberta de FFmpeg/libass/json-c.
- `vcpkg.json` — features de manifesto `sdl-vcpkg` e `imgui-vcpkg`.
- `buildsystems/visualstudio/Directory.Build.props` — a solução nativa linka SDL3, SDL3_ttf e Dear ImGui do vcpkg (`sdl-vcpkg` + `imgui-vcpkg`).
- `.github/workflows/linux-system-deps.yml` — cobertura de CI para apt/`SYSTEM`.
