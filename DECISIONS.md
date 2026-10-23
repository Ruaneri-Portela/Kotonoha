# Technical Decisions

Registro das decisões arquiteturais importantes do projeto.

Formato: `DEC-XXX`

---

## DEC-001 — Documentação obrigatória

### Estado

ATIVA

### Decisão

Toda alteração relevante na Kotonoha Engine deverá atualizar a documentação do projeto.

### Estrutura

- `MASTER.md` → estado atual;
- `CHANGELOG.md` → histórico;
- `DECISIONS.md` → decisões arquiteturais;
- `TODO.md` → backlog e próximos passos.

### Motivo

O projeto continuará durante múltiplas conversas e sessões.

Informações essenciais não podem depender apenas do histórico de chat.

---

## DEC-002 — Estados de validação

### Estado

ATIVA

### Decisão

Toda modificação será classificada como:
- HIPÓTESE;
- IMPLEMENTADO;
- VALIDADO.

### Motivo

Código aparentemente correto não será considerado funcional até teste real.

---

## DEC-003 — Teste em hardware real

### Estado

ATIVA

### Decisão

O Poco X7 Pro ARM64 é o dispositivo principal de validação Android.

---

## DEC-004 — Assets School Days fora do código público

### Estado

ATIVA

### Decisão

Arquivos proprietários do jogo não devem fazer parte de commits públicos da engine.

Durante desenvolvimento eles podem existir localmente no ambiente de teste.

---

## DEC-005 — Execução local no Android

### Estado

ATIVA

### Decisão

A arquitetura final não deverá depender de servidor HTTP externo.

Os recursos do jogo devem poder ser utilizados localmente no dispositivo.

---

## DEC-006 — Debug permanece durante desenvolvimento

### Estado

ATIVA

### Decisão

FPS, timestamp/timeline e interfaces de diagnóstico podem permanecer ativos enquanto a engine está sendo desenvolvida.

---

## DEC-007 — Transferência de ownership do frame retido entre Canvas

### Estado

ATIVA

### Decisão

A correção de transição reutiliza o mecanismo existente de `dirtyTexture` em vez de criar um segundo framebuffer global.

Quando uma Gameplay troca para outra, a propriedade da textura retida é transferida ao Canvas de destino antes do reset da origem.

### Motivo

- aproveita `KOTONOHA_SCENE_DRAW_LAST`;
- evita cópia GPU → CPU;
- evita framebuffer adicional;
- mantém a correção localizada no lifecycle de Canvas/Gameplay.
---

## DEC-008 — Primeira exibição da Gameplay deve ser preparada de forma síncrona

### Estado

ATIVA / EXPERIMENTAL ATÉ VALIDAÇÃO

### Decisão

A entrada de uma nova Gameplay não deve depender exclusivamente do ciclo assíncrono de 50 ms do `EventManager`.

Antes da primeira apresentação da cena, os eventos iniciais necessários para Image/Video/Audio devem ser processados síncronamente.

### Motivo

Evitar que a nova Gameplay seja apresentada antes de possuir conteúdo visual pronto, preservando uma transição direta entre o último quadro da cena anterior e a primeira exibição da próxima.
---

## DEC-009 — WAITING não deve substituir o frame visualmente válido anterior

### Estado

ATIVA / EXPERIMENTAL ATÉ VALIDAÇÃO

### Decisão

Um componente que retorna `KOTONOHA_SCENE_WAITING` declara que ainda não possui conteúdo pronto para apresentação.

Seu target não deve substituir `dirtyTexture` até existir um frame válido.

### Motivo

Separar dois estados semanticamente diferentes:

- `DRAW`: existe conteúdo pronto para apresentar;
- `WAITING`: ainda não existe conteúdo apresentável.

Isso permite manter continuidade visual durante handoff entre vídeos.
---

## DEC-010 — A última textura válida do vídeo continua desenhável até o fim do evento

### Estado

ATIVA / EXPERIMENTAL ATÉ VALIDAÇÃO

### Decisão

A ausência temporária de um novo `AVFrame` não invalida uma `SDL_Texture` já apresentada.

Enquanto o evento de vídeo ainda estiver ativo:

- se existe textura válida, ela continua sendo apresentada;
- `WAITING` significa que nenhuma textura válida existe ainda.

### Motivo

Evitar que desalinhamentos entre duração decodificável do WMV, PTS e intervalo ORS criem uma lacuna visual preta entre vídeos consecutivos.