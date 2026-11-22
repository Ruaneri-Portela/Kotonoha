# Validação de build

[English (US)](BUILD_VALIDATION.md) | **Português (Brasil)**

Após mudar o CMake ou o projeto Xcode, verifique os metadados:

```sh
python3 tools/ValidateBuildMetadata.py
```

A verificação, que não modifica arquivos, confere **15 aplicações de jogo no CMake**, **32 schemes Xcode** (15 jogos mais o launcher unificado para macOS e iOS), ausência de pendências `xcode_pending` sem registro, IDs dos alvos, preparação de dependências e includes `.xcconfig`. O Android possui 15 flavors de jogos e um flavor `launcher` separado (`KOTONOHA_BUILD_GAME=all`); execute `python3 tools/check_release_matrix.py` para validar ambas as matrizes. Exige apenas Python 3 e não compila código.

Para testar a compilação nativa, siga os [guias de build](BUILD_GUIDES.pt-BR.md), usando SDKs/vcpkg e dados locais quando necessários. A verificação de metadados não substitui build, testes nem validação de empacotamento.
