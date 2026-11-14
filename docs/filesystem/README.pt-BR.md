# Filesystem V2 — estado de implementação

[English (US)](README.md) | **Português (Brasil)**

A referência principal é a [especificação de arquitetura original](SPECIFICATION.pt-BR.md). O módulo é um **filesystem virtual experimental somente para leitura**; a migração dos leitores antigos ainda não terminou.

## Implementado nesta branch

- `Kotonoha::Filesystem::FileHandle` (read/seek/tell/size), `IMount`, `FileInfo` e erros tipados.
- `SDLFileSource`: leitura de arquivos locais por SDL3 IOStream, sem `std::ifstream`.
- `DirectoryMount`: diretórios indexados sem distinguir caixa ASCII, com caminhos relativos e validação de symlinks.
- `LayeredMount`: overlays de arquivos-fonte com prioridade, listagem mesclada e consulta separada a overrides lógicos. Um pacote substituído não mescla entradas automaticamente com o pacote original.
- `ISO9660Mount`: reutiliza `DiscImage` para ler ranges, sem extrair integralmente ISOs.
- `Filesystem::mount(source)`: diretórios locais, layouts ISO/BIN reconhecidos e arquivos TAR (USTAR/PAX/GNU-longname); outras fontes geram erro explícito.

Saves, configurações e documentos **permanecem fora** do VFS de leitura. A escrita experimental do antigo `UnifiedFileSystem` foi retirada.

## Verificação

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build --target KotonohaFilesystemV2Tests
ctest --test-dir build -R '^Kotonoha\.FilesystemV2$' --output-on-failure
```

Os testes sintéticos cobrem leitura de diretórios/ISO, seek/EOF, overlays, nomes ambíguos, symlinks e cursores independentes. Compilações nativas, Android, WebAssembly e Apple **ainda não foram executadas**.

## Migração pendente

O adaptador de paths físicos segue disponível para providers legados. Pacotes CMP de Summer e RPA de DDLC aceitam fontes virtuais; os outros indexadores ainda precisam dessa migração. Mídia nativa usa AVIO customizado do FFmpeg. O provider Fetch executa pré-carregamento assíncrono e Range limitado, mas ainda não foi testado em navegador. Blocos ausentes geram `WouldBlock` sem bloquear a main thread. O `BrowserBlobAdapter` está disponível para vídeo virtual; a ponte Worker/Asyncify e as rotas HTML legadas ainda estão pendentes.

A política StoragePaths foi movida para `lib/src/Filesystem/StoragePaths.cpp`; todos os includes internos usam `Kotonoha/Filesystem/StoragePaths.hpp` e o shim `core/FileSystem.hpp` foi eliminado. Os símbolos públicos e os diretórios de saves permanecem iguais.

A seleção de montadores aceita `.tar`, `.iso`, `.bin` e `.img` (as imagens de disco são verificadas pelo `DiscImage` existente); outros arquivos geram `UnsupportedMount`. Arquivos `.tar.gz`, UDF e CUE com múltiplas trilhas ainda não estão cobertos.

O novo overload `MediaPlayer::open(FileHandle, logicalName, ...)` pode preparar mídia compatível com o navegador como URL `blob:` gerenciada por `BrowserBlobAdapter` (limite de 64 MiB). Em plataformas nativas continua com AVIO do FFmpeg. As rotas web legadas por path/AssetProvider ainda precisam de migração e testes reais.

## Fallback automático no navegador

No Emscripten, arquivos ausentes ou de tamanho divergente em `game-assets` podem ser atendidos por `FallbackMount(local, FetchAssetProvider)` após o manifesto ser lido assincronamente no preRun. O provider padrão e `Filesystem::mount("/game-assets")` aceitam esse roteamento. A inicialização não prossegue se o pré-carregamento necessário exceder 128 MiB ou o manifesto falhar. Bytes HTTP não são copiados para MEMFS. Veja [build web](../BUILD_WEB.pt-BR.md) e [matriz de migração](MIGRATION.pt-BR.md). O manifesto remoto não migra automaticamente indexadores específicos de jogos.
