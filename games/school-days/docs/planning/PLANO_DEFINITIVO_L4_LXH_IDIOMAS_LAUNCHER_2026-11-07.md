# School Days HQ / Kotonoha v3 L4 — plano de integração L×H, idiomas e launcher

## DECISÃO FINAL DE ENTREGA COMUNITÁRIA — 07/11/2026

Após o novo `LxH.GPK` obrigatório, o usuário decidiu **não realizar mais
QA físico interno**. A distribuição L4 será testada pela comunidade, que
reportará bugs; manter a matriz 0/14 finais físicos **não certificados**
como transparência, mas não inventar PASS nem reabrir testes de GUI.
**Entrega executada:** a Release L4 foi recompilada em build isolada com
engine/ImGui/FFmpeg e dependências estáticas, e os binários publicados
possuem **somente duas DLLs locais**, `SDL3.dll` e `SDL3_ttf.dll`.
`SchoolDays.exe` SHA256
`31e8cf33e7f6215ed71dfd7e72b00f0e0104462afc0c30c1ae05328545fa8316`.
`ALL_BUILD Release PASS`, **67/67 CTest PASS (71,24 s)**,
probe C++ do `LxH.GPK` real PASS, smoke EN e ES na Release publicada
exit0; sem QA físico de finais por escolha expressa do usuário.
Antigo EXE + 29 DLLs em
`build/school-days-dev-control4/backup-old-runtime-dlls-20261107`.
Os 30 GPKs, o inglês inicial, as artes `System/Option` e os sete saves
originais autênticos com `GlobalFlag.DAT` foram mantidos byte-idênticos
(backup SHA256 em `backup-lxh-seven-saves-20261107`).

O diretório `display-options/Script/English` era apenas um vestígio vazio
do overlay antigo: **não havia ORS nele**, a montagem empacotada obtém os
sete ORS de `Packs/LxH.GPK`. Foi retirado com manifesto antigo de loose;
backup privado preservado. Configuração de idioma mudou para inglês
(`settings.json` sem tradução, `language.txt=english`); ES pode ser
selecionado posteriormente no launcher existente. Instruções e tabela
dos slots: `Release/LEIA-ME-L4-LXH-SAVES.txt`.

## DECISÃO NOVA DO USUÁRIO — 07/11/2026, PREVALECE SOBRE ON/OFF ABAIXO

**Mudança expressa de produto**: School Days L×H deixa de ser DLC
selecionável. Os sete epílogos são conteúdo **permanente e integrado** ao
School Days HQ v1.02 para Windows, reproduzidos automaticamente após seus
sete finais autorais. Não haverá preferência/alternância L×H ON/OFF no
launcher nem nos modos normais de jogo. As passagens antigas que exigem
verificar ON/OFF, aviso de DLC desativada ou matriz 28 combinações são
**histórico de QA anterior**, não requisitos atuais a executar novamente.

**Novo pacote**: criar `Packs/LxH.GPK`, distribuído como **30º arquivo
adicional**, sem alterar um byte dos **29 GPKs comerciais originais**.
Inventário de entrada: **28 arquivos** do overlay L4 autenticado — sete
`Script/English/05/<ending>.ENG.ORS` estendidos, sete
`LxH/<ending>-LXH.png` ingleses universais 800×452, sete
`LxH/Media/<ending>-LXH.wmv`, sete
`LxH/Media/<ending>-LXH.ogg`. Guardar tabela de caminhos e SHA256
por entrada, verificar round-trip byte-idêntico, streaming/range/seek
real para WMV/OGG, ausência de entradas ambíguas e hashes intactos
dos originais. **Não substituir Script.GPK**, nem manter dependência
de assets L×H soltos em `display-options` após migração validada.

**Bloqueio arquitetural confirmado**: `SchoolDaysRetailGpkSpecs()` e
`SchoolDaysGpkAssetStore::OpenRetailDirectory()` conhecem rigidamente
apenas os 29 arquivos comerciais; copiar `LxH.GPK` para `Packs` sozinho
não o monta. Implementar montagem game-specific de GPK adicional, sem
misturá-lo à lista autenticada retail. Resolver por caminhos lógicos
existentes, dando prioridade ao pacote novo para os sete ORS específicos
e para `LxH/*`, sem substituir outros arquivos; detectar pacote ausente,
incompleto, adulterado ou colisões e **falhar explicitamente** em vez de
iniciar silenciosamente finais comerciais sem seus epílogos. Preservar
o overlay separado dos desenhos de opções do usuário (System/Option).

**Primeira entrega do novo formato (07/11, histórico do empacotamento):**
`games/school-days/tools/build_lxh_gpk.py` gera o pacote adicional em
modo raw-seekable, com índice `STKFile0PIDX` XOR+zlib e trailer
`STKFile0PACKFILE`. Criado/verificado round-trip 28/28 entradas iguais
ao overlay de origem, sem sobrescrever os packs comerciais, em scratch:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-integrated-lxh-gpk-20261107/LxH.GPK`.
Manifesto com SHA256 por entrada:
`qa-integrated-lxh-gpk-20261107/lxh-gpk-roundtrip-manifest.json`.
Arquivo publicado como **novo**
`D:/Dev/Kotonoha-v3/build/school-days-dev-control4/games/school-days/Release/Packs/LxH.GPK`:
**237.329.938 bytes**, SHA256
`772b501fc70ae89e52861378a99ff1b27d3c47eb20b2473836591069143edb64`.
A pasta tem agora 30 GPKs, 29 originais + 1 novo. O arquivo foi criado
**antes** da adaptação do montador; o status mais recente está abaixo.

**Integração C++ e primeira build (07/11):**

- Criado `SchoolDaysLxhContent.hpp` como catálogo game-specific dos sete
  finais e 28 entradas; retirado o uso de `SchoolDaysLxhPolicy` no fluxo
  principal. `MountSchoolDaysRetailGpks` passa a exigir e validar
  **LxH.GPK** além dos 29 comerciais; falha explicitamente se o pacote
  estiver ausente, malformado ou com 28 recursos incompletos/ambíguos.
- `SchoolDaysAssetMount` faz lookup/read/list/seek prioritariamente do
  pacote integrado para os sete ORS estendidos e 21 mídias. Raw streaming
  de WMV/OGG sem substituir `Script.GPK` ou os outros GPK comerciais.
- Os 28 arquivos soltos do overlay L4 antigo foram **movidos com
  preservação**, após comparar os SHA256 com o manifesto, para scratch:
  `D:/Dev/_temp_lxh_static_audit_worker8/qa-integrated-lxh-gpk-20261107/retired-release-loose-assets/`.
  `System/Option` permanece intacto.
- `main.cpp` não lê/grava mais `LxH.mode`, não oferece `--lxh` e não
  propaga ON/OFF do launcher. EN: `Script.GPK` + `en-lxh.json`; ES:
  `es.json` + `es-lxh.json`, com cobertura contextual.
- **`school_days` e `SchoolDays` Release PASS**, EXE SHA256
  `D05BA329E30AF2F32DA03ABEDB8D7E6044265C251C8047D26C12DC99C8EB6393`.
  Smokes SDL dummy `--frames 5`, **sem `--node`**:
  **EN=exit0, ES=exit0**, inclusive depois de retirar o overlay L×H
  solto. Evidência privada:
  `D:/Dev/_temp_lxh_static_audit_worker8/qa-integrated-lxh-packonly-headless-20261107/`.
  São inicializações curtas, não finais físicos.

**Validação incremental posterior:** `SchoolDaysGpkStreamingTest` passou
com o novo GPK obrigatório, bloqueio de overlay L×H antigo e streaming
seekable (último CTest focalizado **1/1 PASS**). `SchoolDaysRuntimeSmoke`,
`SchoolDaysLauncherTest` e `SchoolDaysPortTest` passaram após adaptar
fixtures extraídas; o primeiro CTest integral da migração era **64/67**,
antes dessas correções. `SchoolDaysLxhPrivateMountProbe.exe` compilado
e executado contra os 30 GPKs reais passou em **7/7 ORS grafted + 21/21
mídias PNG/WMV/OGG**, raw seek, tempos de epílogo e overlay de opções,
sem modificar comerciais. **CTest completo nos binários disponíveis:
67/67 PASS (70,74 s)** após as correções; esse resultado não substitui
um ALL_BUILD com hash final único. `git diff --check` PASS; sem símbolos
ativos de ON/OFF em `src/include/tests`. O `SchoolDays.exe` da Release
estava aberto pelo usuário (PID 17220) durante aquele teste e foi
preservado até sua saída natural.
**Matriz física atual: 0/14 novos finais ponta a ponta certificados**.

**Build final unificada após janela liberada (07/11/2026):**
`ALL_BUILD Release PASS`; novo **CTest 67/67 PASS (73,44 s)**.
SHA256 do `SchoolDays.exe` Kotonoha final:
`3B9602AD38AEA9C7652845E1C670B286BC8D1493E653372FEDF082F8459C29A5`.
SHA256 `Packs/LxH.GPK` inalterado:
`772b501fc70ae89e52861378a99ff1b27d3c47eb20b2473836591069143edb64`.
Headless de inicialização EN e ES, ambos exit0 com SDL dummy/software,
`--frames 5`, `--documents` privados, sem `--node` e sem L×H loose:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-integrated-lxh-final-smoke-20261107-160326/`.
Faltam os **14 finais naturais físicos** e testes AV/Save/Load na GUI;
nenhum teste headless deve ser creditado nessa matriz.

**Contrato de idiomas integrado**:

- Inglês: HQ original lido de `Script.GPK` comercial (sem JSON base);
  falas novas do L×H vindas de `translations/en-lxh.json`.
- Espanhol: HQ original por `translations/es.json` V4b, L×H por
  `translations/es-lxh.json`, preservando a regra de **zero overrides
  conflitantes** sobre as 273 traduções espanholas já completas no V4b.
- Futuro `<locale>`: `translations/<locale>.json` do HQ e
  `translations/<locale>-lxh.json` da extensão, verificação estrita de
  cobertura/contexto, sem fallback silencioso EN. JSONs continuam fora
  do GPK para adicionar idiomas sem reempacotar mídia.
- Sete cartões visuais em inglês permanecem universais nesta versão;
  não reivindicar que texto impresso nesses PNG foi traduzido para ES.

**Compatibilidade de save**: preservar SaveFile/GlobalFlag retail,
sidecars de proveniência já emitidos, schema e hashes; saves históricos
de epílogo `lxh=1` agora abrem com o conteúdo L×H presente, sem aviso
"DLC OFF". Manter checagens de integridade/overwrite e recuperar saves
normais/legacy. Retirar a condição de bloqueio de DLC desativada somente
depois de comprovar que o pack é obrigatório e montado.

**Recontagem honesta do QA**: a matriz NOVA do produto é **EN/ES × 7
finais integrados = 14 cenários**, mais Save/Load/GlobalFlag/Replay,
PlayData/BackLog/Options/AV, idioma e perfis. Antes da nova arquitetura
há **cinco** percursos ON ponta a ponta (ENDG5 EN, ENDG6 EN/ES,
ENDG7 ES, ENDG8 EN); quatro SDL/timeout, ENDG5 EN usou `choose`.
Os **três** percursos OFF anteriores continuam evidência histórica
útil do motor retail, mas **não contam** nos 14 cenários novos. E os
cinco ON anteriores são apenas baseline de comportamento: **0/14
cenários foram revalidados carregando o novo `LxH.GPK`**, que ainda
precisa ser construído/integrado. Retestar todos no pacote definitivo.
O QA físico genuíno de PlayData com save no mesmo perfil/retorno ao
título é reaproveitado; testes de aviso ON→OFF tornam-se obsoletos.

**Sequência atualizada**: Etapas 1–4 preservadas; Etapa 5 começa por
criação e montagem do pack integrado, remoção da alternância L×H,
retestes unitários, depois matriz natural 14/14 e demais gates de
Windows. Etapa 6 moderniza o **launcher existente** com descoberta
dinâmica dos pares JSON e sem chave L×H; Etapa 7 RC Windows. Android
continua fora do escopo, e não fazer commit/push.

---

## Decisão de produto

Versão de trabalho exclusiva: `D:/Dev/Kotonoha-v3/build/school-days-dev-control4`
(Windows). Primeiro concluir e estabilizar Windows; Android só depois.

Inicialmente, **somente inglês e espanhol** serão oferecidos no launcher.
Outros idiomas poderão ser adicionados via JSON futuramente sem modificar os
29 GPKs comerciais. O PT-BR em desenvolvimento continua **privado**.
Os **assets visuais do L×H serão universais em inglês**, independentemente
do idioma selecionado. Traduzir texto de narrativa e escolhas por JSON.
O overlay L×H continua opcional, após créditos dos sete finais originais,
sem criar finais novos.

## Estado comprovado antes desta etapa

- O launcher dedicado de School Days HQ **já existe** na L4. Tem abas
  genéricas, seleção English (original)/Spanish (JSON)/Custom, preferência
  L×H Auto/On/Off e persistência. **Ainda não descobre automaticamente todos
  os idiomas da pasta translations**.
- O runtime tem lookup JSON NODE e `@ors:<eventOrdinal>:<slot>` para casos
  contextuais, com speaker original para cores ASS, escolhas e BackLog.
  A versão espanhola V4b foi validada com 30.968 ocorrências base e 31.241
  com L×H; os testes nativos de cobertura têm zero entradas ausentes.
- Atualmente há **um único caminho JSON ativo** no motor. O L×H herda esse
  dicionário, mas **não escolhe independentemente uma tradução da DLC**.
  O espanhol contém as traduções HQ/L×H; o HQ inglês comercial é nativo,
  mas **as 273 novas falas do L×H continuam no espanhol-fonte** quando
  o jogador seleciona inglês sem JSON.
- A mídia L×H está em `<Release>/display-options/LxH`: 7 PNG, 7 WMV,
  7 OGG e mais 7 ORS no overlay. A origem do usuário é
  `D:/SCHOOL DAYS HQ/Overflow/SCHOOLDAYS HQ/LxH`, com 7 PNG em inglês de
  **800×452**; os 7 PNG correspondentes no overlay têm a mesma resolução
  mas hashes diferentes e contêm a arte espanhola. Os **14 WMV/OGG já
  correspondem byte a byte** entre a pasta de origem e o overlay L4.
- O manifesto privado `.school-days-lxh-local-manifest.json` lista
  28 recursos e seus SHA256. **Está consistente com o overlay atual**;
  portanto a troca dos 7 PNG exige atualizar as 7 entradas no manifesto.
  `SchoolDaysLxhPolicy::OverlayComplete` hoje verifica presença/tamanho
  dos 28 recursos, não o texto nem o checksum do manifesto.

## Etapa 1 — Assets ingleses universais do L×H

1. Registrar SHA256 de origem/destino e guardar backup **somente no
   scratch privado**, sem publicar imagens comerciais/traduzidas.
2. Validar visualmente os sete PNG de origem, título inglês, transparência,
   integridade e resolução 800×452. Nenhuma imagem será redimensionada.
3. Copiar **somente os sete PNG** para
   `<Release>/display-options/LxH/<cena>-LXH.png`, preservando exatamente os
   nomes. Não mexer nos WMV/OGG, sete ORS ou em GPK.
4. Atualizar/verificar os sete SHA256 no manifesto privado e provar que os
   outros 21 recursos permanecem byte-idênticos. Testar cada imagem no jogo,
   antes do countdown, com L×H ON; com OFF ela não deve ser carregada.

**Gate:** 7/7 PNG em inglês corretos, 800×452, hash igual à origem;
28/28 arquivos do L×H presentes, ORS/WMV/OGG/GPK intactos.

## Etapa 2 — Tradução inglesa integral dos epílogos

1. Partir do inventário privado das **273 ocorrências `PRINT_TEXT` L×H**,
   com NODE, evento no parser C++, speaker e contexto da cena.
2. Traduzir do espanhol para inglês e revisar **semântica, registro,
   pronomes, nomes, cronologia, pontuação e continuidade** com os finais HQ.
   Não traduzir os identificadores de speaker/cor ASS.
3. Produzir `translations/en-lxh.json`, estritamente com as novas falas
   L×H e overrides `@ors` quando um mesmo texto exigir contextos distintos.
   Identidades legítimas precisam de revisão/documentação.
4. Validar as 273/273 linhas em inglês no parser nativo, no filme,
   na legenda e no BackLog; nenhuma fala espanhola do L×H pode permanecer
   quando English estiver ativo.

**Gate:** inglês HQ continua vindo do Script.GPK, sem JSON do HQ;
L×H ON aplica 273/273 traduções inglesas adicionais; L×H OFF não exige
o JSON da DLC.

## Etapa 3 — Contrato multilíngue HQ + DLC e catálogo futuro

1. Manter compatibilidade com `translations/es.json` V4b e seu
   `es-reviewed-identities.json`. Introduzir **camada específica de DLC**:
   `translations/<idioma>-lxh.json` para conteúdo extra. Exemplo inicial:
   `en-lxh.json` e `es-lxh.json`; o espanhol adicional pode ser identidade
   contextual legitimamente revisada quando a fonte ORS já é espanhola.
2. Definir lookup determinístico **DLC contextual → DLC NODE literal →
   JSON base contextual → JSON base NODE literal → base `nodo`**,
   aplicável somente quando DLC ON e o evento pertence ao epílogo. HQ inglês
   nativo sem DLC continua dispensando JSON. Para locale não nativo,
   lacunas são erro explícito, **jamais fallback para inglês**.
3. A seleção do usuário é um **locale estável** (ex.: `en`, `es`, futuro
   `fr`) e o runtime determina os arquivos HQ/DLC dessa língua.
   Preferências persistem por identificador, não por inferência do nome
   de um GPK. Só aceitar a DLC ON para aquele locale quando sua cobertura
   específica estiver válida.
4. Congelar idioma e modo DLC por sessão. Trocas no launcher devem valer
   para a **próxima inicialização da gameplay**, nunca enxertar/remover ORS
   no meio de uma cena.
5. Criar validador/status por idioma e DLC: disponível, incompleto ou
   ausente; usar o mesmo parser nativo, NODE e manifesto de identidades.

**Gate:** inglês/espanhol × DLC OFF/ON sem idioma trocado, sem legenda/choice
faltante, sem BackLog em língua errada. Adicionar terceiro JSON sintético em
teste prova que a arquitetura **não está hardcoded para dois idiomas**.

## Etapa 4 — Saves, Replay, Options e transições

1. Fixar comportamento dos saves: save de gameplay comum carrega com DLC
   ON ou OFF; save criado **dentro de epílogo L×H** com DLC OFF deve ser
   recusado com orientação clara para ativá-la (nunca travar ou cair em
   roteiro incorreto). Não gravar/modificar saves comerciais originais.
2. Testar novo jogo, Load, Save, SaveFeel/GlobalFlag, Replay Data, BackLog,
   Back/Forward, Skip-to-end/next-choice, Pause, Auto, Restart, retorno ao
   título e abertura de Options com DLC ON/OFF.
3. As configurações de vídeo, resolução, upscale/render scale, áudio, volume
   e layout do Display Options devem refletir-se também durante o L×H.
   Manter cores existentes dos seis personagens estilizados; demais falas
   usam Default, como decidido.
4. Preservar os fades exigidos: após EndRoll original → L×H e L×H →
   countdown; manter a duração de 30s após a revelação e a ordem dos finais.

**Gate:** nenhum save corrompido, sentimentos de um save não persistem no
seguinte, sem vazamento de modo/idioma entre sessões; opções, mídia,
legendas e retorno ao menu mantêm comportamento consistente.

## Etapa 5 — Estresse Windows antes da reforma visual do launcher

- Matriz mínima **2 idiomas × 2 estados da DLC × 7 finais = 28 cenários**
  de chegada ao encerramento real; incluir pelo menos uma rota normal
  sem L×H por idioma e estado.
- Percorrer nos sete finais: último trecho HQ, EndRoll, fade, filme e OGG
  L×H (quando ON), legendas/BackLog, outro fade, PNG final universal,
  countdown e retorno ao menu; quando OFF, verificar encerramento HQ sem
  mídia/roteiro/PNG extra.
- Em **perfis `--documents` isolados**, alternar repetidamente saves
  de final e de gameplay na mesma execução, com DLC ON/OFF e EN/ES; validar
  save normal versus save interno de epílogo. Verificar GlobalFlag/Replay.
- Exercitar mídia 1× real, velocidades aceleradas, pausa/reinício, seek,
  decoder/audio timing, mudança de Options (video/upscale/audio), janela
  Standard/Wide, render D3D11 e demais backends **disponíveis**.
- Automatizar censo de traduções, asserts de imagens/hashes, smoke CLI,
  testes de transição e regressões já existentes; complementar com
  screenshots e inspeção audiovisual real. Não chamar 4 smoke headless de
  certificação audiovisual.

**Gate para avançar à etapa 6:** todos os 28 cenários sem erro, perfis e
saves intactos, sem leaks/travamentos perceptíveis, áudio e vídeo sincronizados,
legendas/choices/BackLog adequados ao idioma, fallback inglês indevido zero,
sem regressão nas opções/fades. Documentar falhas, corrigir e repetir
antes de mudar o design.

## Etapa 6 — Reforma do launcher dedicado, inspirada no Spider-Man Remastered

Somente **depois** de fechar as etapas anteriores. **Não construir outro
launcher**: modernizar `SchoolDays::Launcher` existente, preservando
configuração avançada, Route, Packs/ISO, Display, Audio, Translation e
compatibilidade CLI.

- Adotar composição visual moderna: logo/hero do School Days, navegação
  lateral ou agrupamento equivalente, área grande de opções e resumo de
  estado antes de Play. Inspirar-se na organização/usabilidade do launcher
  Spider-Man Remastered sem copiar assets protegidos de outra franquia.
- **Descoberta dinâmica:** examinar `translations` ao abrir/atualizar e
exibir apenas arquivos JSON de tradução de HQ válidos, excluindo os
  `*-lxh.json`, `*-reviewed-identities.json`, manifestos e templates.
  Associar cada locale à sua DLC JSON, nome amigável, estado de cobertura
  HQ/DLC, origem e versão; idiomas adicionais entram na lista sem alterar
  código/enum. Preservar suporte a JSON manual/custom.
- Exibir DLC como opção separada ON/OFF (Auto avançado, se mantido), com
  disponibilidade por idioma, recursos e aviso em caso de tradução incompleta.
- Persistir idioma pelo ID, DLC, caminhos, tela/áudio/upscaler e opções
  sem reescrever dados comerciais. `--lxh` explícito prevalece.
- Exercitar controle de teclado/mouse, estados inválidos, seleção vazia,
  ausência de packs/JSON/overlay, update de catálogos e UI responsiva.

**Gate:** lista atual EN/ES funciona, terceiro idioma sintético é detectado
sem recompilar, DLC associa o JSON certo, Play é impedido quando há
lacunas e nenhum ajuste de UI regride a gameplay.

## Etapa 7 — Candidato final Windows e depois Android

Consolidar relatório de proveniência e testes, hashes dos 29 GPKs,
imagens/medias/ORS privados e saves. Repetir CTest serial, inspeção
visual do launcher, opções e narrativa. **Somente com Windows fechado**
abrir novo plano de Android, sem presumir paridade automática.

## Restrições permanentes

- Não modificar ou publicar retail GPK/EXE/DLL, ORS privados, WMV/OGG,
  imagens e tradução de terceiros; todo material licenciado fica fora do Git.
- Não tocar saves reais. Backups e testes em scratch/`--documents` isolados.
- Sem commit/push, alterações na instalação comercial ou builds Android
  sem solicitação explícita. Não abrir EXE comercial para testes.
- Engine genérico continua genérico; política School Days e L×H somente
  dentro de `games/school-days` quando possível.

**Ordem vinculante:** assets ingleses → tradução inglesa L×H → composição
de idiomas HQ/DLC → compatibilidade saves/options → estresse Windows →
redesenho e lista automática de idiomas no launcher → validação final.

## Checkpoint operacional da Etapa 5 — 2026-11-07 04:08 BRT

Etapas 1–4 preservadas, **não reabrir**. ENDG5 EN/ON foi validado até o
menu após 36 escolhas, epílogo integral 1× e FlgH EndNo=5; 21/36 era
estado intermediário da campanha, não nova conclusão. ENDG6 EN/ON foi
alcançado duas vezes por **37/37 escolhas físicas**, mas primeiro travou
antes do WMV e no segundo concluiu WMV 1× (quatro screenshots e áudio
WASAPI Samsung/NVIDIA) e travou no CreateBG do cartão final. Nenhuma das
duas tentativas ENDG6 concluiu SaveFeel/countdown/menu/GlobalFlag.

Correção da causa `MediaPlayer::State` que cancela tokens compartilhados:
tokens individualizados por WMV/OGG e intro/loop, fila dedicada a
fundos, watchdog WMV/OGG 12s e fundo 30s contados somente no momento
em que o evento é devido, descarte de resultados atrasados. L4 Release
**ALL_BUILD e 66/66 CTest PASS**;
SHA256 `1A996138129204C15647C33F3B9B4467E67EF110BCB6F524326553FEAB565122`.
Nova rota física ENDG6 está em
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code6-mouse-cancelpool-20261107-0410`.
**Gate Stage5 permanece ABERTO/PENDENTE**; 28 cenários e stress funcional
não certificados. É proibido iniciar Etapa 6 ou declarar RC antes dos
gates físicos. Sem commits, Android, instaladores ou alterações comerciais.

### Atualização operacional 04:19 BRT — ENDG6 EN/ON

O terceiro ENDG6 no mesmo executável L4 corrigido **PASSOU**:
37/37 escolhas por `DevBridge.mouse_move/click` submetidas ao
hit-test SDL, WMV/OGG preparados em 5/2ms sem cancelamento,
reprodução integral 1× com quatro checkpoints, fade, SaveFeel,
countdown, retorno ao título e FlgH validado
(`EndNo=6`, `EndClear=-1`, `[End06]=-1`, SHA256
`b72337455a217646e586885bb7f5981d19a75d3a5ed60b4c411cb737909643e2`).
WASAPI Samsung capturou 165s, áudio digital PCM real, sem comprovar
escuta humana/lipsync; frame etiquetado BG foi capturado antes do
`CreateBG` e **não** deve ser apresentado como prova visual do PNG.
Relatório/evidências em `../qa/L4_STAGE5_QA_PROGRESS_2026-11-07.md`.
Iniciado ENDG7 ES/ON com 52 escolhas naturais e sessão isolada;
**Etapa 5 permanece incompleta** e a Etapa 6 ainda bloqueada.

### Atualização operacional 04:39 BRT — ENDG7 ES/ON concluído

O runner principal encerrou a campanha `ENDG7 ES/ON` em
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code7-es-on-20261107-0421`
com **52/52 escolhas SDL mouse hit-test**, `route-audit.json valid=true`,
WMV/OGG preparados sem cancelamento (5/2ms), filme 1× completo, fades,
PNG inglês **Cold Scarf** comprovado visualmente **após CreateBG**,
SaveFeel/countdown e retorno ao título (runner exit0). `GlobalFlag.DAT`
isolado: `valid=true`, `EndNo=7`, `EndClear=-1`, `[End07]=-1`, SHA256
`aaa0a79ccfbe0c1951de85ff3f5d740a48d18b4e215b13d63f9b700f2fb743d2`.
WASAPI Samsung/NVIDIA: 112s PCM16 estéreo 48kHz, 9.271 pacotes;
atividade digital detectada **sem** certificação de escuta/lipsync.
Observador passivo registrou fade e countdown 5/15/28s, mas sua conexão
DevBridge foi resetada após o encerramento normal do runner: seu status
`complete:false` **não** invalida o retorno ao menu documentado pela
campanha principal e **não** equivale a sucesso independente do observador.
Total **3/28 cenários** ao menu (ENDG5 EN/ON por `choose`; ENDG6 EN/ON e
ENDG7 ES/ON por SDL mouse), sendo apenas **2/28 via SDL hit-test integral**.
Ainda faltam timeouts naturais, DLC OFF, Save→Load A→B→A, Replay/PlayData,
BackLog, Options e demais cenários. **Etapa 5 em andamento; Etapas 6–7
bloqueadas**, sem Android/commit/push.

### Atualização operacional pós-ENDG8 — timeouts KTRF naturais aprovados

Sessão Windows L4 exclusiva:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code8-en-on-20261107-0445`.
**ENDG8 EN/ON ponta a ponta PASS**, `05/05-G0-L03`, 37 decisões
autênticas: **22 cliques SDL mouse** e **15 timeouts naturais** `-1`
aguardados no relógio real, sem comando de escolha/timeout nem `--node`.
EndRoll original e WMV/OGG L×H completos **1×**, fades,
quatro checkpoints de vídeo, PNG inglês **“Braided Hair”** visto
depois do `CreateBG` (tick6975), SaveFeel/countdown e retorno ao
título (`NATURAL_END_TO_END_RESULT ... terminal True menu True movie True`,
exit0). `route-audit.json valid=true`, `22/22` mouse ack,
`15/15` timeouts, KTRF SHA autenticado. `globalflag-audit.json valid=true`,
`EndNo=8`, `EndClear=-1`, `[End08]=-1`, SHA256
`c0fcf0e0a8581549f4dfaaaf493cfc479baa7f979b2ce5b44054e3b771d7cf7f`.
WASAPI Samsung/NVIDIA: **137s** PCM16 estéreo 48kHz,
**11.781 pacotes**, atividade digital (não escuta/lipsync completos).
Media worker preparou WMV/OGG em **6/2ms** sem cancelamento.

O auditor scratch precisou correção de `timeout<0` para `timeout==1`
porque o TSV codifica separadamente o indicador de timeout e o valor
semântico `-1`; não houve mudança no jogo, GPK ou KTRF. Auditoria
reexecutada **valid=true**, sem repetir a campanha. Evidência detalhada
em `../qa/L4_STAGE5_QA_PROGRESS_2026-11-07.md`.

Total **4/28 combinações** até o título; **3/28 com todas as decisões
via SDL mouse hit-test ou expiração natural**. DLC OFF, os demais idiomas,
finais e stress funcional/persistência continuam pendentes. **Gate da
Etapa 5 ainda ABERTO; Etapas 6–7 não iniciadas.**

### Atualização pós-ENDG6 EN/OFF — primeiro cenário retail sem L×H validado

Sessão isolada
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code6-en-off-20261107-0500`,
mesma L4 Release SHA256 `1A996138129204C15647C33F3B9B4467E67EF110BCB6F524326553FEAB565122`.
`--lxh off`: **37/37 cliques SDL mouse** (sem `choose`), final
`05/05-A2-F00`, EndRoll original completo a **1×**, capturas
25/50/75/96%, retorno ao título, runner **exit0** e processo encerrado.
`route-audit.json` e `globalflag-audit.json` ambos **valid=true**;
FlgH `EndNo=6`, `EndClear=-1`, `[End06]=-1`, SHA256
`b72337455a217646e586885bb7f5981d19a75d3a5ed60b4c411cb737909643e2`.
Zero referências `LxH/Media`, fade exclusivo, PNG `-LXH.png` ou sidecar
L×H no perfil/sessão. WASAPI Samsung/NVIDIA: **57s / 5.700 pacotes**
PCM16 estéreo 48kHz, atividade digital detectada — **sem** provar
escuta humana ou lipsync perfeito. Nenhum GPK/save real alterado.
Total agora **5/28** combos até título; **4/28 via SDL hit-test e/ou
timeout natural integral**. SAVE→LOAD, Replay, Options, ES/OFF e
demais combinações ainda pendentes. Etapa 5 permanece **em andamento**;
launcher/RC bloqueados, sem Android/commit/push.

### Atualização Stage5 funcional — Save→Load A→B→A e regressão de aba atual

No mesmo processo L4 Windows com `--documents` exclusivamente em scratch,
dois SaveFiles SLog autênticos foram criados por **MenuBar SAVE → CMAP WideFull
→ editor de comentário nativo → SDL Return**. A (`00/00-00-B01`, nó9) e
B (`01/01-00-E01`, nó44) foram restaurados sucessivamente **A→B→A** com
transições retail de Load, sem RPC de load e sem modificar os SaveFiles; a
barra de sentimentos retornou a imagens de referência A/B/A com **0 diferenças
em 2.848 amostras RGB** por comparação correspondente (A vs B difere em
313 amostras, soma absoluta 12.995).

No primeiro teste, reclicar na **mesma página** de Load apagava indevidamente
o cache de ocupação sem mudar `Page()`, e a linha visualmente ocupada não
aceitava Load. Diagnóstico confirmado por navegação real 2→1. Corrigido
**somente em `SchoolDaysSaveLoadModel.cpp`**, sem tocar no engine; teste novo
`SchoolDays.SaveLoadSamePage` verifica preservação na mesma página e
invalidação correta quando a página muda. **ALL_BUILD Release exit0 e
67/67 CTest Release PASS**. Nova L4 Release SHA256
`0FAEA26D2D1C6316511B714022DCF72BADA545C1899FBC91023D8C4690C3560B`.
Rerun físico da versão corrigida:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-stage5-samepage-retest-20261107-r2`,
START legítimo + **clique redundante página 1 em TODOS os Loads**, resultados
A→B→A corretos, **exit0**, processo encerrado, hashes SLog/sidecar/GlobalFlag
sem alteração; `PHYSICAL_SAME_PAGE_LOAD_A_B_A_PASS`.

Também passaram por cliques SDL físicos nesta sessão os controles
Pause ON/OFF (tick congelado), Auto ON/OFF por imagens, velocidades 1/2/4/12/24
com mute narrativo lógico em 12/24 e recuperação em 1×, BackLog Open/Close,
Back/Restart até tick1 na mesma cena e SkipToEnd com avanço de tick.
Evidência integral e caminhos em `../qa/L4_STAGE5_QA_PROGRESS_2026-11-07.md`.

**Etapa 5 continua aberta, matriz 5/28**. Save→Load A→B→A passa a estar
concluído, mas Replay/PlayData, Options detalhadas, SkipNextChoice e 23
combinações finais/locale/ON-OFF seguem pendentes. Sem launcher/RC/Android,
sem commit/push.

### Atualização Stage5 — ENDG6 ES/OFF passa após regressão same-page

Scratch `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code6-es-off-20261107-1230`;
usou EXE L4 SHA256 `0FAEA26D2D1C6316511B714022DCF72BADA545C1899FBC91023D8C4690C3560B`
com espanhol base `es.json` SHA256
`17f0db1490f97e84e1a83001ee50feee3d82f19749b3625c2f49eaf0fba8495a`.
**37/37 escolhas SDL mouse hit-test**, final autêntico `05/05-A2-F00`,
EndRoll HQ original completo **1×** até tick5867, capturas 25/50/75/96%,
retorno ao título e processo encerrado, runner exit0. Auditorias independentes
`route-audit.json valid=true` e `globalflag-audit.json valid=true`
(`EndNo=6`, `EndClear=-1`, `[End06]=-1`, SHA256
`b72337455a217646e586885bb7f5981d19a75d3a5ed60b4c411cb737909643e2`).
**0** mídias/fades/fundos L×H detectados no OFF e **0** sidecars L×H.
WASAPI Samsung/NVIDIA 60s, 5.998 pacotes PCM16 estéreo 48kHz com
atividade digital (não prova escuta humana/sincronismo perfeito).

Total atualizado **6/28 combinações ponta a ponta**, sendo **5/28**
inteiramente SDL mouse ou timeout natural; **22 pendentes**.
Os detalhes físicos/independentes estão em
`../qa/L4_STAGE5_QA_PROGRESS_2026-11-07.md`. O gate 5 permanece aberto;
Etapas 6–7 ainda não iniciadas, sem Android/commit/push.

### Atualização ENDG6 ES/OFF — retorno ao título e tradução ES visualmente confirmados

Perfil privado único
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code6-es-off-20261107-1230`,
na L4 Release SHA256 `0FAEA26D2D1C6316511B714022DCF72BADA545C1899FBC91023D8C4690C3560B`;
`--translation Release/translations/es.json` SHA256
`17F0DB1490F97E84E1A83001EE50FEEE3D82F19749B3625C2F49EAF0FBA8495A`,
`--lxh off`. **37/37 escolhas via SDL mouse**, nenhum `choose`/`--node`,
final retail legítimo `05/05-A2-F00`, EndRoll HQ completo 1× com
checkpoints 25/50/75/96%, retorno ao título tick5867, runner exit0 e processo
encerrado. Auditoria KTRF independente `route-audit.json valid=true`,
`real_mouse_ack=37`, `returned_to_title=1`, zero timeout. FlgH isolado
`globalflag-audit.json valid=true`, `EndNo=6`, `EndClear=-1`, SHA256
`b72337455a217646e586885bb7f5981d19a75d3a5ed60b4c411cb737909643e2`.
Nenhuma mídia, fade, PNG ou sidecar L×H no modo OFF. WASAPI endpoint
Samsung/NVIDIA: 60s / 5.998 pacotes, PCM16 48kHz estéreo, atividade digital
sem escuta/lipsync isolados. A captura final direta `frame-1.png`
mostra legenda espanhola **“Un año después”**; não certifica visualmente cada
uma das 37 opções exibidas.

**Matriz atualizada: 6/28 combos concluídos**, **5/28 apenas SDL
hit-test/timeout natural** (ENDG5 ainda usa `choose`); **22 combos
pendentes**. Etapa 5 continua aberta, Options/Replay/PlayData/SuperSkip/
provenance e outros gates ainda pendentes. Sem launcher/RC/Android/commit/push.

### Retomada Etapa 5 — sessão Options/Replay e hash L4 atual

O QA privado `functional-options-replay-prime-20261107-r2` encerrou a
sessão PID18284 sem deixar `SchoolDays.exe` ativo. Relatório
`qa_stage5_gui_result.json`: Title Clear confirmado visualmente; Options
General/Sound/Display navegadas e fotografadas sem Apply/resolução alterada;
Replay HScene/PlayData e paginação navegados, miniaturas HScene desbloqueadas
exibidas, SaveFiles privados inalterados. **Não confundir navegação/unlock com
playback HScene/PlayData nem com testes de toggles/restauração de opções.**
Bloqueio declarado pelo QA: `PLAYDATA_UI_EMPTY despite private SLog slots
[0, 1]`; **causa diagnosticada em auditoria READ-ONLY**: a sessão Replay
usou FlgH de ENDG6 (243 entradas, EndClear e REP, sem SaveConfig) com SLog
0/1 de outro perfil SaveLoad (FlgH 37 entradas, quatro chaves metadata
`FILMEngine/SaveFile...`). `ReadSlotMetadata` requer metadata do mesmo perfil
para mostrar ocupação; não há evidência de bug ReplayModel aqui. Próximo QA:
gerar SaveFile + FlgH metadata pela UI do jogo **no mesmo perfil** do Replay,
sem fabricar/mesclar flags. Playback PlayData segue pendente. A matriz física
permanece **6/28** nesta conferência.

Hash SHA256 do EXE L4 atual:
`379F48E60530D334B980A7D39E2C8C52DB32BA077CC2B8182B9B13858F218033`.
**CTest Release serial 67/67 PASS nesse hash** na retomada, sem recompilar,
incluindo regressão SaveLoadSamePage. Etapas 6–7 continuam bloqueadas até
concluir integralmente os gates físicos/AV/persistência da Etapa 5.

### Atualização ENDG6 ES/ON — 7/28 combinações físicas

Nova campanha privada
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code6-es-on-20261107-1334`
com EXE L4 SHA256 `379F48E60530D334B980A7D39E2C8C52DB32BA077CC2B8182B9B13858F218033`
e `es.json` V4b autenticado. **37/37** escolhas reais via `mouse_move` +
`click` SDL, sem `choose`/`--node`, final legítimo `05/05-A2-F00` e epílogo
L×H WMV/OGG integral **1×**; checkpoints 25/50/75/96% e retorno ao título
tick8963, runner exit0/processo encerrado. `route-audit.json valid=true`,
`real_mouse_ack=37`, `video_open=1`, `returned_to_title=1`, sem razões de
falha. WMV/OGG preparados sem cancelamento (62/2ms), fades EndRoll/L×H/
countdown observados. Desta vez `CreateBG` autenticado e screenshot visual
`review-code6-es-final-card-after-CreateBG.png`: cartão inglês universal
**Blue Smile** efetivamente apresentado no tick8880. `globalflag-audit.json`
`valid=true`, EndNo6/EndClear−1/[End06]−1, SHA256
`b72337455a217646e586885bb7f5981d19a75d3a5ed60b4c411cb737909643e2`.
WASAPI Samsung/NVIDIA: 142s, 12.191 pacotes PCM16 48kHz estéreo, RMS
−36,94dBFS/pico −8,61dBFS, atividade digital; sem escuta/lipsync isolados.

**Agora 7/28 cenários ponta a ponta**, **6/28** por SDL/timeout natural;
ENDG5 EN/ON ainda é a exceção com `choose`. Restam **21 combinações** e
gates físicos Replay/PlayData, Save L×H ON→OFF warning EN/ES, Options,
SuperSkip, persistência e AV; Etapa 5 não concluída, Stage6/7 bloqueadas.

### ENDG5 EN/OFF — reteste 1× e matriz passa para 8/28

Rota física em scratch exclusivo
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code5-en-off-v3-20261107-1410`:
**36/36 escolhas SDL mouse**, sem `choose`, `--node`, `debugJump` ou
timeouts artificiais. Corrigido **somente no runner scratch** o instante
da troca MenuBar de 24× para 1×: agora `early_original_one_x=1.0` antes do
final legítimo `05/05-A2-Z00`; primeiro filme abriu em **1×** no tick2.
Quatro amostras reais desse filme em 25/50/75/96% ticks 56/108/148/187,
todas 1×, distintas, com cena autoral restante avançando em 1× até o fim
tick6003 e Title. Runner exit0/processo encerrado; auditor KTRF
`route-audit.json valid=true`, auditor 1× extra
`code5-off-1x-audit.json valid=true`, `globalflag-audit.json valid=true`
com `EndNo5`, `EndClear=-1`, `[End05]=-1`, SHA
`0f829203f59cc9cbf1ad5dc4e22feb4b516c370ee18b7ed0814458329464838a`.
Zero recursos/fades/sidecar L×H no OFF. WASAPI compartilhado 40s,
3.999 pacotes PCM16 48kHz estéreo, atividade digital sem escuta/lipsync
isolados. A tentativa anterior `qa-l4-natural-code5-en-off-20261107-1350`
não contou por primeira mídia 24× e checkpoints colapsados no mesmo tick;
auditor negativo a rejeitou como esperado.

**Etapa 5 agora 8/28 combinações**, **7/28** via SDL/timeout natural;
**20 combinações faltam**. Persistência ON→OFF, Replay/PlayData, menus e
demais gates seguem abertos. Etapas 6–7 ainda bloqueadas; somente Windows,
sem commit/push ou Android.

### Etapa 5 — Replay Data físico validado com save nativo no mesmo perfil

Gate **PlayData real PASS** em
`D:/Dev/_temp_lxh_static_audit_worker8/qa-stage5-playdata-legit-prime-20261107-r2`,
mesma build L4 SHA256 `379F48E60530D334B980A7D39E2C8C52DB32BA077CC2B8182B9B13858F218033`,
PID7992/DevBridge48578/`--documents` privado. Perfil iniciado com FlgH
ENDG6 genuíno (EndClear e 7 REP unlock), **sem SLog importado**; START
e SAVE nativos por MenuBar/CMAP/editor TextInput Return criaram
SaveFile000 v1.02, sidecar `lxh=0`, e SaveConfig primário/`_Sub` tipo8
no MESMO GlobalFlag. Return to Title pelo modal YES, Replay HScene →
Replay Data mostrou a **linha 0 ocupada**. Clique SDL na linha 0 ativou
`playDataOpen=true`, gameplay/filme/legendas inglesas e chip `REPLAYMODE`,
com screenshot real `real-playdata-gameplay-retry1.png`. TOPMENU durante
PlayData abre diálogo autoral `Return to Title Screen?`, **não** retorno
imediato: YES físico via CMAP retornou Title e fechou PlayData. Relatório
privado `qa-stage5-timeout-review-20261107/playdata-popup-confirm-prime-20261107-r2/qa_playdata_native_return_result.json`:
`status=PASS`, hashes SHA256 de SaveFile/sidecar/GlobalFlag **inalterados**.
Os dois aborts iniciais vieram de premissas erradas do harness (frame
preto antes de primeiro gameplay visível; esperar `scene==""` e retorno
direto sem modal). Não exigiram patch do runtime. Processo privado
encerrado, nenhum SchoolDays.exe restante. **HScene playback pendente**;
matriz endings mantém **8/28, restam 20**; persistência L×H ON→OFF,
SuperSkip, QuickSave, Overwrite e Options reversíveis ainda pendentes.
Etapas 6 e 7 bloqueadas; sem Android/commit/push.
