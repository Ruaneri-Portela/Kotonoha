#!/usr/bin/env python3
"""Source-level regression checks for debug applet bridges.

This checks API wiring only. It is not a C++ compilation, runtime playback,
a game-data test, or a claim of full per-platform compatibility.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

CHECKS = {
    "lib/include/Kotonoha/core/AssetProvider.hpp": (
        "dumpAsset(", "AssetSource source = AssetSource::Effective",
    ),
    "lib/src/core/AssetProvider.cpp": (
        "AssetProvider::dumpAsset(", "Filesystem::OutputFile",
        "openStream(id)", "read(id, source)",
    ),
    "lib/src/debug/DebugOverlay.cpp": (
        "Dump raw asset to Documents", "GetAssets().dumpAsset(",
        "Open in Voice Dubbing", "dubbingApplet->openAsset(",
        "GetAssets().recentAudio()", "GetRuntime().debugSceneAudio()",
    ),
    "lib/include/Kotonoha/core/Runtime.hpp": (
        "activeTranslationKeys()", "debugSceneAudio()",
        "debugOverlayAssetChanged(",
    ),
    "lib/src/debug/VoiceDubbingApplet.cpp": (
        "VoiceDubbingApplet::openAsset(",
        "GetRuntime().debugSceneAudio()",
        "GetAssets().recentAudio()",
        "GetTranslation().text(",
        "GetDebug().trackActiveTranslation()",
        "debugOverlayAssetChanged(",
    ),
    "lib/tests/AssetProviderTests.cpp": (
        "dumpProviderAssets()", "AssetSource::Original",
        "streaming.dumpAsset(", "Never overwrite existing export",
    ),
    "games/luca-system/src/LucaSystemRuntime.cpp": (
        "LucaSystemRuntime::activeTranslationKeys()",
        "LucaSystemRuntime::debugSceneAudio()",
        "GetTranslation().text(0,",
    ),
    "games/summer-radish-vacation/src/SummerRuntime.cpp": (
        "SummerRuntime::activeTranslationKeys()",
        "SummerRuntime::debugSceneAudio()",
        "translated(original)", "translated(meoChoices[i].first)",
        "SummerRuntime::translationSources()",
    ),
    "games/doki-doki-literature-club/src/DdlcRuntime.cpp": (
        "GameRuntime::activeTranslationKeys()",
        "session_->dialogueSourceText()",
    ),
    "games/reallive/src/RealLiveRuntime.cpp": (
        "RealLiveRuntime::activeTranslationKeys()",
        "RealLiveRuntime::debugSceneAudio()",
    ),
    "launcher/CMakeLists.txt": (
        '"MEO|summer_radish_vacation"',
        '"KATAWA|doki_doki_literature_club"',
    ),
}
failures = []
for filename, required in CHECKS.items():
    path = ROOT / filename
    if not path.is_file():
        failures.append(f"{filename}: missing file")
        continue
    body = path.read_text(encoding="utf-8")
    for token in required:
        if token not in body:
            failures.append(f"{filename}: missing integration marker {token!r}")
for problem in failures:
    print(f"[Debug bridge] {problem}", file=sys.stderr)
if failures:
    raise SystemExit(1)
print(f"[Debug bridge] {len(CHECKS)} source files checked; compile/runtime validation still required")
