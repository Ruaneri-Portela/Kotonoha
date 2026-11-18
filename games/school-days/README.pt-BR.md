# School Days HQ

[English (US)](README.md) | **Português (Brasil)**

Reprodução de mídia/roteiros originais, menus nativos, saves e investigação de fidelidade.

**Estado:** A fidelidade ao original e a validação integral de rotas/finais continuam incompletas.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_SCHOOL_DAYS=ON
cmake --build build --target SchoolDays
```

## Dados do jogo

Forneça os GPK originais e licenciados. A interface Display Options do Windows usa arte personalizada separada.
Use o launcher ou passe `--school-days /caminho/para/dados` ao executável. Nunca versione dados comerciais.

## Compatibilidade dos índices GPK

O leitor de GPK agora aceita preenchimento de zeros após o terminador do PIDX
decodificado e espaços físicos não utilizados entre arquivos ou antes do índice.
Continuam rejeitados arquivos sobrepostos, bytes não nulos após o terminador e
dados fora dos limites do pacote. O comportamento foi conferido com o leitor
[DaysEngine GPK](https://github.com/superboo07/DaysEngine/tree/master/crates/days-gpk),
licenciado sob MIT, sem adicionar dependência de Rust.

Os testes de regressão usam arquivos sintéticos. Isso **não** significa suporte
automático a Shiny Days/Cross Days: descoberta de chaves, layouts de pacotes e
diferenças de runtime ainda exigem trabalho específico. A confirmação com os
jogos originais depende de dados obtidos legalmente pelo usuário.

## Investigação detalhada

[Resumo do estado do port](STATUS.pt-BR.md) · [Investigação detalhada](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Índice de investigações](docs/README.pt-BR.md) e [assets da interface Display Options](display-options/README.pt-BR.md).

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
