# Dependências SDL3 e ImGui: sistema, vcpkg e submódulos

O Kotonoha permite resolver **SDL3/SDL3_ttf** e **Dear ImGui** de forma
independente, preservando os targets públicos `SDL3::SDL3`,
`SDL3_ttf::SDL3_ttf` e `KOTONOHA_IMGUI_TARGET`.

## Seleção

As opções de CMake são:

- `KOTONOHA_SDL_PROVIDER=AUTO|SYSTEM|VCPKG|SUBMODULES`
- `KOTONOHA_IMGUI_PROVIDER=AUTO|SYSTEM|VCPKG|SUBMODULES`

`AUTO` é o padrão e procura, nesta ordem: **pacotes instalados**,
**vcpkg** (quando configurado) e **submódulos** (quando clonados).

As opções legadas `KOTONOHA_SDL_FROM_VCPKG=ON` e
`KOTONOHA_IMGUI_FROM_VCPKG=ON` continuam selecionando vcpkg quando
o provider correspondente está em `AUTO`.

Se as bibliotecas foram instaladas apenas parcialmente, o CMake emite
uma mensagem orientando a instalação completa ou a seleção explícita
de outro provider.

## Debian 13 / Linux ARM64: pacotes nativos

```bash
sudo apt update
sudo apt install libsdl3-dev libsdl3-ttf-dev libimgui-dev
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DKOTONOHA_BUILD_ALL=ON \
  -DKOTONOHA_SDL_PROVIDER=SYSTEM -DKOTONOHA_IMGUI_PROVIDER=SYSTEM \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel 4
```

Também são necessárias as dependências comuns do engine (FFmpeg, libass,
json-c, OpenGL etc.). O CMake continua verificando-as pelo pkg-config.

O pacote Debian `libimgui-dev` contém o núcleo ImGui como biblioteca
instalada, enquanto `imgui_impl_sdl3.cpp` e
`imgui_impl_sdlrenderer3.cpp` ficam em
`/usr/share/doc/libimgui-dev/examples/backends`. O Kotonoha compila
somente esses dois backends e vincula o núcleo instalado. Quando a distro
utiliza outro caminho, informe
`-DKOTONOHA_IMGUI_SYSTEM_BACKENDS_DIR=/caminho/dos/backends`.

O pacote SDL3 deve ter a versão das APIs usadas pelo projeto; se sua
distribuição for mais antiga, use vcpkg ou submódulos.

## vcpkg

Configure com o toolchain do vcpkg e com os recursos do manifesto:

```bash
cmake -S . -B build-vcpkg -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  "-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg" \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_IMGUI_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_ALL=ON
cmake --build build-vcpkg --parallel 4
```

## Submódulos

```bash
git submodule update --init --recursive
cmake -S . -B build-submodules -G Ninja \
  -DKOTONOHA_SDL_PROVIDER=SUBMODULES \
  -DKOTONOHA_IMGUI_PROVIDER=SUBMODULES \
  -DKOTONOHA_BUILD_ALL=ON
cmake --build build-submodules --parallel 4
```

Android continua usando submódulos, pois a integração SDL/Java precisa
dos fontes SDL incluídos no projeto.

Evite reutilizar o mesmo diretório de build com toolchains diferentes.
Selecionar outro provider no mesmo build nativo é possível, mas a
reconfiguração de dependências pode exigir um novo diretório de build.

Na instalação local em Linux, bibliotecas externas do sistema permanecem
em seus caminhos da distribuição; bibliotecas geradas pelo projeto e
dependências distribuídas por vcpkg continuam sujeitas às regras de
empacotamento já existentes.
