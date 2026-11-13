# Kotonoha — Xcode macOS + iOS

Projeto Xcode único, gerado a partir dos três `project.pbxproj` fornecidos. Contém **26 targets** (11 jogos × 2 plataformas + 2 frameworks × 2 plataformas), **22 schemes compartilhados** para executar os jogos e referências aos arquivos C++ originais.

## Organização

- **Core/KotonohaEngine**: fontes em `lib/src/`, organizadas por subsistema.
- **Core/KotonohaImGui**: fontes em `lib/submodules/imgui/`.
- **Games**: apenas as fontes em `games/<jogo>/`.
- **Modules/VM**: fontes em `modules/vms/<nome>/` (grupo de navegação nomeado VM).
- **Modules/Providers**: fontes em `modules/providers/<nome>/`.
- **Dependencies/Python**: Python XCFramework para DokiDoki.
- **Configurations**: `.xcconfig` comuns e por plataforma/target.
- **Products**: macOS e iOS separados.

As fontes de VM/Providers **continuam no target do jogo correspondente**, respeitando exatamente a lista de fontes original. Separá-las em targets de bibliotecas exigiria avaliar símbolos e dependências para evitar duplicação de implementações.

## Dependências esperadas

Conforme os arquivos originais, espera-se encontrar `SDL3`, `SDL3_ttf`, `libass`, `libharfbuzz`, `freetype`, `fontconfig`, `FFmpeg` e demais dependências no local configurado. Em `Config/Common.xcconfig`:

- macOS Apple Silicon: `buildsystems/xcode/vcpkg/macos/arm64-osx`;
- iOS simulador (Apple Silicon): `buildsystems/xcode/vcpkg/simulator/arm64-ios-simulator`;
- iOS dispositivo arm64: `buildsystems/xcode/vcpkg/ios/arm64-ios` (**novo caminho sugerido**, pode ser necessário ajustar);
- frameworks SDL: buscas nas pastas Xcode de `lib/submodules/SDL` e `SDL_ttf`.

Se suas dependências estiverem em outras pastas, edite o `.xcconfig` comum. O caminho antigo absoluto `/Users/nero/Codes/Kotonoha` foi removido das configurações geradas.

### Python para DokiDoki

O projeto original embutia `Python.framework` em um caminho com `sourceTree = <absolute>` mas valor relativo, e para iOS selecionava diretamente uma fatia de simulador. Este projeto passa a referenciar **XCFrameworks no nível superior**, deixando o Xcode selecionar a fatia por plataforma:

- `python/macOS/Python.xcframework`
- `python/iOS/Python.xcframework`

Certifique-se de que essas pastas existam no repositório e incluam suporte às arquiteturas necessárias. Se estiverem noutro lugar, ajuste as duas referências em **Dependencies/Python**.

##  Verificação

Para inspecionar a lista de schemes em um Mac:

```sh
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
```

Exemplo de build de simulador:

```sh
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaExample (iOS)' -configuration Debug \
  -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' build
```

Exemplo de build macOS:

```sh
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaExample (macOS)' -configuration Debug build
```

A validação dos caminhos de fontes e dependências requer a árvore real do repositório na posição esperada.

## Regras obrigatórias de empacotamento dos 22 aplicativos

Todos os **11 jogos x macOS/iOS** utilizam uma única configuração de bundle:

- `INFOPLIST_FILE = "$(SRCROOT)/Info.plist"` e `GENERATE_INFOPLIST_FILE = NO` nas configurações Debug e Release de **cada target de aplicativo**. O arquivo de origem é **o já existente** em `buildsystems/xcode/Info.plist` do seu repositório; ele **não foi substituído nem copiado** no ZIP. Mantenha esse arquivo ao extrair o pacote. Cada aplicativo mantém seu `PRODUCT_BUNDLE_IDENTIFIER` próprio. Caso o plist use identificadores fixos ou chaves exclusivas do macOS, corrija o **mesmo arquivo de origem** para ambas as plataformas (por exemplo, com `$(PRODUCT_BUNDLE_IDENTIFIER)` e chaves apropriadas a cada SDK).
- `assets/` na raiz do repositório entra em **Copy Bundle Resources** como `assets/` em *todos* os 22 targets.
- `games/<jogo>/game-assets/` entra em **Copy Bundle Resources** como `game-assets/` apenas no respectivo target do jogo. Não se mistura assets de jogos diferentes.
- Cada app contém `SDL3.framework` e `SDL3_ttf.framework` em sua pasta `Frameworks/`. A fase **Embed SDL3 + SDL3_ttf and Verify Assets**, executada após o Copy Bundle Resources e o Embed Frameworks dos módulos Kotonoha, copia os frameworks compilados corretos e assina os dois quando `CODE_SIGNING_ALLOWED=YES`.

Layout esperado do bundle gerado:

```text
Jogo.app/                           iOS                         macOS
  Frameworks/SDL3.framework         Frameworks/SDL3.framework     Contents/Frameworks/SDL3.framework
  Frameworks/SDL3_ttf.framework     Frameworks/SDL3_ttf.framework Contents/Frameworks/SDL3_ttf.framework
  assets/                            assets/                       Contents/Resources/assets/
  game-assets/                       game-assets/                  Contents/Resources/game-assets/
```

A árvore acima é *esquemática*: em macOS todos os recursos também ficam dentro de `Contents/Resources/` e os executáveis em `Contents/MacOS/`. Em iOS, `Frameworks/` é uma subpasta do `.app`.

### Pré-requisito: frameworks SDL verdadeiros, compilados para o destino

**Não copie os placeholders `.tbd`** encontrados em `Xcode/iOS/SDL3.framework` ou `Xcode/macOS/SDL3.framework`. Eles servem para vinculação (link), não para redistribuição. Gere `SDL3.framework` e `SDL3_ttf.framework` nos projetos SDL do próprio repositório **para cada SDK e configuração**:

```text
lib/submodules/SDL/Xcode/SDL/build/<Debug|Release>[-iphoneos|-iphonesimulator]/SDL3.framework
lib/submodules/SDL_ttf/Xcode/build/<Debug|Release>[-iphoneos|-iphonesimulator]/SDL3_ttf.framework
```

Para macOS, o sufixo de plataforma geralmente fica vazio. O script de embed falha com uma mensagem explícita se os binários compilados estiverem ausentes, impedindo criar aplicativos incompletos ou misturar dispositivo e simulador. Se já possuir frameworks em outro local, defina nos build settings `KOTONOHA_SDL3_FRAMEWORK_PATH` e `KOTONOHA_SDL3_TTF_FRAMEWORK_PATH` como caminhos **completos** aos respectivos diretórios `.framework` para o SDK selecionado.

O script compartilhado encontra-se em `Scripts/EmbedSDLFrameworks.sh`. Ele verifica se ambos `assets/` e `game-assets/` foram efetivamente copiados antes de copiar/assinar SDL. `ENABLE_USER_SCRIPT_SANDBOXING=NO` está definido nos 22 targets de aplicativos porque o script lê frameworks externos ao projeto e escreve no bundle produzido.

**Limitação de validação:** o ZIP inclui o projeto e script de empacotamento, mas não os executáveis SDL, o código-fonte, nem o arquivo de `Info.plist` mantido em sua cópia do repositório. Assim, os metadados, scripts e a sintaxe são verificáveis aqui, mas a execução de `xcodebuild`, assinatura em dispositivo e compatibilidade do conteúdo real do `Info.plist` exigem validação no Mac.

## Ícone compartilhado, execução e Python do DokiDoki (revisão)

### Ícone dos aplicativos macOS

A árvore do repositório contém `assets/icon.icns`. Os **11 aplicativos macOS** incluem a fase **Install shared macOS app icon**, que executa `Scripts/ConfigureMacOSIcon.sh` após a cópia de recursos:

1. Verifica `assets/icon.icns` e o `Info.plist` processado do `.app`.
2. Copia o ícone para `<jogo>.app/Contents/Resources/icon.icns`.
3. Define `CFBundleIconFile=icon.icns` **somente no `Info.plist` do bundle compilado**. O arquivo compartilhado `buildsystems/xcode/Info.plist` permanece inalterado e continua sendo usado por todos os targets.

O ícone também permanece disponível em `Contents/Resources/assets/icon.icns`, como parte dos assets globais. **iOS não aceita diretamente `.icns` como AppIcon**. Para ter ícone também no iPhone/iPad e em pacotes de distribuição, gere imagens PNG e um `AppIcon.appiconset` válido a partir da arte original; esta revisão não inventa PNGs, pois o arquivo de imagem real não foi fornecido no pacote de configurações.

### Desativar `-NSDocumentRevisionsDebugMode`

Os **22 schemes compartilhados** alteram `debugDocumentVersioning` de `YES` para `NO` tanto em `LaunchAction` quanto em `ProfileAction`. Nenhum contém argumento explícito `-NSDocumentRevisionsDebugMode`. Essa é a configuração Xcode que controla a passagem automática do argumento de debug de versões de documento. Os 22 schemes foram verificados por XML.

### Empacotamento especial do DokiDoki / CPython 3.15

O utilitário `tools/PrepareApple.py` prepara o runtime CPython por destino e **está incluído** neste pacote atualizado:

- **iOS dispositivo:** `build/iOS-deps/python/native/iphoneos/`
- **iOS simulador:** `build/iOS-deps/python/native/iphonesimulator/`
- **macOS:** `build/macOS-deps/Frameworks/Python.framework`

A fase **Embed CPython for DokiDoki** foi adicionada apenas aos dois targets `DokiDoki-macOS` e `DokiDoki-iOS`. Ela executa `Scripts/EmbedDokiDokiPython.sh` e funciona após o `Embed Frameworks` do Xcode (que já vincula e embute o `Python.xcframework` correspondente):

- iOS: usa `build/iOS-deps/python/native/$(PLATFORM_NAME)` para copiar `python/lib/python3.15` (incluindo os arquivos `.fwork`) para `<app>.app/python/lib/python3.15`, e os `Frameworks/*.framework` das extensões para `<app>.app/Frameworks/`; assina extensões quando aplicável.
- macOS: verifica `Python.framework` em `<app>.app/Contents/Frameworks/` e, caso esteja ausente, usa o framework preparado em `build/macOS-deps/Frameworks/Python.framework`.
- Se o material preparado ainda não existe, tenta executar `tools/PrepareApple.py`, que deve conter o código enviado e exigir um `Python.xcframework` válido. Como esse preparo pode envolver `xcrun lipo`, deve acontecer num Mac com Xcode instalado.

Os demais jogos não recebem essa fase e não copiam as bibliotecas Python.

**Observação:** o pacote continua sem incluir o repositório completo, binários Python/SDL ou recursos de jogo. A sintaxe do projeto e dos scripts foi validada no ambiente disponível, mas a compilação, a assinatura, os ícones exibidos pelo Finder e a inicialização do CPython precisam ser testados no macOS/Xcode real.

## Download automático do Python (DokiDoki, 3.15-b3)

O pacote de Xcode inclui `tools/PrepareApple.py`, para instalar
na raiz do repositório (juntamente com `buildsystems/xcode/`). O script baixa
os artefatos da release `3.15-b3` do projeto BeeWare, valida o SHA-256,
extrai o `Python.xcframework` e prepara os arquivos usados no bundle.
O download ocorre **apenas no primeiro build** ou se o XCFramework estiver
faltando; quando o cache e o material preparado existem, são reutilizados.

### Usar o instalador sem baixar manualmente

```sh
# A partir da raiz do repositório:
python3 tools/PrepareApple.py --platform ios

# Também disponível para macOS (consulta o digest do asset na API GitHub):
python3 tools/PrepareApple.py --platform macos

# Preparar ambos (padrão, sem flags):
python3 tools/PrepareApple.py
```

O arquivo iOS é obtido de:

`https://github.com/beeware/Python-Apple-support/releases/download/3.15-b3/Python-3.15-iOS-support.b3.tar.gz`

O hash iOS **fixo** para esta release é:

`429080dcfab3778b75bbc6548cbfd754dc943cfc6092a92b72022541ff3212a0`

### Compatibilidade com seu comando anterior

```sh
mkdir -p build/iOS-deps/python
curl -fL \
  -o build/iOS-deps/python/Python-iOS.tar.gz \
  https://github.com/beeware/Python-Apple-support/releases/download/3.15-b3/Python-3.15-iOS-support.b3.tar.gz

echo '429080dcfab3778b75bbc6548cbfd754dc943cfc6092a92b72022541ff3212a0  build/iOS-deps/python/Python-iOS.tar.gz' | shasum -a 256 -c -

python3 tools/PrepareApple.py --archive build/iOS-deps/python/Python-iOS.tar.gz
```

Observe o `curl -o`: no comando sem essa opção, o tarball seria enviado para
stdout em vez de ser gravado no arquivo que o `shasum` tenta verificar.
O parâmetro `--archive` continua funcionando e valida o hash **antes de extrair**.
Também existem `--macos-archive`, `--macos-sha256`, `--force-download`,
`--force-prepare` e `--platform ios|macos|all`.

### Integração com Xcode

Os schemes **DokiDoki (iOS)** e **DokiDoki (macOS)** têm uma **Build Pre-action**
que roda o script com `--platform ios` ou `--platform macos`, respectivamente,
antes do início do build. Isso é importante porque a referência
`Python.xcframework` precisa existir durante a fase de linkedição. Os dois
targets DokiDoki também possuem uma fase de preparo no início de seus build
phases para quem usa `xcodebuild -target`; essa opção depende da ordem de
planejamento do build do Xcode, por isso prefira o build por `-scheme`.

A fase **Embed CPython for DokiDoki** continua encarregada de copiar a biblioteca
padrão e as extensões do CPython para o `.app`. Outras aplicações não baixam
nem empacotam Python.

**Nota:** o download da release BeeWare pode ser grande e requer internet.
O macOS valida o SHA-256 publicado para o asset na API do GitHub; se o digest
não estiver disponível, informe `--macos-sha256` explicitamente.
Este ambiente não executa `xcodebuild`, de modo que o download e a compilação
reais devem ser testados em um Mac com Xcode.


## Preparador unificado: `tools/PrepareApple.py` (CPython + vcpkg)

O antigo `tools/PrepareApplePython.py` foi substituído por
`tools/PrepareApple.py`. O novo programa é utilizado pelas 22 configurações
compartilhadas do Xcode (Build Pre-actions):

- **Todos os jogos** verificam e, se necessário, instalam as dependências do
  `vcpkg.json` da raiz do projeto para o SDK escolhido.
- **Somente DokiDoki** também baixa/verifica CPython e prepara os frameworks
  Python para seu bundle.
- As dependências existentes são reutilizadas, e novas instalações usam
  `vcpkg install` em modo manifesto com `--x-install-root`.
- Quando o executável `vcpkg` não existir, o script pode clonar/compilar o
  vcpkg oficial dentro de `build/tools/vcpkg/`, sem sobrescrever os
  diretórios de instalações da árvore Xcode. Esse bootstrap requer macOS,
  Xcode Command Line Tools e internet.

### Execução a partir da raiz do Kotonoha

```sh
# Prepara CPython + vcpkg para ambos os SDKs iOS (dispositivo e simulador)
python3 tools/PrepareApple.py --platform ios

# Prepara SOMENTE Python, sem instalar vcpkg (útil para corrigir a extração)
python3 tools/PrepareApple.py --platform ios --only-python

# Instala/verifica vcpkg sem baixar Python
python3 tools/PrepareApple.py --platform ios --only-vcpkg

# Prepara só bibliotecas do simulador arm64
python3 tools/PrepareApple.py --platform ios --sdk iphonesimulator --only-vcpkg

# Prepara as bibliotecas macOS arm64
python3 tools/PrepareApple.py --platform macos --only-vcpkg

# Apenas verifica as bibliotecas sem baixar/compilar nada
python3 tools/PrepareApple.py --platform macos --only-vcpkg --vcpkg check

# Usa vcpkg já instalado em um caminho próprio
python3 tools/PrepareApple.py --platform ios --only-vcpkg \
  --vcpkg-executable /caminho/para/vcpkg

# Continua aceitando o arquivo Python já baixado (SHA-256 verificado)
python3 tools/PrepareApple.py --platform ios --only-python \
  --archive build/iOS-deps/python/Python-iOS.tar.gz
```

### Diretórios mantidos no mesmo padrão dos arquivos originais

| SDK | Triplet | Diretório final |
| --- | --- | --- |
| macOS | `arm64-osx` | `buildsystems/xcode/vcpkg/macos/arm64-osx/` |
| iOS dispositivo | `arm64-ios` | `buildsystems/xcode/vcpkg/ios/arm64-ios/` |
| iOS simulador Apple Silicon | `arm64-ios-simulator` | `buildsystems/xcode/vcpkg/simulator/arm64-ios-simulator/` |

O script consulta o manifesto **real** `vcpkg.json` do Kotonoha e não tenta
inventar versões de portas ou substituir seu manifesto existente. As
instalações são separadas por plataforma e protegidas por lock para evitar
instalações simultâneas no mesmo diretório. No modo padrão `auto`, se faltar
algo necessário à linkedição o vcpkg será invocado; `--vcpkg install` força
a execução mesmo quando já há bibliotecas.

**Correção para `Escaping archive link`:** o BeeWare pode conter aliases
simbólicos como `Python.xcframework -> ../Python.xcframework`. A extração
seleciona apenas a árvore que tem o verdadeiro `Info.plist`, ignorando os
aliases externos a ela. Os links internos ainda são verificados para impedir
saída da árvore de extração. Assim, um arquivo de download validado por SHA-256
pode ser utilizado sem baixar novamente.

**Limites da automação:** as bibliotecas vcpkg, frameworks SDL, Python e o
conteúdo dos assets não são redistribuídos neste ZIP. As versões instaláveis
dependem do `vcpkg.json` original, dos triplets disponíveis na versão
do vcpkg, dos compiladores e da compatibilidade dos ports com iOS. A
configuração fornecida usa `arm64` em macOS e no simulador; builds `x86_64`
precisam de instalações de bibliotecas/triplets adicionais. Valide com Xcode
local antes de distribuir pacotes assinados.


## Correção do build geral (13/11/2026)

Após a verificação do log do Xcode, foram corrigidas as referências do
`Python.xcframework` de `../../python/...` para `python/...` (relativas a
`buildsystems/xcode/`). O arquivo do projeto foi disponibilizado com o nome
`Kotonoha.xcodeproj` para corresponder ao caminho do comando já utilizado
pelo usuário. As referências `ReferencedContainer` dos 22 schemes também
foram atualizadas para esse nome.

O `xcodebuild -alltargets` não executa as Build Pre-Actions dos schemes;
portanto, o script `tools/BuildAllApple.sh` prepara antecipadamente vcpkg
e CPython, seleciona os SDKs e constrói os schemes sequencialmente.

```sh
# macOS Apple Silicon
bash tools/BuildAllApple.sh --platform macos

# iOS para dispositivo (sem assinatura, compilação apenas)
bash tools/BuildAllApple.sh --platform ios

# iOS Simulator Apple Silicon
bash tools/BuildAllApple.sh --platform simulator

# Todas as três variantes
bash tools/BuildAllApple.sh --platform all
```

O script usa `CODE_SIGNING_ALLOWED=NO` exclusivamente no comando
`xcodebuild` (não modifica o projeto e não gera .app instalável no aparelho).
Para distribuição ou execução física em iOS, configure assinaturas e
provisionamento no Xcode e compile pelo scheme do aplicativo.

A configuração de `ARCHS=arm64` corresponde às dependências vcpkg disponíveis
(`arm64-osx`, `arm64-ios` e `arm64-ios-simulator`) e ao Mac Apple Silicon.
Para criar builds universais Intel + Apple Silicon será necessário também
compilar as bibliotecas para x86_64 e ajustar as configurações de arch.

O arquivo `Info.plist` compartilhado, a organização `Modules/VM` e
`Modules/Providers`, os assets globais e `game-assets` de cada jogo,
e o empacotamento SDL permanecem inalterados.

**Limitação:** a sintaxe foi validada, mas a compilação real não foi executada
no ambiente de geração por não haver Xcode e código-fonte completo.


## Correção dos headers CPython para DokiDoki (13/11/2026)

Erro observado no Xcode:

```text
games/doki-doki-literature-club/src/Python.cpp:5:10:
fatal error: 'Python.h' file not found
```

O target conseguia encontrar o `Python.xcframework`, mas os headers do
CPython 3.15 não estavam em `HEADER_SEARCH_PATHS` do compilador.
Os dois novos arquivos de configuração, aplicados **somente** aos Debug e
Release de DokiDoki, são:

- `Config/DokiDoki.macOS.xcconfig`: adiciona `macos-arm64_x86_64/Python.framework/Versions/3.15/include/python3.15` (mais `Headers`).
- `Config/DokiDoki.iOS.xcconfig`: usa `ios-arm64/Python.framework/Headers` em `iphoneos` e `ios-arm64_x86_64-simulator/Python.framework/Headers` em `iphonesimulator`, com o fallback `include/python3.15` de cada fatia.

Ambos incluem `Game.<platform>.xcconfig` e preservam os caminhos de includes
normais do engine, SDL, vcpkg e módulos. O aviso de C++20 de `Runtime.hpp`
não é a causa fatal do erro, portanto o padrão C++17 original foi mantido.

Antes de compilar o DokiDoki, execute:

```sh
python3 tools/PrepareApple.py --platform macos --only-python
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'DokiDoki (macOS)' -configuration Debug \
  -destination 'generic/platform=macOS' \
  CODE_SIGNING_ALLOWED=NO build
```

A compilação real não foi executada no ambiente de geração (sem o Xcode e os
binários originais). Verifique os paths efetivos com
`xcodebuild -showBuildSettings ... | grep HEADER_SEARCH_PATHS`.
