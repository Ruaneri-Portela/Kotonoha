# Kotonoha WebAssembly: engine dinâmica compartilhada

## Objetivo

O modo padrão do Emscripten continua sendo **estático**: cada
`<Jogo>.wasm` contém sua cópia do código da engine. O modo opcional
`KOTONOHA_WASM_SHARED_ENGINE=ON` cria uma biblioteca dinâmica WASM
versionada, compartilhada pelos ports no mesmo diretório de distribuição.

O Emscripten separa os módulos em:

- **MAIN_MODULE=2**: `<Jogo>.js` e `<Jogo>.wasm`, com o entrypoint SDL,
  as bibliotecas de sistema, o runtime JavaScript, os exports `ccall`
  usados pelo Fetch e a configuração do IDBFS.
- **SIDE_MODULE=1**: `libs/libkotonoha-<versão>.wasm`, com o código
  C++ do Kotonoha, depurador, renderização, traduções, dublagem e
  provedores. O ImGui da engine faz parte desse módulo.

O motor é compilado uma vez, mesmo com vários ports habilitados.
Apenas o bootstrap e o código próprio de cada jogo ficam no seu
`<Jogo>.wasm`. O nome da biblioteca inclui a revisão da engine para
evitar misturar versões incompatíveis.

> **Atenção:** Emscripten não possui um mecanismo de bibliotecas dinâmicas
> independente de seu runtime. `MAIN_MODULE` ainda traz libc++, SDL3,
> FFmpeg e outras dependências de sistema exigidas pelo jogo. O objetivo
> é não duplicar **o código do Kotonoha** em cada `<Jogo>.wasm`, não
> tornar cada jogo um pequeno plugin sem runtime próprio.

## Compilação

Configure a partir da raiz, com **Emscripten** e as dependências
cross-compiled da mesma forma que no build WASM existente:

```sh
emcmake cmake -S . -B build-wasm-dynamic -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DKOTONOHA_BUILD_WASM=ON \
  -DKOTONOHA_WASM_SHARED_ENGINE=ON \
  -DKOTONOHA_BUILD_EXAMPLE=ON

cmake --build build-wasm-dynamic --parallel 4
python3 tools/serve_web.py --root build-wasm-dynamic/outputs --port 3000
```

Abra `http://localhost:3000/Kotonoha/index.html` para o Example.
Para compilar vários ports, habilite seus respectivos
`KOTONOHA_BUILD_<PORT>=ON` ou `KOTONOHA_BUILD_ALL=ON`
(a restrição atual para DDLC/Katawa no WASM permanece).

A flag `KOTONOHA_WASM_SHARED_ENGINE=ON` fora do Emscripten falha com
um diagnóstico. A opção OFF mantém o comportamento anterior; não misture
os dois modos no mesmo diretório de build.

## Layout publicado

```text
outputs/
  libs/
    libkotonoha-<versão>.wasm
  Kotonoha/
    index.html
    Kotonoha.js
    Kotonoha.wasm
    Kotonoha.data
    KotonohaPreloadCache.js
    KotonohaSharedWasmCache.js
    service-worker.js
    game-assets/
  Amagami/
    index.html
    Amagami.js
    Amagami.wasm
    ...
```

O site deve publicar **`outputs/` inteiro na mesma origem HTTP**.
Não publique apenas `outputs/Kotonoha/`: o carregador resolve
`../libs/libkotonoha-<versão>.wasm`. Evite alterações de nome,
redirecionamentos e respostas HTML para URLs `.wasm`. O MIME deve ser
`application/wasm`.

O `Module.locateFile` da página encaminha apenas a biblioteca dinâmica
para a pasta compartilhada. Cada jogo conserva sua própria instalação
PWA, seus arquivos de save, `game-assets/` e seu provedor HTTP Fetch.

O arquivo JS `KotonohaSharedWasmCache.js` intercepta **apenas** o GET
do engine WASM versionado. Seu CacheStorage pertence à mesma origem,
permitindo reutilização e fallback offline entre jogos após o primeiro
download concluído, quando CacheStorage está disponível. Não interfere
com pedidos `Range`, `game-assets`, indexação ou LRU do IndexedDB.
O Serviço Worker do jogo continua limitado ao escopo do próprio PWA.

## Asyncify e compatibilidade

Como funções C++ de um módulo dinâmico podem suspender durante HTTP Fetch,
o build aplica Asyncify ao side module e ao main module. Um gerador produz
a lista `ASYNCIFY_IMPORTS` a partir dos exports da engine antes de ligar
os jogos. O compilador deve construir engine, jogos e bibliotecas ligadas
com opções compatíveis de exceções, PIC, memória e Emscripten.

Validações disponíveis:

```sh
python3 tools/test_wasm_shared_engine.py
node tools/test_wasm_shared_cache.js
node tools/check_web_shell.js

# Executados automaticamente após o link real:
python3 tools/check_wasm_asyncify.py outputs/Kotonoha/Kotonoha.js
python3 tools/check_wasm_shared_layout.py \
  outputs/Kotonoha/Kotonoha.js outputs/libs/libkotonoha-<versão>.wasm
```

**Limitação de validação:** a auditoria de contratos, testes sintéticos e o
smoke test de linking são distintos de um build integral de todos os ports.
Sem os assets originais e um navegador real, não se pode afirmar que
todas as cenas/decodificadores passam. Se um pacote de terceiros não foi
compilado como PIC ou a ABI do Emscripten for diferente, será necessário
recompilá-lo com o toolchain correspondente.

## Padrões dos ports

Os ports continuam implementando `Runtime::debugNodes`,
`Runtime::translationSources`, `Runtime::activeTranslationKeys` e
`Runtime::debugSceneAudio`; os recursos são consultados via
`AssetProvider`. Não é necessário copiar o depurador, editor de tradução,
navegador de assets ou applet de dublagem para cada jogo. Consulte
`docs/PORT_DEBUG_INTEGRATION.pt-BR.md`.
