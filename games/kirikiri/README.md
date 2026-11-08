# Kirikiri2 / KirikiriZ port

Suporte a jogos da engine [Kirikiri2](https://github.com/krkrz/krkr2) / [KirikiriZ](https://github.com/krkrz/krkrz) (吉里吉里2/Z) no Kotonoha.

## O que é suportado

- **XP3 archives** (`.xp3`): leitura de entradas compactadas e não compactadas, sem carregar todo o arquivo na memória (leitura sob demanda).
- **Arquivos soltos**: leitura direta do sistema de arquivos do jogo.
- **KAG Script (`.ks`)**: parsing de tags KAG para texto, plano de fundo, personagens, escolhas, saltos, rótulos, espera e áudio básico.
- **Launcher**: seleção de pasta/ISO via `--kirikiri` ou pelo launcher de pastas do Kotonoha.
- **Save/load**: quicksave (F5/F6) e menu do jogo.
- **Integração Kotonoha**: engine, renderer, áudio, overlays, settings e MCP compartilhados.

## O que ainda não é suportado

- **TJS2 bytecode**: scripts `.ks` pré-compilados (bytecode TJS) não são interpretados. Jogos como NEKOPARA usam bytecode compilado e precisam de um interpretador TJS2 separado.
- **Plugins Kirikiri**: plugins externos (`.dll`) não são carregados.
- **TLG6 imagens**: apenas formatos nativos do SDL/FFmpeg.

## Como construir

O alvo é construído automaticamente com o projeto (habilitado por padrão). Para desativar:

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=OFF
```

## Como executar

```sh
build/games/kirikiri/Kirikiri.app/Contents/MacOS/Kirikiri \
  --kirikiri /caminho/para/dados/do/jogo \
  --no-launcher
```

Ou use o launcher de pastas (`--launcher`, padrão) para selecionar o diretório de dados.

O jogo é válido se a pasta contiver arquivos `.ks` ou um arquivo `.xp3`.

## Exemplo de script KAG suportado

```ks
[cm text="Olá, mundo!" speaker="Narrador"]
[wait time="1000"]
[chara_show layer="0" file="hero.png" x="100" y="200"]
[select question="Escolha:" option1="Sim" option2="Não"]
```

## Testes

```sh
ctest --test-dir build -R Kirikiri
```

## Licença

Este port é uma implementação independente e não distribui nenhum código binário da engine Kirikiri/KirikiriZ nem dados comerciais de jogos. Forneça seus próprios dados legalmente obtidos.
