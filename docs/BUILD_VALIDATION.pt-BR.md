# Validação de build

[English (US)](BUILD_VALIDATION.md) | **Português (Brasil)**

Após mudar o CMake ou o projeto Xcode, verifique os metadados:

```sh
python3 tools/ValidateBuildMetadata.py
```

A verificação, que não modifica arquivos, confere os 13 jogos CMake/Android, os 22 schemes Xcode existentes, as pendências N2System/PONscripter declaradas (`xcode_pending`), IDs dos alvos, preparação de dependências e includes `.xcconfig`. Exige apenas Python 3 e não compila código.

Para testar a compilação nativa, siga os [guias de build](BUILD_GUIDES.pt-BR.md), usando SDKs/vcpkg e dados locais quando necessários. A verificação de metadados não substitui build, testes nem validação de empacotamento.
