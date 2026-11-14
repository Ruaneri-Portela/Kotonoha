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

Quando não existe pasta local `game-assets` selecionada, a página tenta
automaticamente carregar o manifesto do servidor. Arquivos locais são
comparados com o manifesto por **ID lógico sem distinção de caixa ASCII e
tamanho**; arquivos ausentes ou com tamanho divergente são fornecidos pelo
montador virtual Fetch. Assets HTTP e placeholders remotos **não** são
copiados para MEMFS.

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

Hospedagem em outra origem exige CORS correto. HTTP não fornece listagem
genérica de diretórios: o manifesto é obrigatório. Erros HTTP, manifesto
inválido ou arquivos remotos acima do limite atual de **128 MiB para
pré-carregamento automático** mantêm a seleção de origem aberta, sem iniciar
o motor com dados incompletos. O backend Fetch também tem suporte a requisições
Range limitadas, mas instalações remotas grandes ainda exigem estratégia
de suspensão por Worker/Asyncify compatível com o loop do navegador.

Use `window.KotonohaAutoServerAssets = false` antes da inicialização para
manter a escolha manual entre pasta e servidor. O self-test web já usa a
escolha manual.

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

O botão **+ Adicionar pasta**, no canto superior direito, continua disponível
depois da inicialização. Selecione uma pasta local e informe seu nome no
filesystem virtual, por exemplo `game-assets`, `forced_subtitles` ou
`ovrlay`. Seus arquivos ficam disponíveis em `/<nome>/` no Emscripten.
Os nomes são validados e `/save` permanece reservado para os saves.
O HTML evita sobrescrever arquivos existentes.

Após a inicialização do filesystem, também é possível chamar
`window.KotonohaMountBrowserFolder(fileList, "forced_subtitles")`.

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
