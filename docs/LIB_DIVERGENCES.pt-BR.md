# Decisões de paridade da biblioteca compartilhada — 2026-11-15

[English (US)](LIB_DIVERGENCES.md) | **Português (Brasil)**

Escopo: sincronização Windows L4 com upstream/dev `fdd0b041`. Os seis
arquivos abaixo divergiam no marco `661312c`. O diff original, buscas
`git grep` em cada arquivo e propostas de restauração estão fora do Git,
em `build/_sync-divergencias-lib.md`.

As seis diferenças **ALTERAVAM COMPORTAMENTO**, não eram aditivas: a versão
local removia interfaces públicas ou alterava resultados, custo de CPU e
renderização. Nenhum requisito exclusivo de School Days justificava mudar
a biblioteca compartilhada. Em vez de criar uma variante específica,
cada arquivo foi restaurado **byte a byte ao upstream/dev**, em um commit
local independente.

| Arquivo | Classe original e motivo de estar em `lib/` | Usuários, inclusive outros ports | Verificação existente | Ação e risco restante |
| --- | --- | --- | --- | --- |
| `lib/include/Kotonoha/components/SubtitleScene.hpp` | **ALTERA COMPORTAMENTO**: remove dois acessores públicos de cues; API compartilhada de legendas | Amagami e Snow referenciam `SubtitleScene`; School Days o estende. `git grep` não encontrou uso direto dos acessores removidos em `games/` ou `modules/` | `Kotonoha.Presentation` exercita legendas, **não testa diretamente os acessores** | Upstream restaurado em `a354496`. Contrato de compilação público recuperado; ainda falta asserção dedicada |
| `lib/src/components/SubtitleScene.cpp` | **ALTERA COMPORTAMENTO**: substitui cache/pool ASS de texturas, remove invalidação, registro de fonte e parte da lógica de apresentação; compositor genérico | Amagami, Snow e School Days | Testes de legenda/rasterização em `Kotonoha.Presentation`; **sem benchmark de cache**, sem QA com GPU real | Upstream restaurado em `9ba0dc4`. Política compartilhada recuperada; mídia real e GPU continuam não verificadas |
| `lib/src/components/VNBacklog.cpp` | **ALTERA COMPORTAMENTO**: remove culling fora do viewport e modifica cálculo vertical; backlog genérico | Example, Amagami, Kirikiri, Luca System e PMAIL (alguns têm overrides) | `Kotonoha.Presentation`, `Kotonoha.ComponentHooks`, testes Example; **sem benchmark de histórico longo** | Upstream restaurado em `0588a9f`. Culling e espaçamento medido recuperados |
| `lib/src/core/AssetOverlay.cpp` | **ALTERA COMPORTAMENTO**: remove fallback de diretório absoluto quando a descoberta falha; infraestrutura genérica | Provider RPA do DDLC e overlay GPK do School Days | `Kotonoha.Directories`, `Kotonoha.AssetProvider`, testes sintéticos School Days; **sem teste dedicado de volume de rede** | Upstream restaurado em `563d648`. Fallback de caminho absoluto recuperado sem mudar assinatura |
| `lib/include/Kotonoha/localization/Translation.hpp` | **ALTERA COMPORTAMENTO**: remove overload público `text(string section, string original)`; localização genérica | Amagami, DDLC, Example, Kirikiri, PMAIL, RealLive, Snow, Summer e Zero usam `text(int route,...)`; nenhuma chamada à sobrecarga por string foi encontrada | Teste de tradução Example cobre rota inteira; **sem asserção da sobrecarga por seção** | Upstream restaurado em `8b50862`. Ambas as sobrecargas disponíveis; reduz risco de quebra para usuários externos |
| `lib/src/localization/Translation.cpp` | **ALTERA COMPORTAMENTO**: remove lookup por seção, fallback `nodo` e coleta de originais; implementação genérica | Mesmos ports da rota inteira; nenhum chamador da API por seção na árvore atual | Testes por rota inteira; **fallback por seção não testado separadamente** | Upstream restaurado em `6428e41`. Ordem original da busca recuperada; teste dedicado recomendado antes de futuras alterações |

Não é necessária opção nova nem override do School Days: o comportamento
desejado nesses seis arquivos genéricos **é o padrão upstream**. Uma opção
criaria divergência desnecessária e comportamento especial sem requisito.

## Outros ports e limites restantes

A matriz CMake fica em `buildsystems/cmake/KotonohaGames.cmake`; cada port
é selecionado com `KOTONOHA_BUILD_<NAME>=ON`. A compilação dos ports
não School Days no Windows Release está registrada no log local e em
`build/_sync-lib-otherports-*.log`. Compilação prova compatibilidade
de símbolos/assinaturas, não gameplay, qualidade visual, saves, codecs ou
paridade de outras plataformas.

O `StoragePaths.cpp` upstream permanece inalterado. A implementação
Desktop usa namespace por aplicativo, mas o teste upstream
`Kotonoha.Directories` espera diretamente a raiz de dados do usuário.
Manter o teste local coerente com o namespace da implementação enquanto
o upstream não conciliar ambos. A exceção Windows L4 de `settings.json`
pertence a `games/school-days/src/main.cpp`, não à API de storage global.

### Compatibilidade adicional encontrada ao compilar outros ports

A compilação dos dez outros ports Windows mostrou mais um contrato
removido em `lib/include/Kotonoha/core/Runtime.hpp`: Kirikiri,
Luca System e PMAIL ainda sobrescrevem `dialogueText() const` e
`dialogueSpeaker() const`, mas a classe-base sincronizada já não os
declarava (MSVC C3668). Esse item está fora dos seis arquivos acima.
O commit `dcc90ac` restaura ambos os virtuais com os retornos vazios
do upstream, preservando as extensões específicas do School Days.
É correção **ADITIVA**, sem alterar o comportamento padrão.
Os logs da compilação Windows dos outros ports registram a falha inicial
e o reteste.
