#!/usr/bin/env python3
"""Source-only checks for the shared SDL3 bridge and desktop packaging."""
from pathlib import Path
import json
import re

root = Path(__file__).resolve().parents[1]
games = sorted((root / "games").glob("*/src/main.cpp"))
expected_slugs = set(json.loads(
    (root / "buildsystems/xcode/ProjectManifest.json").read_text(
        encoding="utf-8"))["games"].values())
actual_slugs = {entry.parents[1].name for entry in games}
assert actual_slugs == expected_slugs, (
    f"SDL game entrypoints differ from manifest: missing="
    f"{sorted(expected_slugs - actual_slugs)}, extra="
    f"{sorted(actual_slugs - expected_slugs)}")
for entry in games:
    code = entry.read_text(encoding="utf-8")
    assert "#include <Kotonoha/core/SDLCallbackApp.hpp>" in code, entry
    assert code.count("KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)") == 1, entry
    for method in ("init", "event", "iterate", "quit"):
        assert f"KotonohaPortCallbacks::{method}(" in code, (entry, method)
    assert not re.search(r"^(?:SDL_AppResult|void) SDL_App(?:Init|Event|Iterate|Quit)\(",
                         code, re.MULTILINE), entry
    assert "SDL_SetAppMetadata(" not in code, (
        entry, "Use KOTONOHA_SET_APP_METADATA instead of literal versions")
    if "KOTONOHA_SET_APP_METADATA(" in code:
        assert '"0.1.0"' not in code, (entry, "Hardcoded SDL metadata version")

for name in ("PmailStage", "PmailTitleMenu", "PmailTextBox"):
    directory = root / "games/pmail/src"
    hdr = (directory / f"{name}.hpp").read_text(encoding="utf-8")
    cpp = (directory / f"{name}.cpp").read_text(encoding="utf-8")
    assert "TexturePtr" in hdr and "SDL_DestroyTexture(" not in cpp, name

common = (root / "lib/include/Kotonoha/core/SDL.hpp").read_text(encoding="utf-8")
for handle in ("SDLMemoryPtr", "SDLArrayPtr", "AudioDeviceHandle",
               "TexturePtr", "SurfacePtr", "GamepadPtr", "IOStreamPtr"):
    assert handle in common, handle

app = (root / "buildsystems/cmake/games/KotonohaApp.cmake").read_text(encoding="utf-8")
app += (root / "buildsystems/cmake/packaging/KotonohaAppPackaging.cmake").read_text(encoding="utf-8")
deploy = (root / "buildsystems/cmake/packaging/CopyRuntimeLibraries.cmake").read_text(encoding="utf-8")
cpack = (root / "buildsystems/cmake/packaging/KotonohaCPack.cmake").read_text(encoding="utf-8")
for token in ("CMAKE_SHARED_LIBRARIES", "kotonoha_copy_runtime_libraries",
              "kotonoha_get_default_dependency_dirs"):
    assert token in app and token in deploy if token == "CMAKE_SHARED_LIBRARIES" else token in app, token
assert "file(GET_RUNTIME_DEPENDENCIES" in deploy
assert "CPACK_GENERATOR \"ZIP\"" in cpack
assert "CPACK_ARCHIVE_COMPONENT_INSTALL ON" in cpack
assert "KOTONOHA_CPACK_INCLUDE_GAME_ASSETS" in cpack
assert "KOTONOHA_WASM_INITIAL_MEMORY" in app
print(f"[Kotonoha] {len(games)} SDL game entrypoints and CPack/deploy audit OK")
