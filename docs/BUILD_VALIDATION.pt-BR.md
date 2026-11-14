# Validação de compilação

[English (US)](BUILD_VALIDATION.md) | **Português (Brasil)**

Compile e teste os alvos modificados. **Compilar sem erros não comprova compatibilidade integral com o jogo original.** Testes que precisam de dados comerciais devem ser configurados localmente; não versionar esses assets nem relatórios que os contenham.

## Desktop

```sh
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Para jogos, informe `KOTONOHA_BUILD_*` e o alvo nativo. Filtre os testes com `ctest --test-dir build -R '<jogo>' --output-on-failure`.

## Android

```sh
cd buildsystems/android
./gradlew :app:assembleExampleDebug -Pabis=arm64-v8a
```

Para outros jogos, troque `Example` pelo nome do flavor. Execute builds Gradle sequencialmente quando compartilham diretórios nativos.

## Apple

```sh
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaExample (macOS)' -configuration Debug build
```

Use um scheme `(iOS)` com destino de simulador/dispositivo para compilar iOS. O projeto Xcode já está versionado; os antigos caminhos do gerador Ruby/workspace estão obsoletos.

## Escopo dos resultados

Registre **plataforma, compilador/SDK, configuração, alvo, comando e resultado** em cada validação. Afirmações históricas descrevem a máquina e a revisão realmente testadas, não garantem a árvore atual.
