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

O adaptador de paths físicos segue disponível para os providers antigos. Próximos marcos: migrar pacotes CMP e indexadores de Summer; migrar demais providers; adaptar FFmpeg via AVIO; implementar Fetch/Blob com modelo seguro de navegador e modos BIN/CUE necessários. Backend HTTP, ponte síncrona Web e adaptador FFmpeg ainda não estão implementados.

A política StoragePaths foi movida para `lib/src/Filesystem/StoragePaths.cpp`; o antigo `core/FileSystem.hpp` é um shim. Os nomes públicos e os diretórios de saves permanecem iguais.
