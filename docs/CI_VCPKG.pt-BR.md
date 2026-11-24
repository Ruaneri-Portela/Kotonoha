# GitHub Actions e CI com vcpkg

As workflows de compilação em `.github/workflows/` são disparadas manualmente por **Actions → Run workflow** na branch `dev`. Elas publicam binários e pacotes sem incluir arquivos comerciais dos jogos.

| Workflow | Runner e plataformas | Artefatos e verificações |
| --- | --- | --- |
| [`linux.yml`](../.github/workflows/linux.yml) | **Um** job `ubuntu-24.04`; Linux `x64-linux-dynamic` **e depois** WASM `wasm32-emscripten` (Emscripten 3.1.73) | ZIPs e saídas separados por plataforma; testes nativos sintéticos, exports WASM, testes de navegador e HTTP |
| [`windows.yml`](../.github/workflows/windows.yml) | Windows x64, `x64-windows` | ZIPs, binários e testes sintéticos |
| [`macos.yml`](../.github/workflows/macos.yml) | Matriz de arquiteturas nativas macOS e triplets dinâmicos | Bundles/ZIPs e testes sintéticos |
| [`android.yml`](../.github/workflows/android.yml) | Android ARM64, NDK e Gradle | APKs Debug e ZIP, conferência das bibliotecas empacotadas |

O antigo `wasm.yml` foi removido. Linux e WASM são compilados em sequência no **mesmo runner**, evitando duas máquinas virtuais de compilação, com diretórios CMake e artefatos separados.

**Política de cache:** nenhuma workflow restaura ou grava os diretórios de **compilação incremental** dos jogos e do motor. Mantemos apenas os arquivos binários de **dependências do vcpkg**, separados por triplet, para não recompilar bibliotecas externas desnecessariamente. O SDK do Emscripten pode usar cache de instalação, e o cache de build do Gradle foi desativado. Artefatos publicados pelo GitHub Actions são independentes desses caches.

Em Linux, Windows, macOS e WASM, selecione a feature `sdl-vcpkg` com `KOTONOHA_SDL_PROVIDER=VCPKG`. Dear ImGui sempre é compilado a partir do submódulo Git fixado pelo projeto, inicializado automaticamente pelo CMake quando ausente. Android continua usando os submódulos SDL e ImGui para integração nativa.

Exemplo de configuração no Linux:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build
```

Para WASM, utilize `wasm32-emscripten` e a ferramenta de chainload CMake do Emscripten. Selecionar SDL/ImGui via vcpkg sem configurar a toolchain é erro de configuração.

As workflows de contratos/código podem rodar em pushes e PRs. **Passar nos testes sintéticos não equivale a validar todas as plataformas ou os jogos comerciais.** A workflow `xcode.yml` valida a configuração do projeto Xcode, sem compilar nem assinar todos os alvos Apple.
