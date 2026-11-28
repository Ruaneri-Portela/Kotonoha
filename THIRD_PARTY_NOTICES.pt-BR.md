# Avisos de terceiros e limite de redistribuição

[English (US)](THIRD_PARTY_NOTICES.md) | **Português (Brasil)**

Este documento descreve os componentes de terceiros conhecidos utilizados pelo Kotonoha.
Ele **não** constitui concessão de licença para esses componentes nem para qualquer jogo comercial.

## Código original do Kotonoha

O código original do Kotonoha é distribuído sob a Licença MIT em [LICENSE](LICENSE)
(e na cópia histórica em [lib/LICENSE](lib/LICENSE)). Cabeçalhos de copyright ou SPDX
em arquivos não devem ser usados para relicenciar código obtido de outras fontes.

## Arquivos-fonte de terceiros versionados neste repositório

| Material | Origem (Upstream) | Licença/aviso rastreado aqui |
| --- | --- | --- |
| Shaders Anime4K em `assets/Anime4K/` | https://github.com/bloc97/Anime4K | [Texto MIT](assets/Anime4K/LICENSE-Anime4K.txt), [registro de origem](assets/Anime4K/SOURCE.txt) |
| Shaders ArtCNN em `assets/ArtCNN/` | https://github.com/Artoriuz/ArtCNN | [Texto MIT](assets/ArtCNN/LICENSE-ArtCNN.txt), [registro de origem](assets/ArtCNN/SOURCE.txt) |
| Trechos de provider derivados de open-amagami | https://github.com/hokagocafe/open-amagami | [Texto MIT](modules/providers/amagami/third_party/open-amagami-LICENSE.txt) |
| Subconjunto MIT do parser libreallive em `games/reallive/vendor/libreallive/` | https://github.com/eglaysher/rlvm | [Aviso MIT](games/reallive/vendor/libreallive/LICENSE), [procedência](games/reallive/vendor/libreallive/ORIGIN.md); preserve os cabeçalhos individuais |

Preserve os avisos e atribuições originais ao modificar ou redistribuir esses arquivos.
O registro de origem do Anime4K relaciona apenas um subconjunto dos shaders no commit
fixado: **uma comparação integral de revisões/hashes para cada variante ainda não foi concluída**.

## Submódulos Git e dependências em tempo de execução

SDL3, SDL3_ttf e Dear ImGui são rastreados como **submódulos Git**, sem relicenciamento
pelo Kotonoha. Eles são resolvidos via `.gitmodules` e possuem seus próprios avisos de
copyright e licença. FFmpeg, libass, CPython e dependências declaradas em `vcpkg.json`
ou providas pelo sistema operacional mantêm igualmente seus termos próprios.

Em toda distribuição binária, inspecione as **versões exatas e funcionalidades compiladas**
efetivamente entregues. Em especial, o licenciamento do FFmpeg depende dos codecs
configurados e de opções GPL/nonfree. Inclua todas as licenças aplicáveis, avisos de
copyright e eventuais ofertas de código-fonte exigidas pelas licenças de terceiros. Os
avisos incluídos nos pacotes ZIP do Kotonoha constituem uma **linha de base**, não prova
de auditoria completa de todas as bibliotecas dinâmicas empacotadas.

## Referências que exigem cautela adicional de licenciamento

Algumas implementações de motores originais servem apenas como **referência**, não como
dependências incorporadas. Os metadados do GitHub identificam
[rlvm](https://github.com/eglaysher/rlvm) como **GPL-3.0** e
[ponscripter-fork](https://github.com/sekaiproject/ponscripter-fork) como **GPL-2.0**.
**Não** copie arquivos de implementação desses projetos para módulos MIT do Kotonoha sem
uma decisão compatível de licenciamento e conformidade. O subconjunto `libreallive`
identificado separadamente traz seus próprios avisos MIT; a licença GPL do repositório
rlvm completo permanece válida para o restante.

[NitroSharp](https://github.com/CommitteeOfZero/nitrosharp) e
[LuckSystem](https://github.com/wetor/LuckSystem) são identificados como upstreams MIT;
mantenha seus avisos caso algum trecho seja copiado.
[Kirikiroid2Yuri](https://github.com/YuriSizuku/Kirikiroid2Yuri) apresenta licenciamento
misto/não classificado nos metadados do GitHub: verifique a licença de **cada arquivo e
dependência** antes de copiar qualquer código. A simples presença de um link externo não
configura violação de licença nem autoriza remover atribuições obrigatórias. A procedência
em nível de código-fonte de todas as implementações do Kotonoha **não foi integralmente
estabelecida** por esta auditoria.

## Material que requer confirmação de procedência

- O splash universal e os ícones de plataforma são gerados em `design/generated/` por
  `tools/generate_brand_assets.py` a partir de `design/tokens.json` (os antigos binários
  genéricos `assets/icon.*` foram removidos). `assets/SPLASH.PNG` e `assets/SchoolDays.ico`
  são mantidos estritamente como overrides visuais autorizados pelo proprietário para o port
  School Days e não são aplicados aos demais ports do Kotonoha.
- `lib/tests/fixtures/SvgTestFont.ttf`: fonte exclusiva de teste; origem e permissão de
  redistribuição ainda precisam ser registradas. Não inclua em pacotes destinados a jogadores.
- Fixtures sintéticas de testes de jogos devem permanecer rastreáveis aos seus geradores;
  capturas de tela, sons, scripts ou conteúdos de pacotes comerciais não devem ser comitados
  como fixtures de teste sem autorização.

Esses itens **não são comprovadamente infratores**; são sinalizados como pendentes de
verificação em vez de apagados ou descritos indevidamente como de licença aberta.

## Dados comerciais de jogos, títulos e referências externas

Nomes de jogos, logotipos, scripts, mídias e demais assets comerciais pertencem aos seus
respectivos titulares. Conteúdos fornecidos localmente em `games/<slug>/game-assets/` não
devem ser redistribuídos pelo CI nem pelo empacotamento padrão de desktop. Links para
implementações de código aberto de terceiros têm finalidade exclusiva de atribuição e
referência; não concedem permissão para reutilizar assets proprietários nem para suprimir
avisos obrigatórios.

Consulte o guia bilíngue de auditoria em [docs/LICENSING.md](docs/LICENSING.md) e
[docs/LICENSING.pt-BR.md](docs/LICENSING.pt-BR.md).
