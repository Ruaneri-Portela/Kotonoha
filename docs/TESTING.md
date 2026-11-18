# Kotonoha test groups

CTest suites use exactly one of two labels:

- `synthetic`: does not need a user's commercial/retail game data. May create temporary media or use the repository's controlled test fixtures.
- `real`: needs game assets, disc images, or game-specific route/installation data, including tests whose fixture setup depends on `game-assets`.

## Running the tests

Configure and compile your native build with `-DBUILD_TESTING=ON`, then run:

```sh
ctest --test-dir build-linux -L '^synthetic$' --output-on-failure
ctest --test-dir build-linux -L '^real$' --output-on-failure
ctest --test-dir build-linux -N -L '^real$'
```

For Windows multi-config builds, add `-C Release` (or your configuration).

The GitHub Actions Linux, Windows and macOS builds run **only synthetic CTest**.
The WASM browser smoke tests use generated/self-test fixtures, not retail installations.
Android CI validates example/SchoolDays APK creation; it does not execute retail-data tests.
Retail tests are configured only when the appropriate per-game CMake data path or disc image is supplied.
Check individual game `tests/CMakeLists.txt` files for the `*_DATA_DIR`, `*_DISC_IMAGE` and other supported options.

A failed synthetic regression test emits a GitHub Actions warning and remains visible in the logs;
it does not suppress errors from compilation, malformed packages, or artifact uploads.
