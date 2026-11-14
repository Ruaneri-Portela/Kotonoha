# Build validation

**English (US)** | [Português (Brasil)](BUILD_VALIDATION.pt-BR.md)

Validate the CMake game matrix and Xcode schemes after build-system changes:

```sh
python3 tools/ValidateBuildMetadata.py
```

The read-only check verifies 11 game IDs, the 22 Xcode schemes, target references, dependency-preparation actions, and `.xcconfig` includes. It needs only Python 3 and does not compile anything.

For native build validation, use the platform commands in [build guides](BUILD_GUIDES.md), installed SDKs/vcpkg and local game data as needed. This metadata check is not a replacement for compilation, tests, or packaging verification.
