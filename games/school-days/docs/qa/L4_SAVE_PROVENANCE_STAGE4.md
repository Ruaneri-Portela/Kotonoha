# L4: provenance sidecar de NOVOS saves, sem alterar SLog/GlobalFlag

Implementação game-specific: `SchoolDaysSaveProvenance.{hpp,cpp}`,
`SchoolDaysSaveStorage::{SaveFilePath}` e `SchoolDaysRuntime`.
Nenhum campo comercial é acrescentado a `SaveFile*.DAT` ou `GlobalFlag`.

## Contrato

- Cada novo save de slot L4 recebe o companion
  `<SaveFile>.kotonoha-lxh.v1` dentro do perfil de `--documents`. O
  `QuickSave` recebe o mesmo suffix quando o destino está sob `documents`.
- Formato privado V1: magic fixo, SHA-256 de `caminho absoluto normalizado+slot`,
  comprimento e SHA-256 dos bytes exatos do SaveFile, SceneKey do **último
  tag1 SLog**, `lxh=0/1` e tick/limites do epílogo.
- `lxh=1` exige L×H ON, um dos sete finais, ORS com EndRoll/PLAY_MOVIE
  L×H/Next, `movieStates` do vídeo efetivamente despachado,
  `eventTouched`, fase de entrada/epílogo e
  `movieStartTick <= tick < nextTick`. Replay/PlayData, créditos comerciais,
  SaveFeel e handoffs nunca recebem `lxh=1`; novos saves comuns recebem
  sidecar com `lxh=0`.
- **DLC OFF + sidecar válido + lxh=1:** LOAD de narrativa é recusado antes da
  animação de carregamento e antes de `prepareCarryFrame`, retire de ponteiros
  ou commit KTRF. Modal persistente EN/ES orienta ativar DLC e reiniciar;
  ESC/Enter/clique dispensam aviso sem fechar SaveLoad. `loadSlot` faz
  rechecagem imediatamente antes da restauração e `loadGame` antes de tocar
  os buffers do jogo. **DLC ON:** caminho original.
- Sidecar ausente/alterado/malformado/hash divergente/SceneKey divergente:
  `Untrusted`, sem afirmação de origem L×H nem bloqueio especulativo. Retail
  e saves legacy antigos continuam compatíveis.

## Segurança de overwrite e limites de recuperação

1. Antes de sobrescrever qualquer SaveFile gerenciado, invalida-se o sidecar
   anterior. Falha ao invalidar interrompe o write **antes** do truncamento.
2. O SaveFile continua usando o escritor nativo, com `SaveService::Commit`
   e gravação separada do GlobalFlag. Não alteramos a ordem interna desses
   dois persistidores nem introduzimos transação geral no engine.
3. Após ambos concluírem, relemos o SaveFile e confirmamos seus bytes
   integralmente. O novo sidecar é gravado em arquivo temporário no mesmo
   diretório e publicado por rename; falhas geram erro visível e estado
   `Untrusted`. Temporários deixados por falha não são considerados no load.

**Limitação deliberada:** SaveFile + GlobalFlag + sidecar **não** são uma
transação atômica única. Queda de energia/processo entre a invalidação antiga e
publicação do novo sidecar pode deixar um SaveFile gravado sem provenance.
Por compatibilidade obrigatória, ele será tratado como legacy/untrusted
(potencial *falso-negativo*, nunca afirmação inventada de L×H). Um journal
durável e alterações mais amplas no writer seriam necessários para eliminar
completamente essa janela, fora do escopo seguro autorizado. `rename` não
significa `fsync` durável do diretório em todas as plataformas.

O SHA-256 vincula **integridade dos bytes**, não autentica uma pessoa contra
edição deliberada simultânea de SaveFile + sidecar. Copiar um sidecar entre
perfis muda o binding de caminho e o invalida. O sidecar registra **origem no
momento de salvar**; não restaura posição temporal ao carregar: o loader
comercial ainda começa a cena no tick zero.

## Validação

Teste `SchoolDays.SaveProvenance` usa apenas arquivos temporários e ORS/SLog
sintéticos: SHA-256 NIST (vazio, abc, milhão de 'a'), sete finais,
limites de tick/evento, overwrite com bytes idênticos, perfis `--documents`
isolados, slot versus QuickSave, sidecar inválido e falha de invalidação.
Runtime sintético valida recusa com DLC OFF, dismiss/retry e invariância
de Gameplay*, ORS*, tick, StateStore, SLog, LoadTransition e SaveLoad owner.
Sem execução de GUI, packs, ORS privados ou saves reais.
