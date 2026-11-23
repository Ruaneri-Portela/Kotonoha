# N2System

[English (US)](README.md) | **Português (Brasil)**

Port do motor Nitroplus N2System / NsScript VM, gerenciador de variáveis, grafo de cena, sistema de áudio, caixa de diálogo com typewriter e persistência de save state baseado no [NitroSharp](https://github.com/CommitteeOfZero/nitrosharp).

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_N2SYSTEM=ON
cmake --build build --target N2SystemGame
```

## Compilação no Android

```sh
cd buildsystems/android
./gradlew :app:assembleN2SystemDebug
```

## Comportamento em execução

- `CreateSound` apenas cria uma entidade de som silenciada. Quem inicia a
  reprodução é `Request(name, "Play")`; `SetVolume`/`SetLoop` por entidade são
  aplicados ao canal único de reprodução. `@BGM04` e `BGM04` endereçam a mesma
  entidade. `"Disused"` não interrompe a reprodução.
- Steins;Gate mantém um único canal de BGM do menu para a jogabilidade. A BGM
  do título (`sound/bgm/bgm04.ogg`) é iniciada por `function.SoundPlay` e não é
  interrompida na transição menu → jogo.
- Após escolher START, `sg00_01.nsb` cria `dx/prologue01.ngs` /
  `dx/prologue02.ngs` via `CreateMovieSG` e aguarda com `MovieWaitSG` (que
  consulta `RemainTime`). O filme de introdução não repete e pode ser pulado
  com o gesto de skip deliberado do motor.

## Estado Detalhado

[Resumo do estado](STATUS.pt-BR.md) | [Status in English](STATUS.md)

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)
