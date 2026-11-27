# Compilação Visual Studio (solução nativa)

**Português (Brasil)** | [English (US)](BUILD_VISUAL_STUDIO.md)

Além do CMake, o Kotonoha oferece um caminho de compilação independente para
Windows: a solução Visual Studio versionada em
`buildsystems/visualstudio/Kotonoha.sln`. Assim como o projeto Xcode nativo, ela
referencia a árvore de fontes diretamente e não invoca CMake.

## Mapa da solução

| Configuração | Plataformas | Toolset | Dependências |
|---|---|---|---|
| Debug, Release | Win32 (x86), x64 (amd64), ARM64 | MSVC v145 / Windows SDK 10.0 | vcpkg em modo manifesto |

Os 74 projetos são divididos em 1 DLL do motor, 39 bibliotecas estáticas de
módulos (12 VMs, 14 formatos, 13 providers), 16 bibliotecas estáticas de jogos
mais 16 executáveis com apenas `main.cpp`, e 2 projetos do launcher
(`KotonohaLauncherCore` também é uma DLL). SDL3, SDL3_ttf e Dear ImGui vêm
inteiramente do vcpkg. Cada biblioteca de jogo tem seu próprio diretório
intermediário, de modo que fontes com o mesmo nome em ports diferentes não
colidem no MSBuild. Veja `buildsystems/visualstudio/README.md` para detalhes.

## Preparar e compilar

Scripts Windows são apenas PowerShell (`.ps1`); os validadores
multiplataforma continuam em Python (`.py`). Na raiz do repositório:

```powershell
$env:VCPKG_ROOT = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg'
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Generate
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Version
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Install
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Build
```

`-Mode Install` executa `vcpkg install` para `x86-windows`, `x64-windows` e
`arm64-windows` com `--x-feature=sdl-vcpkg --x-feature=imgui-vcpkg` e os ports
de overlay do ffmpeg. `-Mode Build` compila as seis combinações
Debug/Release × plataforma; use `-Architectures x64 -Configuration Release`
para iterar mais rápido. Os artefatos ficam em
`buildsystems/visualstudio/out/<Platform>/<Configuration>/`.

Abrir o `.sln` no Visual Studio também funciona: os caminhos de include e
bibliotecas do vcpkg vêm de `buildsystems/visualstudio/vcpkg/<arch>/<triplet>/`, portanto não é
necessário um `vcpkg integrate install` global.

## Verificação

```sh
python3 tools/ValidateBuildMetadata.py
python3 tools/check_visual_studio_sources.py
```

Ambos são somente leitura. O primeiro verifica a consistência dos metadados
CMake/Xcode/Visual Studio; o segundo verifica a cobertura das fontes e a matriz
de configurações da solução Visual Studio.

## Launcher e gerenciador Avalonia

`KotonohaLauncherCore.dll` exporta `KotonohaLauncherRunDll`, um ponto de entrada
RUNDLL32 equivalente a um fork no Windows. O gerenciador em
`launcher/desktop/KotonohaManager/` usa-o para iniciar cada jogo na sua própria
main thread (requisito do SDL), recebe o PID, captura stdout/stderr e mantém o
card verde até o processo encerrar. O gerenciador alterna entre português e
inglês em tempo real e copia `assets/` para o build para que o motor abra as
fontes incluídas.

Assets comerciais permanecem em diretórios locais e não versionados
`games/<slug>/game-assets/` e nunca são copiados ou empacotados pela solução.
