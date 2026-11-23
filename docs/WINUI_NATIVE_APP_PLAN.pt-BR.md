# Plano de implementação — launcher nativo WinUI 3 para Visual Studio

**Português (Brasil)** | [English (US)](WINUI_NATIVE_APP_PLAN.md)

Status: **em implementação** — Fase 1 (bridge) e o esqueleto da Fase 2 estão
presentes; a camada XAML WinUI ainda exige o toolchain do Windows App SDK.
Este plano transforma o estudo de
viabilidade do WinUI em etapas acionáveis. Leitura relacionada:
[alvos do Visual Studio](VISUAL_STUDIO_TARGETS.pt-BR.md),
[guia do Visual Studio](../buildsystems/visualstudio/README.pt-BR.md),
[configurações do engine](ENGINE_SETTINGS.pt-BR.md), [overlays](ASSET_OVERLAYS.pt-BR.md).

## 1. Objetivo e não-objetivos

Construir um aplicativo nativo WinUI 3 para Windows que atue como **biblioteca de
jogos e hub de inicialização** dos ports Kotonoha, com o mesmo papel do app
nativo Android e do launcher nativo macOS/iOS.

Ele deve:

- gerenciar uma **biblioteca de jogos** (múltiplas roots: pastas, ISO/BIN);
- seguir a identidade visual do Kotonoha, adaptada ao **Fluent/WinUI**;
- expor **por jogo**: overlay, tradução, arquivo de rota e legendas forçadas;
- permitir escolher **opções de inicialização**: janela/monitor, backend de
  renderização, upscaler, volume, fontes e cores;
- compilar dentro de `buildsystems/visualstudio/` para **x64 e ARM64**, junto
  dos 19 projetos existentes (mais a nova biblioteca estática `KotonohaLauncherCore`).

Não-objetivos na primeira iteração:

- hospedar SDL3 dentro de um `SwapChainPanel` (ver §3);
- alterar `KOTONOHA_SDL_CALLBACK_APP` ou o `main` de qualquer port;
- alterar o formato legado de campos de `game-params.txt`;
- redistribuir assets comerciais de jogos.

## 2. Decisão de arquitetura (ADR)

**O WinUI é um apresentador. O engine continua em processos separados.**

```
WinUI app
 ├─ KotonohaLauncherScanRootsJson(roots)      -> biblioteca de jogos
 ├─ KotonohaLauncherLoadGameParamsJson(...)   -> params por jogo
 ├─ KotonohaLauncherSaveGameParams(...)       -> params por jogo
 ├─ KotonohaLauncherSaveProfileEngineSettingsJson(...) -> settings.json
 └─ CreateProcessW("<port>.exe --no-launcher ...")
                                              -> SDL_AppInit -> Engine
```

Justificativa: espelha o Android (`LauncherActivity.java` passa `kotonoha_args`
para a activity do jogo), preserva o invariante do callback SDL e reaproveita
toda a lógica de detecção/validação da bridge C ABI. `SwapChainPanel` exigiria
`CreateSwapChainForComposition` + `ISwapChainPanelNative`, uma thread separada
com `DesktopWindowXamlSource`, e entraria em conflito com a HWND do SDL e com a
captura de input do XAML.

## 3. Pré-requisitos (usuário / CI)

- Visual Studio com **Desktop development with C++**, **Windows SDK** e o
  componente **C++/WinUI 3 (Windows App SDK)**; redistribuível do Windows App SDK
  ou deployment self-contained.
- Developer Mode + certificado de teste com `Publisher` igual ao `Identity` do
  MSIX para sideload, ou assinatura da Store para release.
- Manter as árvores vcpkg existentes para `x64-windows` e `arm64-windows`.
- `git submodule update --init --recursive` (SDL3, SDL3_ttf, imgui).

## 4. Fases

### Fase 0 — Baseline (sem código)

Registre o estado verde atual: `python3 tools/ValidateBuildMetadata.py`,
`generate_visualstudio.py --check`, build MSBuild Debug/x64 de
`KotonohaExample` e `DokiDoki`. Não avance até que tudo passe.

### Fase 1 — Função C ABI faltante (somente C++)

Objetivo: permitir que a UI escreva configurações do engine **parciais** sem
duplicar o schema de `settings.json` nem `EngineSettings::validate()`.

- Adicionar em `launcher/include/KotonohaLauncher/LauncherBridge.h`:
  `KotonohaLauncherSaveProfileEngineSettingsJson(const char* profileId, const char* partialJson)`
  retornando uma string JSON de status (`ok` / `error`).
- Implementar em `launcher/src/LauncherBridge.cpp`: resolver o diretório do
  perfil via `Kotonoha::documentsDirectory()`, chamar
  `EngineSettings::load(path, defaults)`, mesclar apenas os campos enviados,
  executar `validate()` e gravar com `Kotonoha::writeTextFileAtomically`.
  Reutilizar o comportamento de `resolvedOverlayPath()`.
- Adicionar teste nativo cobrindo: mesclagem parcial, rejeição de faixa
  inválida, resolução de overlay nomeado e isolamento de perfil
  (`SplitSavesByGame`).
- Ligar a exportação ao target de teste do launcher (ou criar um executável de
  teste dedicado se o harness não for compartilhado).

Critério: a função é chamável em C, faz round-trip de JSON parcial e valores
inválidos são rejeitados pelo validador existente (não pela UI).

### Fase 2 — Esqueleto do projeto WinUI na solução VS

- Adicionar `buildsystems/visualstudio/WinUI/` com um `.vcxproj` **artesanal** e
  arquivos de empacotamento MSIX (`Package.appxmanifest`, modelo de certificado).
  Sobrescrever `SubSystem` para `Windows`; C++/WinRT é compatível com C++17,
  igual a `<LanguageStandard>stdcpp17</LanguageStandard>`.
- Registrar em `buildsystems/visualstudio/ProjectManifest.json` em
  `native_projects` (NÃO em `targets`) com `platforms: ["x64", "ARM64"]`. O
  gerador já mapeia configurações Win32 não suportadas para `ActiveCfg` sem
  `Build.0` e nunca reescreve projetos importados.
- Manter `generate_visualstudio.py --check` e `ValidateBuildMetadata.py` verdes;
  adicionar asserções para o novo projeto importado e as duas plataformas.
- Verificar bootstrap unpackaged (`MddBootstrapInitialize`) ou deployment
  packaged; declarar `rescap:runFullTrust` para leitura de pastas/ISOs arbitrários.

Critério: `Kotonoha.sln` lista o projeto WinUI; x64 e ARM64 compilam; Win32 é
ignorado, não falha.

### Fase 3 — Modelo de estado e persistência

Criar view models C++/WinRT espelhando os modelos já validados no macOS
(`MacLauncherStore`, `MacGameItem`, `MacGameParams`):

```
LauncherState
 ├─ LibraryRoots, Games, IsScanning, StatusMessage
 ├─ GlobalLaunchOptions (padrões de renderer/upscaler/volume/fontes/cores)
 └─ ThemeState (AppTheme, AccentColor, FontFamily, UiScale)
GameItem
 ├─ campos de KotonohaLauncherScanRootsJson
 └─ Params: overlay, translation, subtitleStyle, ktrf/scripts, startScenario
ProfileSettingsState
 └─ campos de EngineSettings/settings.json (janela, display, áudio, texto)
```

Persistir em `%APPDATA%\Kotonoha\me.hirameki.kotonoha\`:

| Arquivo | Responsável | Conteúdo |
|---|---|---|
| `library-roots.json` | WinUI | múltiplas roots da biblioteca |
| `launcher-options.json` | WinUI | opções globais + tema |
| `game-params.txt` | **somente bridge** | `GameLaunchParameters` |
| `settings.json` | **somente bridge** | `EngineSettings` do hub e por perfil |

Usar `FolderPicker` + `Windows.Storage.AccessCache` para acesso durável a
pastas. Nunca escrever `game-params.txt` ou `settings.json` diretamente em C#.

Critério: roots, params por jogo e opções globais sobrevivem ao restart; as
configurações escritas pela UI são aceitas sem edição manual.

### Fase 4 — Implementação da UI (Fluent adaptada ao Kotonoha)

- **Página Biblioteca**: `NavigationView`, adicionar/remover múltiplas roots,
  progresso de scan (`Task.Run` + `DispatcherQueue.TryEnqueue`), grade de jogos
  com título/badge do engine de `DiscoveredGameEntry`.
- **Página Detalhes do jogo**: overlay, tradução, `--subtitle-style` e campos
  Ktrf/rota. Mostrar validação de rota apenas quando `IsSchoolDays`, usando
  `KotonohaLauncherValidateRoute` e `KotonohaLauncherDefaultSchoolDaysRoute`.
  O toggle de legendas forçadas escreve `EngineSettings::enableForcedSubtitles`,
  não `GameLaunchParameters`.
- **Opções de inicialização**: renderer (`direct3d11`, `direct3d12`, `opengl`,
  `software`), upscaler/preset/factor a partir de
  `KotonohaLauncherUpscalePresetsJson` (sem lista hardcoded), volume, fontes,
  cores de texto, janela/monitor.
- **Tema**: `ThemeDictionaries` para `Default`/`Light`/`HighContrast` com a
  paleta canônica (`#0D142E`, `#182449`, `#2C3E9A`, `#6A5BE7`, `#C86BD9`,
  `#00D6FF`, `#FF8FBD`, `#B9B5D4`, `#39315E`), Mica/Acrylic, cantos
  arredondados, `Segoe UI Variable`. Não copiar o `glassEffect` da Apple.

Critério: fluxo completo acessível por teclado e toque; HighContrast legível;
strings localizadas ou ao menos passadas pelo caminho de tradução existente.

### Fase 5 — Integração de inicialização

- `CreateProcessW` com argumentos Unicode: `--no-launcher`, `--library`,
  `--config <perfil>\settings.json`, mais flags de overlay/tradução/ktrf/scripts/
  renderer/upscaler conforme necessário.
- Definir `KOTONOHA_USER_DATA_DIR` no environment do processo filho (equivalente
  ao `_wputenv_s` em `launcher/src/main.cpp`).
- Tratar `settings.json` como pertencente ao engine enquanto o jogo roda; a UI
  edita antes do lançamento.
- Manter uma variante hub in-process via IPC (named pipe) apenas se a paridade
  com o macOS for desejada depois.

Critério: iniciar um jogo detectado abre o `.exe` correto com as opções
escolhidas; saves vão para o diretório de perfil esperado; fechar o jogo deixa a
UI responsiva.

### Fase 6 — Validação e documentação

- Estender `tools/ValidateBuildMetadata.py` com as verificações do projeto
  WinUI importado; manter as saídas atuais de CMake/Xcode/VS inalteradas.
- Adicionar checklist de smoke manual em `docs/BUILD_VALIDATION.md`
  (e `.pt-BR.md`).
- Atualizar `docs/VISUAL_STUDIO_TARGETS.md` (+ `.pt-BR.md`) e o par de READMEs
  do VS com o target WinUI e seu suporte exclusivo x64/ARM64.
- Rodar `python3 tools/ValidateBuildMetadata.py` e os testes nativos nas duas
  arquiteturas antes do merge.

## 5. Arquivos a tocar (planejado)

| Caminho | Mudança |
|---|---|
| `launcher/include/KotonohaLauncher/LauncherBridge.h` | nova exportação `SaveProfileEngineSettingsJson` |
| `launcher/src/LauncherBridge.cpp` | implementação + reuso de validação |
| `buildsystems/visualstudio/WinUI/**` | novo projeto artesanal, XAML, MSIX |
| `buildsystems/visualstudio/ProjectManifest.json` | entrada em `native_projects` (x64/ARM64) |
| `buildsystems/visualstudio/Scripts/generate_visualstudio.py` | proteger projeto importado (já suporta) |
| `tools/ValidateBuildMetadata.py` | asserções do projeto WinUI |
| `docs/WINUI_NATIVE_APP_PLAN.md` / `.pt-BR.md` | este plano |
| `docs/VISUAL_STUDIO_TARGETS.md` / `.pt-BR.md` | atualização da matriz |
| `docs/BUILD_VALIDATION.md` / `.pt-BR.md` | checklist de smoke |
| par de READMEs do VS | link para o novo target |

## 6. Riscos e mitigação

| Risco | Mitigação |
|---|---|
| MSIX virtualiza `%APPDATA%` | verificar `documentsDirectory()` em modo packaged; `runFullTrust`; picker + access cache |
| Inicialização do WinAppSDK | Deployment API (packaged) ou `MddBootstrapInitialize` (unpackaged); preferir self-contained |
| x86 é segunda classe no WinAppSDK | suportar x64/ARM64 primeiro; nunca falhar toda a solução por causa de Win32 |
| HWND do SDL vs input XAML | manter engine fora do processo (ADR), não usar `SwapChainPanel` |
| Concorrência em `settings.json` | UI edita antes do launch; arquivo do engine é autoritativo durante a execução |
| Deriva de tema entre plataformas | compartilhar paleta canônica, adaptar materiais por plataforma |
| Regras de validação duplicadas | todas as escritas passam pela bridge C ABI |

## 7. Definição de pronto

- Projeto WinUI compila em x64 e ARM64; Win32 é ignorado sem falha.
- Biblioteca multi-root, overlay/tradução/rota/legendas forçadas por jogo e
  opções de inicialização são editáveis e persistidas.
- Iniciar um jogo pela UI usa o contrato bridge + CLI existente; sem mudanças em
  `KOTONOHA_SDL_CALLBACK_APP` ou nos ports.
- `ValidateBuildMetadata.py` e `generate_visualstudio.py --check` passam, e o
  checklist de smoke é executado em x64 e ARM64.
- Documentação em pares é atualizada junto.
