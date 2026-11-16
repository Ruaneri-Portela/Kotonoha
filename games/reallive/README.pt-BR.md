# RealLive

[English (US)](README.md) | **Português (Brasil)**

Motor nativo para cenários, gráficos e áudio RealLive sem executar o programa Windows.

**Estado:** Uma edição de referência possui ampla cobertura, mas a equivalência total de rotas/interface não foi validada.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON
cmake --build build --target RealLive
```

## Dados do jogo

Use instalação original ou ISO compatível com Seen.txt, Gameexe.ini e recursos.
Use o launcher ou passe `--reallive /caminho/para/dados` ao executável. Nunca versione dados comerciais.

## Compatibilidade nesta branch

- Arquivos SEEN carregados por VFS/ISO agora preservam o REGNAME do
  Gameexe.ini ao selecionar o formato de script RealLive. Isso corrige uma
  incompatibilidade concreta na inicialização de roteiros com a segunda
  camada de codificação, como a edição Full Voice.
- Entrada e retorno ao menu utilizam o cenário SEEN_MENU configurável, sem
  pressupor que todo jogo use o cenário 9032.
- Há validação de limites para scripts sintéticos comprimidos e truncados.

Essas mudanças visam melhorar a abertura do CLANNAD e o frontend de
planetarian. **Não comprovam jogabilidade completa:** ainda faltam ensaios
com as edições originais, escolhas, imagens, áudio, saves e transições.

## Investigação detalhada

[Resumo do estado do port](STATUS.pt-BR.md) · [Investigação detalhada](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)

## Segurança do fluxo de execução

Chamadas locais e entre cenários agora impõem um limite de profundidade para
a pilha de retorno. Uma tentativa de entrar em um cenário inexistente não deve
deixar um retorno fantasma; um retorno inválido não deve consumir o quadro
salvo antes da validação. O teste de sessão mantém suas asserções ativas
também em builds `Release`, mas depende de dados RealLive originais.
