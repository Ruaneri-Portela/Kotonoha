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

## Armazenamento e assets no navegador

- **Saves:** volume IDBFS em `/save`, sincronizado com IndexedDB pelo adaptador web.
- **Assets do servidor:** `tools/generate_manifest.py` gera um manifesto com tamanhos; o navegador monta Fetch como `IMount` somente para leitura, prioriza arquivos locais e não duplica bytes HTTP em MEMFS.
- **Pasta local:** o seletor da página importa os arquivos escolhidos para o sistema virtual. Áudio/vídeo também podem usar URLs de objetos `File`.
- **Mídia:** o player tenta decodificação do navegador e usa FFmpeg em WebAssembly como fallback. Arquivos `.webm` opcionais ajudam com codecs antigos; crie-os apenas com material permitido.

Para o autoteste opcional, habilite `-DKOTONOHA_WEB_SELFTEST=ON`, compile `KotonohaWebSelfTest` e execute `node tools/web_selftest.js build-web/outputs/KotonohaWebSelfTest`. Para depurar C++, use `-g3` e suporte DWARF nas DevTools do Chrome.

**Limites:** permissões do navegador, downloads assíncronos, suporte a codecs e execução sem threads diferem do desktop. Não trate o disco do usuário como um filesystem POSIX acessível diretamente.


## Fallback HTTP(S) de game-assets (Filesystem V2 experimental)

**O início aguarda uma escolha explícita:** a página oferece
**Usar pasta local** ou **Usar HTTP(S)**, sem temporizador nem seleção
automática. A escolha local importa `game-assets` e inicia **sem consultar
HTTP**, mesmo que exista um manifesto no servidor. A escolha HTTP(S) ativa
o provider remoto com manifesto, ou Fetch direto se o manifesto estiver
indisponível. Os dados HTTP não são copiados para MEMFS.

Publique `game-assets/manifest.json` com entradas e tamanhos:

```json
{
  "entries": [
    {"path": "DATA/GRAPHIC.CMP", "size": 12345},
    {"path": "OPENING.WEBM", "size": 67890}
  ]
}
```

Gere o manifesto no deploy com
`python3 tools/generate_manifest.py ./game-assets ./game-assets/manifest.json`.
O campo `files` continua disponível por compatibilidade com leitores de
manifesto existentes.

O endereço padrão é `game-assets/` relativo à página. Para usar um CDN
HTTPS, configure **antes do script de inicialização**:

```html
<script>
  window.KotonohaAssetBaseURL = "https://cdn.example.org/meu-jogo/game-assets/";
</script>
```

A hospedagem em outra origem exige CORS correto. O manifesto agora é
**opcional**: se não existir, o navegador configura Fetch HTTP direto
para caminhos de arquivos conhecidos. HTTP não permite enumerar diretórios
sem índice. O limite de **128 MiB de pré-carregamento** se aplica somente
ao fluxo legado com manifesto. O modo direto usa HEAD, Range, Asyncify e
cache de memória limitado; servidores sem Range são indicados apenas
para arquivos menores. A leitura de rede não bloqueia a thread principal
do navegador.

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
reaberto pelo modo de tela cheia. Pastas adicionais devem ser importadas
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


### Fetch sem manifesto com Asyncify

Quando o HTML não consegue carregar `game-assets/manifest.json`, registra
`game-assets/` diretamente como origem Fetch, sem exigir um índice. O provider
solicita HTTP `HEAD` para descobrir o tamanho dos **caminhos conhecidos**.
Depois, usa `GET Range` para ler partes dos arquivos e manter os bytes em cache.
Se um servidor ignorar Range e devolver o objeto completo com HTTP 200, o
provider aceita a resposta quando o tamanho confere. Recursos remotos não
são copiados ao MEMFS.

O link WebAssembly habilita `-sASYNCIFY=1` para que `exists()`, `open()`
e `FileHandle::read()` possam suspender temporariamente a execução C++ até
o navegador concluir a requisição; não é acesso à rede síncrono. O HTTP 404
fica registrado como inexistente. Arquivos grandes exigem suporte real a
HTTP Range (206 e Content-Range). Um servidor mal configurado pode devolver
a imagem completa, então a hospedagem precisa de limites. Para outro domínio,
configure CORS permitindo HEAD/GET e expondo Content-Length/Content-Range.

Para requisitar um caminho conhecido explicitamente, use
`await window.KotonohaFetchAsset("arquivo.bin")` (1 = pronto,
0 = pendente, -1 = requisição inválida). Também é possível definir
`window.KotonohaAssetBaseURL` antes da inicialização.

HTTP comum não fornece enumeração de diretórios sem índice. Provedores
que precisam descobrir nomes desconhecidos ou abrir arquivos físicos ainda
dependem de migração. As operações Asyncify/SDL precisam de validação contra
reentrância e memória; esta etapa **não comprova gameplay completo**.

### Validação do navegador

```sh
node tools/check_web_shell.js
node --check tools/web_selftest.js
```

O teste Playwright cobre Fetch sem manifesto, pastas locais personalizadas,
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
