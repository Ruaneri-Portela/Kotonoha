# Kotonoha Xcode (macOS e iOS)

[English (US)](README.md) | **Português (Brasil)**

Use o projeto **versionado**: `buildsystems/xcode/Kotonoha.xcodeproj`. Escolha o scheme macOS ou iOS do jogo. A árvore atual contém um `Kotonoha.xcodeproj`, não o workspace multiprojeto descrito em notas antigas.

```sh
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaExample (macOS)' \
  -configuration Debug build
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaExample (iOS)' -configuration Debug \
  -sdk iphonesimulator \
  -destination 'generic/platform=iOS Simulator' build
```

**Dependências:** Configure `Config/*.xcconfig` para SDL3, SDL3_ttf, FFmpeg, libass, bibliotecas de fontes e vcpkg locais. DokiDoki também exige frameworks CPython e biblioteca padrão compatíveis com a plataforma; consulte `tools/PrepareApple.py` para os passos disponíveis. Frameworks SDL devem corresponder ao SDK selecionado e ser incorporados aos bundles. Dados comerciais devem ficar fora do Git.

**Limitações:** Rodar em iOS físico exige assinatura. Comandos históricos do gerador `ruby tools/UpdateGameTargets.rb` não funcionam neste checkout. Consulte os guias [macOS](../../docs/BUILD_MACOS.pt-BR.md), [iOS](../../docs/BUILD_IOS.pt-BR.md) e [validação](../../docs/BUILD_VALIDATION.pt-BR.md).
