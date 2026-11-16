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
