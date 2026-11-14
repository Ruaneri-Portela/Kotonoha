# Compilação iOS

[English (US)](BUILD_IOS.md) | **Português (Brasil)**

O projeto Xcode versionado é `buildsystems/xcode/Kotonoha.xcodeproj`. Use macOS com Xcode e as dependências SDL/vcpkg próprias do destino.

```sh
open buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaExample (iOS)' \
  -configuration Debug \
  -sdk iphonesimulator \
  -destination 'generic/platform=iOS Simulator' build
```

Para dispositivos físicos, selecione o destino iOS e configure a assinatura no Xcode. Os jogos utilizam outros schemes do mesmo projeto; veja o [guia Xcode](../buildsystems/xcode/README.pt-BR.md).

**Atenção:** Guias antigos citam `UpdateGameTargets.rb` e `Kotonoha.xcworkspace`. Nenhum dos dois está presente na árvore versionada atual. Use `Kotonoha.xcodeproj` e não execute o gerador ausente. A compilação depende dos SDKs e frameworks locais.
