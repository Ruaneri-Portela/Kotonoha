# Matriz de migração do Filesystem V2

[English (US)](MIGRATION.md) | **Português (Brasil)**

Este relatório diferencia **conversão de I/O para SDL3** de **suporte real a origens virtuais**. Substituir `std::ifstream` por `Kotonoha::Filesystem::InputFile` ainda pode abrir um arquivo físico; não significa que o provider já aceite `FileHandle` virtual.

| Jogo | I/O convertido | Montagem/indexação virtual |
| --- | --- | --- |
| Example | Usa a camada SDL3 compartilhada | Ainda depende do provider padrão do motor |
| Amagami | Leitura de Session por adaptador SDL3 | Parsers de disco e bootstrap virtual pendentes |
| Zero no Tsukaima | Session e Frontend migrados para SDL3 | RAW disc/CUE e integração VFS pendentes |
| Snow Radish | Streams de Pack e Session convertidos | Pacotes ainda indexam caminhos físicos |
| Summer Radish | Pacotes CMP e vídeos usam `FileHandle` | Piloto pasta/ISO/overlay de fonte; falta build de regressão |
| DDLC | RPA, arquivos soltos e inicialização usam handles virtuais | Piloto pasta/ISO/TAR; falta regressão de VNX/overlays |
| School Days | KTRF, GPK e saves usam adaptadores SDL3 | Indexação/streaming GPK ainda exigem migração de VFS |
| RealLive | Voz usa SDL3, leitor ISO existente mantido | Integração geral do provider ISO ainda exige testes |
| Kirikiri | Leitura de XP3 não usa mais stdio C | Indexação do XP3 continua baseada em paths físicos |
| LucaSystem | PAK usa adaptadores SDL3 | Indexação PAK ainda baseada em caminhos físicos |
| Pure Mail | PAK e runtime usam adaptadores SDL3 | Indexação PAK ainda baseada em caminhos físicos |

Os providers compartilhados `FileSystemAssetProvider` e `IndexedFileSystemAssetProvider` aceitam montagem VFS. Isso **não** migra automaticamente todos os formatos dos jogos.

## Auditar os códigos

```sh
python3 tools/audit_filesystem_v2.py
python3 tools/audit_filesystem_v2.py --strict
```

O comando padrão encontra acessos diretos legados e confere CMake/Xcode; `--strict` falha se restarem chamadas diretas. A auditoria não substitui compilação, CTest, testes dos jogos nem navegador.

## Trabalhos ainda necessários

- Compilar e executar testes por plataforma (Xcode/Android/WebAssembly não verificados).
- Migrar a indexação dos arquivos-fonte de cada provider por `Filesystem::open(id)`, com overlays montados antes de indexar.
- Finalizar o bootstrap web do `FetchAssetProvider`, ponte Worker/Asyncify e testes de rede/CORS/Range. O backend atual faz pré-carregamento assíncrono sem bloquear.
- Conectar o `BrowserBlobAdapter` a todas as rotas web legadas, validar ciclo de vida/seek do FFmpeg, CUE multi-BIN e modos de disco restantes.
- Remover adaptadores de paths físicos somente após testes equivalentes nos onze jogos.

Os critérios finais estão na [especificação original](SPECIFICATION.pt-BR.md). O PR permanece rascunho.

- **Fallback web:** `FallbackMount` local-primeiro, bootstrap de manifesto HTTP e registro do Fetch foram implementados, com pré-carregamento máximo de 128 MiB. Faltam testes reais de navegador e migração integral dos providers legados.
