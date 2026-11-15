# School Days HQ Android — Etapa 8: verificação da build e auditoria final

Data: 2026-11-10. Projeto: `D:\Dev\Kotonoha-v3`.
Alvo: `schoolDaysDev` e `schoolDaysDist` em `arm64-v8a`.
Traduções e saves de teste provenientes de diretórios do usuário **fora do Git**.
Não foram executados o EXE retail nem o instalador Windows. Saves existentes,
29 GPKs originais e traduções PT-BR do PC ficaram intactos.

## Entregáveis locais (não versionados)

| Variante | APK no build local | Bytes | SHA-256 |
|---|---|---:|---|
| DEV, assinada apenas com chave Android Debug | `Android/app/build/outputs/apk/schoolDays/dev/app-schoolDays-dev.apk` | 42.905.229 | `A893F10BDA83263E436B79C10DC2B817E5E49EDB4D00FA68910357BA1BDF7E70` |
| Comercial, **unsigned** | `Android/app/build/outputs/apk/schoolDays/dist/app-schoolDays-dist-unsigned.apk` | 24.273.379 | `A1466486198B4BCAB98C3E39CBEA3D4ACEEADE3943F4B9255C6A4566EC3C4F7C` |

**Não publicar ou compartilhar o APK unsigned como produto instalável**: deve
receber uma assinatura release de quem detém a chave. Não foi introduzido
keystore de distribuição ou assinatura de depuração na variante comercial.

## Resultado dos testes que puderam ser executados

| Verificação | DEV | DIST |
|---|---|---|
| Gradle Java + CMake native ARM64 | BUILD SUCCESSFUL | BUILD SUCCESSFUL |
| Identificador de pacote | `me.hirameki.kotonoha.schooldays.dev` | `me.hirameki.kotonoha.schooldays` |
| 14 arquivos GPK/JSON com SHA256 comparado aos arquivos-fonte | 14/14 | 14/14 |
| 7 slots + GlobalFlag com SHA256 comparado à fonte | 8/8 | **0 presentes** |
| Manifesto de saves de QA / classe Java de importação DEV | Presente / Presente | **Ausente / Ausente** |
| Packs GPK retail no APK | 0 | 0 |
| Bibliotecas nativas `arm64-v8a` | 6 | 6 |
| `zipalign -c -P 16 -v 4` | Verification successful | Verification successful |
| `apksigner verify` | Assinatura Debug v2 válida | Sem assinatura; falha esperada |
| `AndroidManifest.xml` | Paisagem sensor, depuração habilitada | Paisagem sensor, `debuggable` ausente, backup desativado |
| `lintVitalSchoolDaysDist` | N/A | Concluído na build |
| ADB / reprodução real | Sem aparelho listado | Sem aparelho listado |

As bibliotecas comerciais usam segmentos ELF `LOAD` com alinhamento
`0x4000` (16 KiB). A inspeção de strings nativas comerciais não encontrou
`DevBridge`, `MCPServer`, `dev-startup.log`, `KOTONOHA_DEV_CONTROL` ou
as opções CLI `--node` e `--dev-control` como interfaces disponíveis.
Persistem identificadores internos de depuração em tipos/RTTI do motor, mas
**não** uma ponte ou UI de desenvolvimento no aplicativo comercial.

## Correções após auditoria cruzada (Etapa 8)

1. O filtro legado que ignorava `MOUSE_BUTTON_DOWN` com
   `SDL_TOUCH_MOUSEID` na Display Options do jogo foi retirado; esse
   evento agora é produzido apenas pelo adaptador Android central.
2. O botão genérico anteriormente abria `InGameMenu` sem cena, desenho ou
   tratamento no School Days. O jogo agora **desenha um botão MENU próprio**
   no canto inferior esquerdo e encaminha o toque à Config nativa, mantendo
   a barra de reprodução retail intacta.
3. Para os sliders do `SettingsMenu`, a entrada Android passa a enviar
   press/motion/release reais; um arrasto termina fora dos hotspots sem
   click-through ou dupla ativação. O motor Windows mantém seu input original.
4. A importação DEV agora verifica hashes SHA-256 fixos do conjunto auditado
   **antes da build**, exige perfil inicialmente vazio, valida todos os
   payloads antes de publicar, escreve GlobalFlag por último e usa
   marcadores `pending`/`ready` para recuperar uma instalação interrompida
   sem sobrescrever dados. Depois da importação concluída, uma nova abertura
   não restaura saves que o usuário excluiu.
5. O bloqueio de toque durante a transição de Load foi restrito
   explicitamente ao Android; o caminho de entrada Windows anterior ficou
   intacto. DEV e DIST foram recompilados após essa última correção.
5. A roda sintética Android foi alinhada ao sentido natural do arrasto:
   dedo para cima avança conteúdo/páginas. Movimentos longos emitem passos
   proporcionais; fades, carregamentos e transições bloqueiam paginação
   antes de o modelo ser alterado.
6. Uma falha na segunda gravação do painel Display Options é relatada como
   **persistência parcial**, nunca como falha total que esconderia um
   `settings.json` já gravado.

## Limitações verificáveis e próximo teste físico

O comando `adb devices` retornou lista vazia no momento da QA. Portanto não
há prova de que o runtime importe ou abra os sete saves no aparelho, que
Config/Display/Replay/BackLog aceitem os gestos, nem de que os vídeos L×H,
áudio e temporizadores funcionem em um telefone real. A conclusão deste
relatório significa **build e auditoria estática/ZIP passaram**, e não
certificação funcional no celular.

Ao conectar um telefone ARM64 e autorizar a depuração USB, instalar o APK DEV
no seu próprio package-id, abrir o launcher, validar cópia dos oito fixtures,
selecionar os Packs originais via SAF, abrir cada slot e observar EndRoll →
L×H → card → SaveFeel → Title, inclusive o slot 003 que avança
automaticamente de 05-A2-E00 para Blue Smile. Verificar primeiro toque em
Title, escolhas temporizadas, barra, Config General/Sound/Display, Settings
sliders, Save/Load, Replay, Route Map, BackLog, Back físico e gestos, com
4:3/16:9 e mudanças de frequência. Reabrir DEV e confirmar que arquivos
apagados ou alterados não são recriados; confirmar isolamento ao instalar uma
distribuição **assinada com chave release**.

Não fazer push de artefatos APK, saves, traduções ou packs; somente código,
testes documentais e scripts. O diff preexistente não relacionado em
`games/school-days/src/SchoolDaysLauncher.cpp` não deve ser incluído
em commit destas etapas.
Os APKs foram compilados a partir da pasta de trabalho atual, que contém
esse diff pré-existente **ainda não commitado** (guardas da launcher Android).
Consequentemente uma recompilação de um checkout exclusivamente dos commits
publicados não é garantia de SHA-256 binário idêntico; o diff foi preservado
sem edição por esta tarefa.
