# Android School Days — Etapa 7, testes de finais L×H apenas DEV

## Origem dos fixtures (não versionada)

`C:\Users\Suzu\Downloads\traduções\lxh\Save - LxH\Save`

Arquivos encontrados, analisados **somente em leitura**: sete `SaveFile000.DAT` a
`SaveFile006.DAT` mais `GlobalFlag.DAT` (8 arquivos). O codec `SLog` e
`FlgH` foi lido integralmente; os campos de inteiros são codificados
explicitamente em little-endian e o texto em UTF-16LE, sem dependência de
layout de `struct` do Windows. Nenhum slot possui sidecar
`.kotonoha-lxh.v1`: não inventar, converter ou re-assinar procedência.

## Slot → final → teste no celular

| Slot | Cena original gravada | Teste esperado após **Title → Load → Slot** | Estado |
|---|---|---|---|
| 000 | 05/05-A2-Z00 | **Behind the Scenes** — final HQ e epílogo L×H, vídeo e card | Arquivo DEV incluído; execução não testada |
| 001 | 05/05-SB-E01 | **Light** — final HQ, epílogo, retorno à Title | Arquivo DEV incluído; execução não testada |
| 002 | 05/05-B0-F00 | **Cold Scarf** — final HQ, epílogo, retorno à Title | Arquivo DEV incluído; execução não testada |
| 003 | 05/05-A2-E00 | **Blue Smile** — o save é **pré-final**: KTRF confirmou transição `ktrf:next` incondicional à 05/05-A2-F00 (sem escolha intermediária), então continuar até o final | Arquivo DEV incluído; execução não testada |
| 004 | 05/05-SC-L03 | **Innocent Sunset** — final HQ e epílogo | Arquivo DEV incluído; execução não testada |
| 005 | 05/05-G0-L03 | **Braided Hair** — final HQ e epílogo | Arquivo DEV incluído; execução não testada |
| 006 | 05/05-KC-F00 | **The End of Madness** — final HQ e epílogo | Arquivo DEV incluído; execução não testada |

`GlobalFlag.DAT` original contém metadados `SaveFile00N_Sub` para todos
os sete slots. O slot 003 tem referência explícita a `Sonrisa Azul`/Blue Smile.
Não há `Config.DAT` na fonte fornecida; nenhum `Config.DAT` foi criado ou
inventado durante o empacotamento. A runtime utilizará configuração própria.

## Procedimento de build e isolamento

Apenas o build `schoolDaysDev` aceita os saves, usando o argumento externo:

```powershell
-PschoolDaysDevSavesDir='C:\Users\Suzu\Downloads\traduções\lxh\Save - LxH\Save'
```

Ausência, arquivo vazio ou SHA-256 diferente dos **oito hashes fixos e auditados**
abortam a build DEV; não basta renomear arquivos diferentes. Na montagem do APK, apenas esses
oito nomes são permitidos em `assets/dev-save-fixtures/Save/`, com o manifesto
`assets/dev-save-fixtures-manifest.sha256` que inclui nome, SHA-256 e tamanho.
O código responsável está **somente** em
`Android/app/src/schoolDaysDev/java/.../SchoolDaysDevSaveFixtures.java`,
fora de `src/main` e fora da distribuição.

Na primeira abertura da DEV, `BootstrapActivity` chama a classe por reflexão
apenas na edição DEV. A classe recusa execução com pacote não-DEV, valida
manifesto/hashes e copia para
`<getExternalFilesDir(null)>/Kotonoha/me.hirameki.kotonoha.schooldays.dev/Save`
(fallback interno se indisponível). Antes de importar, verifica se o perfil
`Save` está **inteiramente virgem**; um único save preexistente aborta o lote
sem mistura com o GlobalFlag de QA. A importação registra um marcador
`pending` com hash do conjunto para retomada segura após queda do processo:
apenas itens já verificados podem ser retomados; slots divergentes/parciais
causam erro e nunca são sobrescritos. Cada cópia usa diretório temporário e
abertura `CREATE_NEW`; o `GlobalFlag.DAT` é publicado por último. Quando
os oito arquivos estão íntegros, grava marcador `ready`. Em aberturas futuras,
o marcador impede importação repetida **mesmo que o usuário tenha excluído
um slot intencionalmente**. Falha de escrita da própria tentativa limpa apenas
o arquivo incompleto criado na tentativa atual. Nenhum save real ou arquivo
do PC é sobrescrito. O resumo mostra `copied`, `verified` e o resultado
da importação no launcher e log DEV.

## Verificação de APKs

Compilação Gradle DEV e DIST ARM64: **BUILD SUCCESSFUL**. Inspeção dos ZIPs:

| Item | DEV | DIST |
|---|---:|---:|
| SaveFiles de teste + GlobalFlag | 8 | 0 |
| SHA256 idênticos à fonte | 8/8 | N/A |
| Manifesto de saves DEV | Sim | Não |
| Classe Java `SchoolDaysDevSaveFixtures` no DEX | Sim | Não |
| Traduções GPK/JSON | 14 | 14 |
| Packs GPK originais | 0 | 0 |
| Bibliotecas ARM64 | 6 | 6 |

**Não testado em aparelho/emulador**: a instalação DEV, os arquivos efetivamente
copiados ou pulados, o desbloqueio dos finais, vídeos L×H e a navegação via
Load. Não atribuir estado `copiado` em dispositivo antes de evidência de runtime.

**Regra de segurança:** a versão de distribuição não contém saves de teste,
nem classe de fixtures, nem ferramenta de depuração. Os oito arquivos de
origem nunca foram editados. A DEV e a DIST têm IDs e armazenamento separados.
