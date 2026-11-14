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
- **Assets do servidor:** `tools/generate_manifest.py` gera `game-assets/manifest.json`; `WebAssetFetch.cpp` baixa os dados via `emscripten_fetch` e os coloca no MEMFS. Uma URL **não** é automaticamente um arquivo local.
- **Pasta local:** o seletor da página importa os arquivos escolhidos para o sistema virtual. Áudio/vídeo também podem usar URLs de objetos `File`.
- **Mídia:** o player tenta decodificação do navegador e usa FFmpeg em WebAssembly como fallback. Arquivos `.webm` opcionais ajudam com codecs antigos; crie-os apenas com material permitido.

Para o autoteste opcional, habilite `-DKOTONOHA_WEB_SELFTEST=ON`, compile `KotonohaWebSelfTest` e execute `node tools/web_selftest.js build-web/outputs/KotonohaWebSelfTest`. Para depurar C++, use `-g3` e suporte DWARF nas DevTools do Chrome.

**Limites:** permissões do navegador, downloads assíncronos, suporte a codecs e execução sem threads diferem do desktop. Não trate o disco do usuário como um filesystem POSIX acessível diretamente.
