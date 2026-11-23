# Outputs portáteis, SDL3 e empacotamento ZIP

[English (US)](BUILD_PACKAGES.md) | **Português (Brasil)**

## Ciclo de vida SDL3 compartilhado

Os ports de jogos utilizam `Kotonoha/core/SDLCallbackApp.hpp`. Cada jogo define
uma classe com métodos estáticos `init`, `event`, `iterate` e `quit`;
a macro `KOTONOHA_SDL_CALLBACK_APP(MeuPorte)` exporta os quatro callbacks
exigidos pelo SDL3. Não é necessário escrever `SDL_AppInit` nem uma segunda
`main`. A classe do jogo preserva a lógica de launcher, origem dos assets e
encerramento. Exemplo: `games/example/src/main.cpp`.

Recursos SDL alocados devem usar as estruturas RAII de
`Kotonoha/core/SDL.hpp`: `WindowPtr`, `RendererPtr`, `TexturePtr`,
`SurfacePtr`, `IOStreamPtr`, `AudioStreamPtr`, `FontPtr`,
`SDLMemoryPtr<T>`, `SDLArrayPtr<T>`, `AudioDeviceHandle`,
`GamepadPtr` e `JoystickPtr`. Utilize `.get()` para empréstimos
temporários e `std::move` para transferir propriedade. **Não execute
SDL_Destroy*/SDL_free manualmente** em recurso pertencente a RAII.
A migração dos campos legados é incremental; não transforme referências
compartilhadas/emprestadas em ponteiros proprietários sem verificar a posse.

## Outputs e bibliotecas compartilhadas

| Plataforma | Pasta do jogo | Dependências dinâmicas |
|---|---|---|
| Windows | `build/outputs/<Jogo>/` | DLLs **ao lado** do executável |
| Linux | `build/outputs/<Jogo>/` | `libs/*.so*`, `$ORIGIN/libs` |
| macOS sem bundle | `build/outputs/<Jogo>/` | `libs/*.dylib`, `@loader_path/libs` |
| macOS com bundle | `build/outputs/<Jogo>.app` | `Contents/Frameworks` |

O passo POST_BUILD resolve as dependências da aplicação, copia bibliotecas
compiladas pelo próprio CMake mesmo sem vcpkg, e encontra as dependências
do vcpkg. No macOS também procura no Homebrew, ajusta os `install names` e
assina as dylibs copiadas; bundles usam `fixup_bundle`. No Linux recomenda-se
`patchelf` para corrigir o RPATH de bibliotecas transitivas (sem ele aparece
um aviso). Bibliotecas essenciais do sistema operacional não são empacotadas.

## ZIP por jogo usando CPack

Após configurar e compilar os jogos habilitados:

```sh
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON -DKOTONOHA_BUILD_PMAIL=ON
cmake --build build --parallel 4
cmake --build build --target package
```

O CPack cria um ZIP individual por jogo habilitado em `build/packages/`,
com versão/branch/commit no nome. Cada pacote preserva a organização de
output do executável, recursos e libs dinâmicas. Para empacotar apenas um:

```sh
cpack --config build/CPackConfig.cmake -G ZIP \
  -D CPACK_COMPONENTS_ALL=Kotonoha
```

**Dados comerciais não entram por padrão**, evitando sua redistribuição
acidental; o exemplo livre `Kotonoha` pode conter os assets de demonstração.
Se o usuário possui autorização para incluir seus próprios arquivos locais,
pode habilitar `-DKOTONOHA_CPACK_INCLUDE_GAME_ASSETS=ON`. Isso não autoriza
distribuir assets comerciais sem licença. No Android, APK/AAB continuam sendo
produzidos pelo Gradle.

## Memória no WebAssembly

O WASM começa com 128 MiB e pode crescer até 1 GiB por padrão. O Fetch usa
cache LRU de 48 MiB e páginas de 1 MiB. Opções:

```sh
cmake -S . -B build-web \
  -DKOTONOHA_WASM_INITIAL_MEMORY=134217728 \
  -DKOTONOHA_WASM_MAX_MEMORY=1073741824 \
  -DKOTONOHA_WASM_FETCH_CACHE_MIB=48
```

Mesmo com uso menor de memória, grandes GPK/BIN exigem servidor HTTP
com `Range` e resposta `206 Partial Content`. É preciso validar
picos reais em Android, iPhone/iPad e em cada jogo.
