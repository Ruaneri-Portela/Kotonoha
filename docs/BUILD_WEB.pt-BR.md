# WebAssembly (Emscripten)

[English (US)](BUILD_WEB.md) | **Português (Brasil)**

Kotonoha compila ports selecionados para navegador com Emscripten. Instale e ative **emsdk**, além de CMake, vcpkg, Python 3 e os overlay ports necessários.

```sh
source /caminho/para/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DVCPKG_OVERLAY_PORTS=vcpkg-overlays/ports \
  -DKOTONOHA_BUILD_REALLIVE=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --target RealLive
```

Os caminhos das toolchains variam conforme a instalação. `KOTONOHA_BUILD_ALL=ON` seleciona os ports web suportados, mas exclui DDLC. Sirva o resultado por **HTTP**, não por `file://`.

## emsdk oficial (Windows, macOS, Linux)

Instale o emsdk **fora do MSYS2**. No Windows os scripts usam `E:\emsdk` por
padrão; sobrescreva com a variável de ambiente `EMSDK`.

```powershell
git clone https://github.com/emscripten-core/emsdk.git E:\emsdk
E:\emsdk\emsdk.bat install latest
E:\emsdk\emsdk.bat activate latest
```

No macOS/Linux carregue `emsdk_env.sh`. O pacote Emscripten do MSYS2 não é
suportado: o Binaryen trava em `-O3` e perde os exports do Asyncify.

Use o preset CMake `wasm-vcpkg` pelo wrapper PowerShell, que exporta
`EMSCRIPTEN_ROOT`/`EMSDK` para a toolchain de chainload:

```powershell
& .\tools\build-wasm.ps1                  # configura + compila
& .\tools\build-wasm.ps1 -ConfigureOnly   # somente configura
& .\tools\build-wasm.ps1 -Target Kotonoha # um alvo
```

A saída fica em `out/build/wasm-vcpkg/packages/`. No macOS/Linux use
`tools/build-wasm.sh` ou `tools/configure_wasm.sh` com o mesmo preset.

## Auxiliar de configuração (`tools/configure_wasm.sh`)

Algumas instalações (por exemplo, Emscripten via Homebrew) não exportam
`EMSCRIPTEN_ROOT`, que o triplet do vcpkg exige. `tools/configure_wasm.sh`
descobre a raiz a partir do `emcc` no `PATH` e executa `emcmake cmake` com o
triplet `wasm32-emscripten`, os overlays de ports e as features `sdl-vcpkg` /
`imgui-vcpkg` em `build-web/`. `VCPKG_ROOT` deve estar definido. As flags do
Asyncify continuam em `buildsystems/cmake/games/KotonohaApp.cmake`.

```sh
./tools/configure_wasm.sh
cmake --build build-web --target RealLive

KOTONOHA_WASM_CONFIG=Debug ./tools/configure_wasm.sh
cmake --build build-web --target Kotonoha
```

Sobrescreva `KOTONOHA_WASM_BUILD_DIR` ou passe flags extras do CMake como
argumentos. Sempre carregue `emsdk_env.sh` (ou defina `EMSCRIPTEN_ROOT`) antes
do primeiro configure: a toolchain Emscripten é escolhida nesse momento.

## Um PWA instalável para cada jogo web

Cada target compilado com Emscripten vira um Progressive Web App próprio em
`build-web/outputs/<target>/`. A pasta inclui `index.html`,
`manifest.webmanifest`, `service-worker.js`, ícones PNG 180/192/512,
`KotonohaPreloadCache.js` e os arquivos JS/WASM/data do jogo. Nome,
identidade da instalação, cor do ícone e escopo do Service Worker são
específicos de cada jogo. Assim, `/RealLive/` e `/Kirikiri/` são PWAs
independentes. Os ícones são gerados sem dependências extras de Python.

Para instalar, hospede por **HTTPS** (ou `http://localhost` no
desenvolvimento) e use **Instalar aplicativo** no navegador ou **Adicionar à
Tela de Início** no iOS. O Service Worker armazena o shell e, após a
inicialização, os arquivos JS/WASM/data do runtime para uso offline posterior.
Ele **não intercepta `game-assets/`**, requisições `HEAD` ou HTTP `Range`.
Assets de jogos ficam no cache de blocos do IndexedDB, separado do PWA.
Instalar o PWA não baixa todos os arquivos do jogo: jogar offline exige que os
dados necessários já estejam no cache. Limites de armazenamento ainda se
aplicam.

### Gerar preload.json pelo console

Escolha a origem HTTP(S), navegue pelos menus e cenas que devem fazer parte
do primeiro boot e, no console de desenvolvedor, execute:

```javascript
await KotonohaDumpPreload();
```

O navegador baixa um `preload.json`. Publique-o em
`game-assets/preload.json`, ao lado do `manifest.json`. O dump registra
**somente os arquivos e intervalos realmente baixados** até aquele momento;
não copia todos os nomes do manifesto. O runtime também registra os nodos
visitados, quais assets foram baixados em cada nodo e as transições observadas.
O campo opcional `nodes` guarda esse grafo, usado pelo JavaScript para
prever assets de nodos vizinhos a cada mudança de cena notificada pelo C++.
O prefetch especulativo utiliza um limite inicial de 8 MiB, duas requisições
paralelas e pode ser cancelado após uma mudança de cena. O campo `entries`
é **o conjunto obrigatório do boot**: todos esses bytes precisam estar em
cache antes do jogo começar. Para bloquear o boot somente pelos menus e pela
primeira cena, mantenha apenas esses intervalos em `entries`, preservando os
demais em `nodes`. Quando um `preload.json` existir, o
boot HTTP(S) aguarda que todos os trechos exigidos estejam no IndexedDB,
baixando os que faltarem antes de iniciar o WASM. O dump identifica a versão
do manifesto para não combinar caches incompatíveis. O guia e o índice
também podem ser recuperados offline quando já salvos; se faltarem blocos
obrigatórios, o boot permanece bloqueado. A origem local não faz preload HTTP.

## Idioma no navegador e limite do cache de assets

Cada jogo web possui os botões **English / Português** na tela inicial.
A escolha padrão acompanha o idioma do navegador, mas a preferência é
salva no `localStorage`. A tradução cobre seleção de assets, progresso,
montagem de pastas, mensagens de erro e rótulos de acessibilidade.
O idioma dos scripts dentro dos jogos não é alterado por esse seletor.

O cache de blocos no IndexedDB utiliza **LRU**: remove os blocos acessados há
mais tempo primeiro, compartilhando o limite entre jogos da mesma origem web.
O limite padrão é **2 GiB** de chunks binários, sem incluir saves nem o
runtime armazenado pelo PWA. Leituras atualizam a data de último acesso.
Antes de gravar um bloco, o gerenciador libera dados antigos se necessário,
considerando o limite configurado e a estimativa de espaço do navegador.
Em caso de `QuotaExceededError`, remove mais blocos antigos e tenta gravar
novamente. Dados, metadados e contagem de bytes são alterados numa mesma
transação IndexedDB. Os intervalos obrigatórios do `preload.json` permanecem
protegidos contra a limpeza durante a sessão; se não couberem no cache, o
preload falha em vez de iniciar incompleto.

Para definir um limite diferente **antes de iniciar o jogo via HTTP(S)**:

```javascript
window.KotonohaCacheMaxBytes = 512 * 1024 * 1024; // 512 MiB
// Depois de configurar a origem HTTP:
await KotonohaAssetCache.getCacheStats();
```

Alterar esse valor afeta as próximas gravações, não limpa o cache
imediatamente. A limpeza não remove saves em `/save` nem os arquivos do
Service Worker/Cache Storage do PWA. Se não houver espaço para persistir um
asset, a leitura pela rede pode continuar normalmente; o uso offline depende
dos blocos que permaneceram armazenados.

## Armazenamento e assets no navegador

- **Saves:** volume IDBFS em `/save`, sincronizado com IndexedDB pelo adaptador web.
- **Assets do servidor:** `tools/generate_manifest.py` gera um manifesto com tamanhos; o navegador monta Fetch como `IMount` somente para leitura, prioriza arquivos locais e não duplica bytes HTTP em MEMFS.
- **Pasta local:** o seletor da página importa os arquivos escolhidos para o sistema virtual. Áudio/vídeo também podem usar URLs de objetos `File`.
- **Mídia:** o player tenta decodificação do navegador e usa FFmpeg em WebAssembly como fallback. Arquivos `.webm` opcionais ajudam com codecs antigos; crie-os apenas com material permitido.

Para o autoteste opcional, habilite `-DKOTONOHA_WEB_SELFTEST=ON`, compile `KotonohaWebSelfTest` e execute `node tools/web_selftest.js build-web/outputs/KotonohaWebSelfTest`. Para depurar C++, use `-g3` e suporte DWARF nas DevTools do Chrome.

**Limites:** permissões do navegador, downloads assíncronos, suporte a codecs e execução sem threads diferem do desktop. Não trate o disco do usuário como um filesystem POSIX acessível diretamente.


## Manifesto HTTP(S) obrigatório para game-assets

Todos os ports web aguardam a escolha explícita da origem. **Pasta local**
não exige nem consulta manifesto. **HTTP(S)** exige um `manifest.json`
**não vazio**, ao lado do `index.html`, com `entries` contendo caminhos
e tamanhos. Sem manifesto, ou com índice vazio ou inválido, o jogo não
inicia. Se houver falha de rede, um manifesto válido já salvo no
IndexedDB pode ser reutilizado.

A ferramenta Python recebe **dois argumentos posicionais**:
`[pasta] [arquivo]` (diretório a indexar e arquivo de saída):

```sh
python3 tools/generate_manifest.py build-web/outputs/N2SystemGame/game-assets build-web/outputs/N2SystemGame/manifest.json
python3 tools/generate_manifest.py /Volumes/Server/data2/Games/SteinsGate /Volumes/Server/data2/Games/manifest.json
```

`[pasta]` é indexada **diretamente e de forma recursiva**, incluindo arquivos
`.npa` e subpastas como `dx/`. Não há mais detecção automática de uma
subpasta `game-assets/`. `[arquivo]` define exatamente onde gravar o JSON
(caminho absoluto ou relativo ao diretório atual), podendo ficar uma pasta
acima dos arquivos indexados. O próprio arquivo de saída não entra no índice.
No WASM, publique o manifesto ao lado de `index.html` e disponibilize os
arquivos em `game-assets/`, ou configure um mapeamento de URLs no
servidor/CDN. O CMake passa explicitamente os dois caminhos após copiar
os assets.

**O manifesto é uma lista de permissão:** caminho ausente de `entries`
retorna não encontrado imediatamente, sem executar HEAD/GET.
Somente arquivos indexados podem usar Fetch/Range sob demanda e cache.
O `game-assets/preload.json` opcional também precisa referenciar somente
arquivos indexados.

O endereço padrão é `game-assets/` relativo à página. Para usar um CDN
HTTPS, configure **antes do script de inicialização**:

```html
<script>
  window.KotonohaAssetBaseURL = "https://cdn.example.org/meu-jogo/game-assets/";
</script>
```

A hospedagem em outra origem exige CORS correto. O modo direto usa HEAD,
GET Range e Asyncify com cache limitado, sem bloquear a thread principal.
Não é possível listar nomes arbitrários de arquivos em HTTP comum sem
um índice: o porte deve conhecer os nomes dos seus arquivos. PONscripter e
Kirikiri agora consultam scripts e arquivos conhecidos como `arc.nsa` e
`data.xp3` diretamente, sem depender de listagem HTTP. Pacotes com nomes
incomuns ainda exigem pasta local ou índice. O leitor SAR/NSA do PONscripter
mantém arquivos completos em memória e pode exceder os limites do navegador;
o leitor XP3 do Kirikiri utiliza acesso via Range.

A escolha local/remota é sempre manual. A antiga configuração
`window.KotonohaAutoServerAssets` deixou de ser necessária.

```sh
python3 tools/audit_filesystem_v2.py
python3 tools/ValidateBuildMetadata.py
node --check tools/web_selftest.js
# Exige build Emscripten e Playwright configurados:
node tools/web_selftest.js build-web/outputs/KotonohaWebSelfTest
```

Esta integração **ainda não foi validada** num build WebAssembly/navegador.
Alguns indexadores de formatos proprietários ainda exigem caminhos físicos;
logo, não é correto considerar todos os jogos totalmente migrados.

## Montagem de pastas locais personalizadas no navegador

O botão **+ Adicionar pasta** fica disponível **somente na tela de
configuração, antes de iniciar o jogo**. Ele desaparece assim que a origem
local ou HTTP(S) é confirmada, não reaparece durante o gameplay e não é
reaberto pelo modo de tela cheia. O toolbar utiliza `position: fixed`
em vez de `popover`, evitando a centralização automática do Safari/WebKit;
o botão fica no canto superior direito, respeitando a área segura da tela.
O teste `node tools/web_toolbar_layout.js build-web/outputs/KotonohaWebSelfTest`
verifica Chrome e WebKit em orientação vertical, horizontal e desktop.
Pastas adicionais devem ser importadas
antes de iniciar o SDL. Selecione uma pasta local e informe seu nome no
filesystem virtual, por exemplo `game-assets`, `forced_subtitles` ou
`ovrlay`. Seus arquivos ficam disponíveis em `/<nome>/` no Emscripten.
Os nomes são validados e `/save` permanece reservado para os saves.
O HTML evita sobrescrever arquivos existentes.

A API interna `window.KotonohaMountBrowserFolder(fileList, "forced_subtitles")`
continua disponível para integrações que precisem montar arquivos
programaticamente; isso não exibe o botão dentro do jogo.

**Atenção:** montar uma pasta torna seus arquivos visíveis no VFS, mas não
faz automaticamente cada porte procurar recursos nela. Os caminhos de busca
e a jogabilidade por toque precisam ser verificados em cada jogo.


### Fetch com manifesto e Asyncify

O índice fornece listagem de diretórios e tamanhos sem HEAD. Bytes dos
arquivos indexados são obtidos por GET Range, com suspensão via Asyncify
enquanto o navegador responde. Não há XMLHttpRequest síncrono.
Arquivos grandes precisam de HTTP 206 Content-Range; um HTTP 200 completo
pode ser aceito para objetos pequenos de até 8 MiB.

`await window.KotonohaFetchAsset("arquivo-indexado.bin")` consulta o
manifesto (1 = indexado, 0 = ausente, -1 = inválido), sem requisitar
arquivos desconhecidos. Para CDN, defina `window.KotonohaAssetBaseURL`
antes da inicialização e publique o manifesto na raiz da página.

### Validação do navegador

```sh
node tools/check_web_shell.js
node --check tools/web_selftest.js
```

O teste Playwright cobre HTTP com manifesto obrigatório, pastas locais personalizadas,
atualização de mounts existentes, saves, mídia e eventos de toque, quando
compilado com `-DKOTONOHA_WEB_SELFTEST=ON`. Ele não substitui testes
de gameplay de cada jogo e ainda não foi executado neste ambiente.

### Falha `_asyncify_start_unwind is not a function`

O erro indica que o JavaScript gerado tenta suspender a execução usando
Asyncify, mas o WebAssembly correspondente não exporta as funções exigidas.
Isso pode resultar de otimização/link incompatível, arquivos JS/WASM de
compilações diferentes ou cache antigo no navegador/CDN.

O build Web agora usa Asyncify e força **-O2 no link final** para mitigar
uma combinação de otimização associada a esse erro. O script
`tools/check_wasm_asyncify.py` valida os quatro exports reais no
arquivo `.wasm` e falha o build antes da publicação se estiverem ausentes.
Recompile e publique **juntos** os arquivos `.js` e `.wasm`, limpando a
configuração antiga e invalidando o cache do navegador/CDN:

```sh
rm -rf build-web
# Configure novamente com os parâmetros Emscripten/vcpkg acima.
cmake --build build-web --target SummerRadish
python3 tools/check_wasm_asyncify.py build-web/outputs/SummerRadish/SummerRadish.js
```

O verificador detecta exports ausentes, mas não substitui testes reais em
Chromium. Ajuste o nome do target conforme o jogo.

### `Uncaught CppException` durante Fetch e Asyncify

Se o console mostrar `Uncaught CppException {excPtr: ...}` a partir do
callback XHR, a exceção C++ escapou da retomada assíncrona ou do callback.
Isso **não** prova que o erro original seja o Asyncify: recursos ausentes,
headers HTTP inválidos e validações de arquivo também podem provocar falhas.

O CMake agora habilita `-fexceptions` para todos os objetos C++ próprios do
projeto WebAssembly, e não só para o executável; o callback Fetch registra
falhas HTTP e captura exceções antes de devolver o controle ao JavaScript.
O teste `KotonohaWebSelfTestCatchAfterAsyncify` verifica se um `try/catch`
ainda funciona depois de `emscripten_sleep`. O Summer Radish também consulta
pelo nome os arquivos `INI.CMP`, `SCRIPT.CMP`, `GRAPHIC.CMP`,
`SOUND.CMP` e `VOICE.CMP` sem depender de enumeração HTTP.

Depois de alterar as flags de compilação, é necessário limpar o build
Emscripten, reconfigurar, recompilar e publicar os arquivos JS/WASM juntos.
Se o teste em Chromium ou o início real do jogo ainda falhar, examine as
mensagens `[Kotonoha] HTTP asset unavailable` para descobrir qual URL/status
causou a falha. Esses ajustes ainda precisam de validação de runtime no
navegador; não são uma confirmação de gameplay completo.

### Controles, toque e validação em navegadores móveis

O HTML utiliza `viewport-fit=cover`, altura dinâmica do navegador,
controles iniciais de pelo menos 44 px, margens seguras e
`touch-action: none` no canvas SDL. Os controles de origem e de pastas
ficam **somente na configuração pré-jogo**. Após a escolha, o canvas recebe
foco. O SDL inicia `SDL_INIT_GAMEPAD`, recebe conexão/desconexão de
controles e utiliza a camada central de entrada para direcionais/analógico,
troca A/B, toque e gestos. O porte Pure Mail também converte os botões
de controle para navegação no título e nas escolhas da história.

Os testes `Kotonoha.InputActions`, `Kotonoha.NativeInput` e Playwright
cobrem mapeamento, gestos, teclado, visualização vertical/horizontal e
inicialização do subsistema SDL de gamepads. Eles **não simulam um controle
físico** no navegador. É necessário verificar manualmente botões, analógicos,
seleção de pastas no Safari iOS e gameplay completo dos jogos comerciais.

**Checklist antes de integrar à main:** testes C++ Linux, compilação de todos
os jogos WASM habilitados, exports Asyncify em todos os pares JS/WASM,
Playwright Chromium, além de áudio/vídeo/gameplay por porte com dados originais
fornecidos legitimamente. Em dispositivos móveis, testar Android real,
iPhone/iPad Safari em ambas as orientações, gamepads Bluetooth/USB e
suspensão/retomada do navegador. Se o seletor de pastas não tiver suporte,
arquivos individuais só preservam a raiz de `game-assets`; para caminhos
aninhados use HTTP(S) Fetch. A CI valida apenas o escopo automatizado,
**não comprova paridade total de dispositivos**.

### Caminhos esperados para inicialização no navegador

| Porte | Arquivos conhecidos a publicar em `game-assets/` |
|---|---|
| Amagami | `Amagami.bin` ou `Amagami.iso` |
| Zero no Tsukaima | `ZeroNoTsukaima.bin` ou `ZeroNoTsukaima.iso` |
| Snow Radish | `SCRIPT.DAT` e `GRAPHIC.DAT` na raiz, `pack/` ou `data/pack/` |
| Pure Mail | `data/pack/script.dat`, outros DAT e `data/movie/PM01L_I5.avi` (ou sidecar `.avi.webm`) |
| School Days | Rota KTRF embarcada pelo build; os 29 GPKs comerciais na raiz ou `Packs/` |

Os leitores de BIN/ISO usam VFS + HTTP Range, sem copiar um disco inteiro
ao MEMFS. A abertura de vídeo utiliza o FileHandle; reprodução efetiva de
AVI depende do codec suportado ou da sidecar WebM. Não há como identificar
arquivos ausentes do servidor só porque a engine foi inicializada.

### School Days: GPK grande, HTTP 200 e erro de trailer

Se aparecer `HTTP asset unavailable: event01.gpk (HTTP 200)` seguido
de `GPK trailer could not be read`, o servidor provavelmente ignorou o
header `Range`: devolveu o GPK inteiro em vez dos poucos bytes pedidos
para validar seu trailer. **Não aumente o limite de cache** para tentar
resolver isso: o host deve devolver `206 Partial Content` e
`Content-Range` para `GET` com `Range`.

Para desenvolvimento local, encerre o servidor anterior na porta 3000
e use o servidor do repositório, que já implementa esses headers:

```sh
python3 tools/serve_web.py --root build-web/outputs --port 3000
curl -I http://localhost:3000/SchoolDays/game-assets/Event01.GPK
curl -i -H 'Range: bytes=0-15' \
  http://localhost:3000/SchoolDays/game-assets/Event01.GPK \
  -o /dev/null -D -
```

A resposta `HEAD` deve incluir `Accept-Ranges: bytes` e
`Content-Length`. A resposta `GET Range` deve ter status `206`,
`Content-Range: bytes 0-15/TAMANHO` e `Content-Length: 16`. Ajuste
maiúsculas/minúsculas ao nome **real** do arquivo publicado.
Servidores externos precisam oferecer o mesmo suporte; para outra origem,
exponha `Content-Length`, `Content-Range` e `Accept-Ranges` via CORS.

O provider Fetch agora usa um cache LRU limitado; trechos antigos podem
ser removidos e recuperados novamente. Para arquivos grandes, o provider
rejeita um `HEAD` que não anuncie `Accept-Ranges: bytes` em vez de
tentar armazenar o pacote inteiro no navegador. Arquivos pequenos ainda
admitem fallback de corpo completo. Um servidor que **afirme** suportar
Range mas devolva HTTP 200 poderá, contudo, transferir o arquivo inteiro
antes do callback Fetch: configure o servidor corretamente.

Teste independente do servidor:

```sh
python3 -m unittest discover -s tools -p test_serve_web.py -v
```

Ele utiliza um GPK esparso de 320 MiB, requisitando somente os bytes finais;
o teste Playwright também verifica `206` e leitura do trailer via C++.

