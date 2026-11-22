# Diretrizes para Agentes no Kotonoha

[English (US)](AGENTS.md) | **Português (Brasil)**

Este documento estabelece orientações normativas para agentes de IA, assistentes de codificação automatizados e contribuidores que trabalham no repositório Kotonoha. Todos os agentes atuando nesta base de código devem seguir estas instruções, regras arquiteturais, padrões de estilo de código e fluxos de validação.

---

## 1. Visão Geral do Projeto e Arquitetura

O Kotonoha é um **motor de visual novel modular em C++17** construído sobre SDL3, Dear ImGui, FFmpeg e libass, com suporte a runtimes adicionais (como CPython para Ren'Py/DDLC). Suporta Desktop (Linux, Windows, macOS), Dispositivos Móveis (iOS, Android) e Web (WebAssembly via Emscripten).

### Estrutura do Repositório

| Diretório / Arquivo | Descrição |
|---|---|
| `lib/` | Biblioteca central do motor, interfaces compartilhadas, cabeçalhos públicos (`lib/include/Kotonoha/`) e testes unitários. |
| `modules/vms/` | Máquinas virtuais de script (Amagami, Snow Radish, Summer Radish, Ren'Py/DDLC, RealLive, Pure Mail). Execução pura de script, independente de renderização. |
| `modules/providers/` | Decodificadores de arquivos e formatos de pacotes que implementam `Kotonoha::AssetProvider` (CMP, ADV, GPK, XP3, PAK, RealLive, RPA, Amagami, Pmail, Zero). |
| `games/` | 16 aplicações de jogos (15 ports e Example) (`example`, `amagami`, `zero-no-tsukaima`, `snow-radish-vacation`, `summer-radish-vacation`, `doki-doki-literature-club`, `school-days`, `reallive`, `kirikiri`, `luca-system`, `pmail`, `katawa-shoujo`, `meo`, `n2system`, `ponscripter`, `artemis`). |
| `buildsystems/` | Orquestração de compilação por plataforma: `cmake/` (módulos CMake), `android/` (projeto Gradle), `xcode/` (`Kotonoha.xcodeproj` nativo). |
| `assets/` | Recursos compartilhados do motor (fontes, ícones, shaders) e avisos legais de terceiros. |
| `docs/` | Documentação bilíngue completa (`*.md` em en-US, `*.pt-BR.md` em pt-BR). |
| `tools/` | Verificação de build, validação de metadados, empacotamento de assets e utilitários web. |
| `launcher/` | Launcher unificado para execução dos runtimes em um único processo. |
| `vcpkg.json` / `vcpkg-overlays/` | Manifesto de dependências e overlays de ports customizados do vcpkg. |

### Invariantes Arquiteturais

1. **Ciclo de Vida de Aplicação Compartilhado com SDL3**:
   - Todo port de jogo deve usar `Kotonoha/core/SDLCallbackApp.hpp` e declarar sua exportação usando `KOTONOHA_SDL_CALLBACK_APP(MyPort)`.
   - Os ports definem métodos estáticos `init`, `event`, `iterate` e `quit`.
   - **Nunca** escreva uma função `int main()` padrão ou uma função `SDL_AppInit` separada.
2. **Gerenciamento de Recursos com RAII do SDL**:
   - Assuma a posse de todos os recursos SDL usando os aliases RAII definidos em `Kotonoha/core/SDL.hpp` (`WindowPtr`, `RendererPtr`, `TexturePtr`, `SurfacePtr`, `IOStreamPtr`, `AudioStreamPtr`, `FontPtr`, `SDLMemoryPtr<T>`, `SDLArrayPtr<T>`, `AudioDeviceHandle`, `GamepadPtr`, `JoystickPtr`).
   - Use `.get()` para passar ponteiros brutos emprestados para funções da SDL.
   - Use `std::move` para transferir propriedade.
   - **Nunca** chame `SDL_Destroy*` ou `SDL_free` manualmente em objetos gerenciados por handles RAII.
3. **Separação Clara de Responsabilidades**:
   - Módulos de VM (`modules/vms/`) executam apenas bytecode/scripts; apresentação e renderização permanecem em `games/`.
   - Provedores de assets (`modules/providers/`) descompactam e analisam contêineres de arquivo; devem estar em conformidade com `Kotonoha::AssetProvider`.
   - Evite dependências circulares entre VMs, provedores e camadas de apresentação.
4. **Sistema de Arquivos Virtual (Filesystem V2)**:
   - O motor utiliza um sistema de arquivos virtual somente-leitura (`FileHandle`, `IMount`, `SDLFileSource`, `DirectoryMount`, `LayeredMount`, `ISO9660Mount`, `Filesystem::mount`).
   - Saves, preferências e diretórios Documents permanecem estritamente fora deste VFS somente-leitura.
   - Nunca use `std::ifstream` para assets virtuais; utilize `SDLFileSource` e `FileHandle`.

---

## 2. Políticas Críticas e Restrições

### Dados de Jogos e Limite Legal
- **Nunca comite, prepare para commit ou redistribua dados comerciais de jogos.** Todos os assets proprietários devem permanecer estritamente em diretórios locais e não rastreados (`games/<slug>/game-assets/`).
- Apenas `games/example` inclui assets de exemplo de distribuição livre.
- No empacotamento (`cpack`), assets comerciais são excluídos por padrão (`-DKOTONOHA_CPACK_INCLUDE_GAME_ASSETS=OFF`).

### Registros Históricos e Forenses
- Arquivos chamados `PORT_STATUS.md` e notas de investigação com datas em `games/*/docs/` são **registros históricos imutáveis**.
- **Não apague, sobrescreva ou reescreva silenciosamente** esses arquivos ao atualizar recursos do motor ou visões gerais.

### Política de Documentação Bilíngue
- Toda documentação ativa deve ser mantida em pares:
  - Inglês (US): `docs/FOO.md`
  - Português Brasileiro: `docs/FOO.pt-BR.md`
- Símbolos de código, argumentos de linha de comando, caminhos de arquivo, opções e identificadores devem permanecer **idênticos** entre as traduções.
- Ao modificar um guia existente ou adicionar um novo, atualize ambas as versões simultaneamente.

### Política de Armazenamento e Diretórios (`FileResolver` e `StoragePaths`)
- Os jogos **nunca** devem construir caminhos de arquivo específicos de plataforma diretamente.
- Use `FileResolver` e `Kotonoha/Filesystem/StoragePaths.hpp`.
- Os locais de armazenamento utilizam como chave o ID da Aplicação (`<app-id>`):
  - **macOS**: `~/Library/Application Support/Kotonoha/<app-id>`
  - **Windows**: `%APPDATA%/Kotonoha/<app-id>`
  - **Linux**: `$XDG_DATA_HOME/Kotonoha/<app-id>`
  - **iOS**: `Documents/Kotonoha/<app-id>`
  - **Android**: `Android/data/<app-id>/files/Kotonoha/<app-id>`
  - **Web**: ponto de montagem `/save` sincronizado com IndexedDB (`IDBFS`).
- Overlays: `--asset-overlay <path>` intercepta recursos por ID lógico antes dos assets base do jogo.

---

## 3. Padrões de Código e Convenções

- **Padrão da Linguagem**: C++17 (`set(CMAKE_CXX_STANDARD 17)`).
- **Formatação (.clang-format)**:
  - Estilo: LLVM
  - Indentação: 2 espaços
  - Limite de Colunas: 80 caracteres
  - Ordenação de Includes: Case-sensitive
- **EditorConfig (.editorconfig)**:
  - Quebra de linha: LF
  - Codificação: UTF-8
  - Espaços em branco no final: Removidos
  - C++ e arquivos em geral: 2 espaços
  - Arquivos CMake (`CMakeLists.txt`, `*.cmake`): 4 espaços
- **Apple e Swift**:
  - Swift 5+ / idiomas modernos do SwiftUI (async/await, sem Combine).
  - Indentação de 4 espaços, PascalCase para tipos, camelCase para propriedades/métodos.
  - Projeto nativo do Xcode localizado em `buildsystems/xcode/Kotonoha.xcodeproj`.
- **Uso de APIs Públicas**:
  - Acesse recursos sempre através da interface pública de `AssetProvider` (`read()`, `loadSurface()`, `texture()`, `loadAudio()`, `loadTextUtf8()`) para permitir que overlays e cache funcionem corretamente.

---

## 4. Sistemas de Build e Compilação

### Desktop (Linux, macOS, Windows) via CMake

```sh
# Configurar com o port de Exemplo
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON

# Compilar o alvo
cmake --build build --target Kotonoha

# Compilar ports específicos (ex: RealLive, DDLC, Amagami)
cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON
cmake --build build --target RealLive

# Compilar todos os ports de desktop
cmake -S . -B build -DKOTONOHA_BUILD_ALL=ON
```

### Gerenciamento de Dependências (vcpkg)
- Builds locais usam submódulos Git por padrão.
- Builds de CI e gerenciados por vcpkg utilizam:
  ```sh
  cmake -S . -B build \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=<triplet> \
    '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg' \
    -DKOTONOHA_SDL_PROVIDER=VCPKG
  ```

### Plataformas Apple (macOS e iOS)
- **Projeto Xcode Nativo**: `buildsystems/xcode/Kotonoha.xcodeproj` (contém 22 esquemas compartilhados: 11 para macOS e 11 para iOS).
- **Script de Preparação**:
  ```sh
  python3 tools/PrepareApple.py
  ```
- **Compilação com xcodebuild**:
  ```sh
  xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
    -scheme 'KotonohaExample (macOS)' -configuration Debug build

  xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
    -scheme 'KotonohaExample (iOS)' -configuration Debug \
    -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' build
  ```
- *Aviso*: Nunca execute scripts obsoletos como `UpdateGameTargets.rb` ou faça referência a `Kotonoha.xcworkspace`.

### Android via Gradle
- Localizado em `buildsystems/android/`.
- Requer JDK 17, Android SDK 36, NDK 28.2.13676358, CMake 3.22.1.
- Binários ELF usam alinhamento de página de 16 KiB (`-Wl,-z,max-page-size=16384`).
- Comandos:
  ```sh
  cd buildsystems/android
  ./gradlew :app:assembleExampleDebug
  ./gradlew :app:assembleRealLiveDebug -Pabis=arm64-v8a
  ```

### WebAssembly (Emscripten)
- Toolchain: `emcmake cmake` com `vcpkg-overlays/ports` e `wasm32-emscripten`.
- Asyncify: `-sASYNCIFY=1` com otimização `-O2` e `-fexceptions`.
- Servidor de Assets: O servidor HTTP **deve** suportar requisições com intervalo de bytes (`206 Partial Content`).
- Servidor de Desenvolvimento:
  ```sh
  python3 tools/serve_web.py --root build-web/outputs --port 3000
  ```

---

## 5. Roteiro de Verificação e Testes

Antes de concluir qualquer tarefa de agente ou comitar alterações, execute os passos de verificação aplicáveis:

### 1. Validação de Metadados de Build (Crucial)
Execute imediatamente após modificar `CMakeLists.txt`, `KotonohaGames.cmake`, `KotonohaOptions.cmake`, esquemas do Xcode, `Config/*.xcconfig` ou `project.pbxproj`:
```sh
python3 tools/ValidateBuildMetadata.py
```
*Saída esperada*: `[Kotonoha] OK: 15 CMake games; 32 existing Xcode schemes; Xcode pending: none`

### 2. Testes Nativos em C++
```sh
# Execução de testes headless
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ctest --test-dir build --output-on-failure

# Suítes de teste específicas
ctest --test-dir build -R '^Kotonoha\.FilesystemV2$' --output-on-failure
ctest --test-dir build -R 'Kotonoha\.(BuildVersion|Launcher)' --output-on-failure
```

### 3. Testes Web e Servidor com Suporte a Range
```sh
node tools/check_web_shell.js
node --check tools/web_selftest.js
python3 -m unittest discover -s tools -p test_serve_web.py -v
python3 tools/check_wasm_asyncify.py <caminho/para/jogo.js>
```

### 4. Verificação de CPack e Arquivos Gerados
```sh
python3 tools/check_game_zip.py <caminho/para/pacote.zip>
python3 tools/check_sdl_cpack.py <diretorio_build>
```

---

## 6. Regras para Agentes: O Que Fazer e O Que NÃO Fazer

### O Que Fazer (DO):
- **SEMPRE** verifique se ambos `*.md` e `*.pt-BR.md` foram atualizados em conjunto ao editar a documentação.
- **SEMPRE** execute `python3 tools/ValidateBuildMetadata.py` ao modificar configurações de compilação ou arquivos do Xcode.
- **SEMPRE** siga estritamente o ciclo de vida compartilhado do SDL3 (`KOTONOHA_SDL_CALLBACK_APP`).
- **SEMPRE** gerencie ponteiros do SDL usando os tipos RAII definidos em `Kotonoha/core/SDL.hpp`.
- **SEMPRE** respeite as regras de `.clang-format` e `.editorconfig` (2 espaços para C++, 4 espaços para CMake).
- **SEMPRE** consulte assets através dos métodos públicos de `Kotonoha::AssetProvider` para preservar suporte a overlays e cache.
- **SEMPRE** use `FileResolver` e `StoragePaths` para todos os caminhos graváveis pelo usuário e recursos do motor.

### O Que NÃO Fazer (DO NOT):
- **NÃO** comite ou redistribua nenhum asset proprietário de jogos comerciais.
- **NÃO** escreva uma função `main()` customizada ou duplique `SDL_AppInit` em nenhum port de jogo.
- **NÃO** invoque `SDL_Destroy*()` ou `SDL_free()` manualmente em objetos gerenciados por RAII.
- **NÃO** apague ou sobrescreva `PORT_STATUS.md` nem documentação histórica de engenharia reversa.
- **NÃO** carregue arquivos de múltiplos gigabytes/megabytes inteiros na MEMFS do WebAssembly; utilize requisições Fetch com HTTP Range.
- **NÃO** procure ou tente executar ferramentas inexistentes como `UpdateGameTargets.rb` ou `Kotonoha.xcworkspace`.
- **NÃO** use caminhos absolutos do sistema operacional gravados diretamente no código para saves ou configurações.
