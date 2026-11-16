# Pure Mail DVD: comparação da inicialização

[English (US)](STARTUP.md) | **Português (Brasil)**

O executável `data/pmail.exe` (PE32 x86, SHA-256
`a9359d55f23831ebee3b4ca803c6aff1d28a37468de1ccb0e19f8d388ca0e654`)
foi examinado diretamente. As descobertas valem para esta versão.

## Evidências confirmadas

- Entrada PE `0x42d5f0`: prepara o runtime Windows/C.
- `0x4137e0`: resolve caminhos de instalação e os pacotes. O porte utiliza
  o sistema de arquivos virtual e multiplataforma do Kotonoha.
- `0x422357..0x4223df`: o script inicial é `start.obj`, com a biblioteca
  de funções `pmail.obj`. O porte não tenta mais iniciar scripts aleatórios
  quando um dos arquivos exigidos está ausente.
- `0x43f7dc` (offset `0x3f7dc`) contém a semente original
  `01 00 00 00`; a VM agora inicializa e reinicializa com 1.
- A tabela em `0x441358` contém quatro variantes: `PM01L_I5.AVI`,
  `PM01S_I5.AVI`, `PM01L_I3.AVI` e `PM01S_I3.AVI`.
  `PM01L_I3` não era considerado antes.

## Implementação e testes

O teste sintético `Pmail.Startup` verifica a primeira saída do gerador
pseudoaleatório, o reset da VM e a busca dos vídeos sem usar assets
comerciais. Plataformas nativas priorizam o AVI original; o navegador
prioriza WebM/MP4 compatíveis. Os caminhos testados são `data/movie/`
e `movie/`.

`PMAIL_MOVIE_VARIANT=PM01L_I3` força uma variante instalada para
comparações. Essa é uma opção de diagnóstico do porte, **não** do jogo
original. `PMAIL_SKIP_INTRO` permanece para testes automatizados.

## Diferenças pendentes para fidelidade 1:1

A escolha precisa da variante do vídeo depende de ramificações de
configuração/CPU em `0x4145bc..0x414720` ainda não reproduzidas. A ordem
dos logotipos, a sequência de abertura até o título e o tempo exato
dos quadros também exigem comparação com o processo original.
Não há alegação de fidelidade completa.
