# PONscripter (`Saya no Uta PT-BR`)

[English (US)](README.md) | **Português (Brasil)**

Port C++17 clean-room da máquina virtual PONscripter (`20090118` / `20091015`), leitor de arquivos SAR/NSA, formatador de texto proporcional e runtime SDL3 para *Saya no Uta* (`SayaNoUta_PT-BR`).

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_PONSCRIPTER=ON
cmake --build build --target PonscripterGame
```

## Dados de jogo HTTP(S) no navegador

O build WebAssembly utiliza o filesystem virtual com Fetch HTTP(S). Sem
listagem de diretório ou `manifest.json`, o detector consulta nomes conhecidos
como `00.utf`, `0.utf`, `arc.nsa` e `arc.sar` usando HEAD/Range.
Scripts e mídias soltos podem ser lidos sob demanda. Scripts com nomes
incomuns ou lacunas na numeração ainda podem exigir índice ou pasta local.
Diferentemente do leitor XP3, o leitor SAR/NSA atual carrega o arquivo
inteiro na memória; pacotes grandes podem ultrapassar os limites do navegador.
Detectar a fonte não comprova que o jogo inteiro funcione.

Veja [WebAssembly e HTTP](../../docs/BUILD_WEB.pt-BR.md) para requisitos
de Range/CORS e a opção manual **Usar HTTP(S)**.

## Estado Detalhado

[Resumo do estado](STATUS.pt-BR.md) | [Status in English](STATUS.md)

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
