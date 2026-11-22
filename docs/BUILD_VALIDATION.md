# Build validation

**English (US)** | [Português (Brasil)](BUILD_VALIDATION.pt-BR.md)

Validate the CMake game matrix and Xcode schemes after build-system changes:

```sh
python3 tools/ValidateBuildMetadata.py
```

The read-only check verifies **15 CMake game applications**, **32 Xcode schemes** (15 games plus the unified launcher on macOS and iOS), no untracked `xcode_pending` gaps, target references, dependency-preparation actions, and `.xcconfig` includes. Android has 15 game flavors plus a separate `launcher` flavor (`KOTONOHA_BUILD_GAME=all`); run `python3 tools/check_release_matrix.py` to verify both matrices. It needs only Python 3 and does not compile anything.

For native build validation, use the platform commands in [build guides](BUILD_GUIDES.md), installed SDKs/vcpkg and local game data as needed. This metadata check is not a replacement for compilation, tests, or packaging verification.
