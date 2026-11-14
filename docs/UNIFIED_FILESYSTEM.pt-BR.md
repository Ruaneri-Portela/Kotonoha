# Protótipo de filesystem unificado

[English (US)](UNIFIED_FILESYSTEM.md) | **Português (Brasil)**

Esta é uma **implementação inicial e incremental**, isolada em outra branch. Ela não substitui `FileResolver`, provedores de arquivos compactados/ISOs nem os formatos de saves dos jogos.

`UnifiedFileSystem` compõe a política existente `directoryLayout(StorageLocations, game)` e um `AssetProvider` opcional para oferecer:

- domínios `FileDomain::Engine`, `Game`, `Reference` e `Saves`;
- leitura de recursos em ordem de prioridade, incluindo provedores e overlays;
- leitura opcional de bytes (ausência = `std::nullopt`; arquivo vazio = vetor vazio);
- gravação de saves binários no diretório do pacote, com o escritor atômico já existente;
- rejeição de caminhos absolutos, travessia `..` (inclusive com barras invertidas) e links simbólicos que escapem das raízes.

Os `AssetProvider` continuam responsáveis por formatos nativos, codecs, overlays e streaming. O serviço não extrai arquivos desnecessariamente nem altera a instalação original.

## Testes

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target KotonohaUnifiedFileSystemTests
ctest --test-dir build -R '^Kotonoha\.UnifiedFileSystem$' --output-on-failure
```

Os testes usam dados sintéticos temporários: prioridade de caminhos no desktop, Android e iOS, overlays, saves binários, arquivos vazios/ausentes e bloqueio de travessia/symlinks.

**Migração pendente:** ports atuais continuam usando as APIs anteriores. A adoção deve ocorrer por etapas, após validar builds, migração de saves, IDBFS web e streaming nativo.

A fábrica `UnifiedFileSystem::forApplication(game, provider)` usa os caminhos reais da plataforma, mantendo o volume IDBFS `/save` no WebAssembly. Testes unitários usam `StorageLocations` sintéticos.
