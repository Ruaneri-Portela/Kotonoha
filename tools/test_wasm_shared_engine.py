#!/usr/bin/env python3
"""Synthetic main/side WASM layout tests, without emsdk or retail assets."""
from __future__ import annotations

import json
from pathlib import Path
import tempfile

from check_wasm_asyncify import WasmValidationError, leb
from check_wasm_shared_layout import custom_sections, validate
from generate_wasm_side_asyncify_imports import generate

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b"\0asm\x01\0\0\0"


def section(kind: int, data: bytes) -> bytes:
    return bytes([kind]) + leb(len(data)) + data


def side_module(symbols: tuple[str, ...], dynamic: bool = True) -> bytes:
    payload = MAGIC
    if dynamic:
        name = b"dylink.0"
        payload += section(0, leb(len(name)) + name + b"\0")
    exports = bytearray(leb(len(symbols)))
    for symbol in symbols:
        name = symbol.encode("utf-8")
        exports.extend(leb(len(name)) + name + b"\0" + leb(0))
    return payload + section(7, bytes(exports))


def run() -> None:
    cmake_lib = (ROOT / "lib/CMakeLists.txt").read_text()
    cmake_app = (ROOT / "buildsystems/cmake/KotonohaApp.cmake").read_text()
    options = (ROOT / "buildsystems/cmake/KotonohaOptions.cmake").read_text()
    html = (ROOT / "buildsystems/cmake/index.html.in").read_text()
    bridge = (ROOT / "lib/src/core/WebMainExports.cpp").read_text()
    provider = (ROOT / "lib/src/Filesystem/Sources/FetchAssetProvider.cpp").read_text()
    cache = (ROOT / "lib/web/KotonohaSharedWasmCache.js").read_text()
    assert "option(KOTONOHA_WASM_SHARED_ENGINE" in options
    assert 'set(KOTONOHA_LIB_TYPE STATIC)' in cmake_lib
    assert 'if(EMSCRIPTEN AND NOT KOTONOHA_WASM_SHARED_ENGINE)' in cmake_lib
    assert '"SHELL:-sSIDE_MODULE=1"' in cmake_lib
    assert '"SHELL:-sASYNCIFY=1"' in cmake_lib
    assert 'LIBRARY_OUTPUT_DIRECTORY "${KOTONOHA_OUTPUT_LIB_DIR}"' in cmake_lib
    assert 'WebAssetFetch\\.cpp' in cmake_lib
    assert 'WebMainExports\\.cpp' in cmake_lib
    assert '"SHELL:-sMAIN_MODULE=2"' in cmake_app
    assert '"SHELL:-sASYNCIFY_IMPORTS=@' in cmake_app
    assert 'WebMainExports.cpp' in cmake_app
    assert 'WebAssetFetch.cpp' in cmake_app
    assert 'KotonohaWasmAsyncifyImports' in cmake_app
    assert '"@WASM_SHARED_ENGINE@" === "true"' in html
    assert '../libs/@KOTONOHA_WASM_SHARED_FILENAME@' in html
    assert 'KotonohaSharedWasmCache.js' in html
    assert 'KotonohaCompleteBrowserFetch' in bridge
    assert '#ifndef KOTONOHA_WASM_SHARED_ENGINE' in provider
    assert 'request.headers.has("range")' in cache
    assert 'request.url !== url' in cache

    with tempfile.TemporaryDirectory(prefix="kotonoha-wasm-shared-") as temp:
        root = Path(temp)
        lib = root / "outputs/libs/libkotonoha-test.wasm"
        game = root / "outputs/Example/Example.js"
        lib.parent.mkdir(parents=True)
        game.parent.mkdir(parents=True)
        lib.write_bytes(side_module((
            "KotonohaDebugApi", "_ZN8Kotonoha6Engine5EventEv",
            "asyncify_start_unwind",
        )))
        game.write_text('const dynamicLibraries=["libkotonoha-test.wasm"];')
        game.with_suffix(".wasm").write_bytes(MAGIC)
        validate(game, lib)
        assert "dylink.0" in custom_sections(lib.read_bytes())

        listing = root / "shared-asyncify-imports.json"
        generate(lib, listing)
        names = json.loads(listing.read_text())
        assert "_ZN8Kotonoha6Engine5EventEv" in names
        assert "KotonohaDebugApi" in names
        assert "emscripten_sleep" in names
        assert "asyncify_start_unwind" not in names
        assert names == sorted(set(names))

        lib.write_bytes(side_module(("KotonohaDebugApi",), dynamic=False))
        try:
            validate(game, lib)
        except WasmValidationError as error:
            assert "dylink" in str(error)
        else:
            raise AssertionError("An ordinary non-dynamic wasm cannot pass")

        lib.write_bytes(MAGIC)
        try:
            generate(lib, listing)
        except WasmValidationError:
            pass
        else:
            raise AssertionError("No dynamic engine exports must fail")
    print("Kotonoha dynamic WASM link contract and parser tests passed.")


if __name__ == "__main__":
    run()
