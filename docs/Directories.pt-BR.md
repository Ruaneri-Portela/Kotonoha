# Diretorios e referencias de recursos

O motor separa tres responsabilidades: **assets da engine**, **assets do jogo**
e **saves/configuracoes**. A politica fica em `core/FileSystem`, por meio de
`StorageLocations`, `DirectoryLayout` e `FileResolver`. Os jogos nao devem chamar
`SDL_GetPrefPath` diretamente nem montar caminhos de armazenamento por conta propria.

## Layout por plataforma

`<pacote>` e o identificador da aplicacao, por exemplo
`me.hirameki.kotonoha.amagami`, nunca o titulo ou o slug `amagami`.
No Android, o identificador vem de `getPackageName()`.

| Plataforma | Assets da engine | Assets do jogo | Saves e configuracoes |
| --- | --- | --- | --- |
| Android | `<dataDir>/files/assets` | `<dataDir>/game_assets`, ou `Android/obb/<pacote>` | `Android/data/<pacote>/files/Kotonoha/<pacote>` |
| iOS | `<bundle>/assets`; alternativa `Documents/assets` | `<bundle>/game-assets`; alternativa `Documents/game_assets`, `Documents/game-assets` ou dados diretamente em Documents | `Documents/Kotonoha/<pacote>` |
| macOS | `<app>.app/Contents/Resources/assets` | `<app>.app/Contents/Resources/game-assets` | `~/Library/Application Support/Kotonoha/<pacote>` |
| Windows | `<executavel>/assets` | `<executavel>/game-assets` | `%APPDATA%/Kotonoha/<pacote>` |
| Linux | `<executavel>/assets` | `<executavel>/game-assets` | `$XDG_DATA_HOME/Kotonoha/<pacote>`, normalmente `~/.local/share/Kotonoha/<pacote>` |

`dataDir` e fornecido pelo Android: `/data/user/0/<pacote>` (equivalente a
`/data/data/<pacote>`). Nao ha caminhos Android fixos no codigo nativo.
O bootstrap extrai os recursos do APK antes de iniciar SDL, mantendo esses
enderecos estaveis. O fingerprint determina quando atualizar os arquivos empacotados.

Os nomes `game_assets` e `game-assets` sao aceitos na busca. O APK e os bundles
existentes continuam contendo `game-assets`. No Android, OBB tem prioridade e
pode conter os dados diretamente ou nessas subpastas. No iOS, o bundle tem
prioridade, com fallback por arquivo para Documents, acessivel pelo app Files.

Em execucoes desktop fora de um bundle macOS, o diretorio de trabalho tambem
pode fornecer `assets`, `game-assets`/`game_assets` e
`games/<slug>/game-assets`. O slug serve apenas para localizar fontes/dados no
repositorio; ele nao determina o destino de saves.

## Launcher e overlay

O mesmo resolvedor procura selecoes de jogo, discos, scripts e overlays nos
locais aplicaveis da plataforma: recursos, diretorios de assets, dados privados,
Files/Documents, dados externos Android e OBB. Nao faz varredura de todo o disco.
Os candidatos sao testados em ordem; o launcher continua procurando quando um
candidato nao passa na validacao especifica do jogo.

* Campo overlay vazio: descobre a pasta `overlay` automaticamente.
* Campo preenchido com `overlay2`: procura essa pasta nos locais conhecidos.
* Uma selecao explicita tem prioridade sobre `KOTONOHA_OVERLAY` e sobre o nome padrao.
* `--no-asset-overlay` desabilita o overlay para a execucao atual.

Por exemplo, `overlay2` pode mudar de `Android/obb/<pacote>/overlay2` para
`Android/data/<pacote>/files/overlay2`, ou para `<dataDir>/files/overlay2`, sem
reescrever o arquivo de preferencias. No iOS, `Documents/overlay` e descoberto
automaticamente; no macOS, `Resources/overlay` tambem.

## Persistencia portavel

O launcher guarda **somente a referencia (nome)**, como `Amagami.iso`, `Packs`,
`route.ktnroute` ou `overlay2`. Traducoes, fontes e listas ordenadas de shaders
tambem sao serializadas por nome. Caminhos absolutos selecionados no navegador
sao aceitos durante a execucao; ao salvar, a parte especifica do dispositivo e removida.
Para reencontrar esses recursos em outra execucao/dispositivo, coloque-os em um
dos locais de busca acima. Prefira nomes distintos quando houver varios recursos.

Arquivos antigos contendo caminhos absolutos continuam sendo tentados; se o
caminho nao existir, o resolvedor procura pelo nome nos locais atuais.
Os arquivos de save propriamente ditos continuam no diretorio de saves, com
as subpastas exigidas pelo formato de cada jogo. Overrides explicitos de save
usados por ferramentas/testes continuam sendo respeitados.

## Fontes e bibliotecas de runtime

O catalogo de fontes aceita `.ttf`, `.otf` e `.ttc` com assinatura de fonte.
O launcher e a libass usam esse mesmo catalogo; licencas como `OFL.txt` nao
sao enviadas ao carregador de fontes.

No Windows, o CMake copia as DLLs identificadas nas dependencias do executavel,
incluindo dependencias transitivas. Assim, Python acompanha apenas o DDLC,
que liga `Python3::Python`, e nao todos os jogos por estar instalado no vcpkg.

Saves gravados anteriormente fora desse layout podem ser copiados para o novo
diretorio de seu pacote. Em particular, o quick-save antigo do Amagami fica em
`%APPDATA%/Kotonoha/amagami/quick-save.txt`; o novo destino e
`%APPDATA%/Kotonoha/me.hirameki.kotonoha.amagami/quick-save.txt`.
