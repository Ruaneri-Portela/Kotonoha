# LucaSystem

[English (US)](README.md) | **Português (Brasil)**

Interpretação nativa de PAK/bytecode e renderização de imagem/áudio/texto testadas com AIR.

**Estado:** O suporte documentado é específico do AIR; não implica compatibilidade equivalente com outros títulos LucaSystem.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_LUCA_SYSTEM=ON
cmake --build build --target LucaSystem
```

## Dados do jogo

Use PAK originais (incluindo PARAM.PAK e SCRIPT.PAK); AIR é o título de referência.
Use o launcher ou passe `--luca-system /caminho/para/dados` ao executável. Nunca versione dados comerciais.

## Investigação detalhada

[Resumo do estado do port](STATUS.pt-BR.md) · [Investigação detalhada](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)

## Saves e retomada de execução

O formato `LUCA_SYSTEM_SAVE_3` preserva a posição no script, os
registradores numéricos, as variáveis de texto (incluindo espaços, quebras de
linha e bytes nulos), flags, pilha de GOSUB, mensagem visível e tempo restante
de espera. Carregar um save não deve executar automaticamente o próximo
opcode: a execução continua a partir da posição registrada.

Saves `LUCA_SYSTEM_SAVE_2` anteriores continuam aceitos quando válidos;
valores de texto que já foram truncados pelo formato antigo não podem ser
recuperados. Arquivos inválidos são rejeitados antes de aplicar o estado à VM.
O estado visual completo da cena e a compatibilidade total com AIR ainda
precisam de testes com os dados originais.

Teste de regressão independente de dados comerciais:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DKOTONOHA_BUILD_LUCA_SYSTEM=ON
cmake --build build --target LucaSystemSaveCodecTests
ctest --test-dir build -R LucaSystem.SaveCodec --output-on-failure
```

## Diálogos na ordem dos opcodes

A entrada agora usa a mesma rotina para teclado, mouse e ações de avanço:
primeiro revela a fala incompleta; depois retoma o interpretador no próximo
opcode. Isso impede que o motor mostre a lista de textos extraídos ignorando
comandos de imagem, música, waits ou desvios intermediários.

F5/F6 têm prioridade sobre a confirmação da caixa de diálogo, permitindo
salvar/carregar enquanto uma fala está visível. Um teste sintético verifica
a decisão `Reveal` versus `ResumeScript`. Ainda é necessária a validação
com o AIR original.

## Saves visuais V4 e chamadas entre cenários V5

O formato `LUCA_SYSTEM_SAVE_5` registra os IDs de imagem, posições dos objetos
`IMAGELOAD` e os quadros da pilha de chamadas entre múltiplos cenários (`CallStackFrame`).
Ao carregar, reabre as imagens dos PAKs originais e restaura a camada visual e o histórico
de sub-rotinas entre scripts. Saves `V4`, `V3` e `V2` permanecem 100% legíveis.

**Limites:** o efeito visual original, as camadas de fundo, transições e
animações ainda exigem reconstrução. O save não inclui pixels nem arquivos
de mídia; o jogo original precisa estar acessível no carregamento.

## Composição e transições experimentais

`SceneLayers` agora separa imagens `IMAGELOAD` ainda não apresentadas das
camadas exibidas. O opcode `DRAW` publica as imagens em ordem e usa uma
transição de opacidade com duração limitada. O final de uma instrução
`MESSAGE` ou `END` também publica imagens pendentes se o título omitir
`DRAW`. `FADE` usa uma sobreposição preta temporizada e `WIPE` utiliza
uma transição de opacidade provisória. A renderização SDL aplica a opacidade
por camada, enquanto o codec V4 continua salvando apenas o estado exibido.

**Importante:** ainda são *aproximações visuais*, não a implementação exata
das curvas, máscaras, direção dos wipes, composição original do fundo ou
operações de animação do AIR. Precisam ser comparadas com o executável e
assets originais. Teste C++17 sem SDL:

```sh
cmake --build build --target LucaSystemSceneLayersTests
ctest --test-dir build -R LucaSystem.SceneLayers --output-on-failure
```

## Recorte horizontal em WIPE

O opcode `WIPE` passa a usar um modo específico de revelação da esquerda
para a direita no lugar do cross-fade provisório. O renderizador recorta tanto
a textura de origem quanto o destino no limite de avanço da transição, sem
esticar sprites. Imagens da composição anterior são preservadas durante o
wipe; somente imagens recém-adicionadas são reveladas. O teste
`LucaSystem.SceneLayers` verifica progresso e limites de recorte.

Isso ainda **não é uma transição AIR 1:1**: direções e máscaras específicas,
operação de objetos e temporizações originais exigem validação com o jogo.

## Composição incremental sem desenho duplicado

Durante `DRAW` ou `WIPE`, os objetos anteriores formam a camada de
saída e apenas os objetos recém-publicados são renderizados na camada de
entrada, com opacidade ou recorte variável. Isso impede que imagens
transparentes antigas sejam desenhadas duas vezes e fiquem artificialmente
escuras/opacas durante a transição. Ao término, a composição inteira é
desenhada uma única vez. O teste `LucaSystem.SceneLayers` verifica a
fronteira entre objetos antigos e novos.

Ainda não há garantia de correspondência exata às máscaras, camadas,
ordens e curvas do AIR original.
