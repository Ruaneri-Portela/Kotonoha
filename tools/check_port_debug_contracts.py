#!/usr/bin/env python3
"""Audit every registered Kotonoha port's shared debug/translation bridge.

This is an explicit *source contract* check. It does not compile or execute
the native engines, does not need commercial game data, and does not prove
audio codec / scene compatibility. Run from anywhere:
    python3 tools/check_port_debug_contracts.py
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "buildsystems/xcode/ProjectManifest.json"

# The shared runtime implementation is intentional for game variants. The
# Example game intentionally has no native scripts (VNManager fallback).
RUNTIMES = {
    "artemis": "games/artemis/src/ArtemisRuntime.cpp",
    "amagami": "games/amagami/src/AmagamiRuntime.cpp",
    "doki-doki-literature-club": "games/doki-doki-literature-club/src/DdlcRuntime.cpp",
    "example": None,
    "katawa-shoujo": "games/doki-doki-literature-club/src/DdlcRuntime.cpp",
    "kirikiri": "games/kirikiri/src/KirikiriRuntime.cpp",
    "luca-system": "games/luca-system/src/LucaSystemRuntime.cpp",
    "meo": "games/summer-radish-vacation/src/SummerRuntime.cpp",
    "n2system": "games/n2system/src/N2Runtime.cpp",
    "pmail": "games/pmail/src/PmailRuntime.cpp",
    "ponscripter": "games/ponscripter/src/PonscripterRuntime.cpp",
    "reallive": "games/reallive/src/RealLiveRuntime.cpp",
    "school-days": "games/school-days/src/SchoolDaysRuntime.cpp",
    "snow-radish-vacation": "games/snow-radish-vacation/src/SnowRuntime.cpp",
    "summer-radish-vacation": "games/summer-radish-vacation/src/SummerRuntime.cpp",
    "zero-no-tsukaima": "games/zero-no-tsukaima/src/ZeroRuntime.cpp",
}

# All native motors should implement each capability through Runtime. Their
# game-specific storage and VM APIs never belong in shared ImGui code.
REQUIRED_RUNTIME_METHODS = (
    "::debugNodes() const",
    "::translationSources() const",
    "::activeTranslationKeys() const",
    "::debugSceneAudio()",
)
COMMON = {
    "lib/include/Kotonoha/core/Runtime.hpp": (
        "debugNodeCatalogKey() const",
        "debugNodes() const",
        "debugSceneAudio()",
        "translationSources() const",
        "activeTranslationKeys() const",
    ),
    "lib/src/core/AssetProvider.cpp": (
        "AssetProvider::list(", "std::sort(result.begin(), result.end());",
    ),
    "lib/src/debug/DebugOverlay.cpp": (
        "drawAssets({});",
        "GetAssets().list()",
        "debugNodeCatalogKey()",
        "lastTranslationCatalogKey",
        "GetRuntime().debugSceneAudio()",
    ),
    "lib/src/debug/VoiceDubbingApplet.cpp": (
        "List provider audio",
        "GetAssets().list()",
        "DebugAssetCatalog::isAudio",
        "GetRuntime().debugSceneAudio()",
        "GetTranslation().text(",
    ),
    "lib/include/Kotonoha/debug/DebugAssetCatalog.hpp": (
        "isAudio(std::string_view id)",
        '"voice:"', '".snd"',
    ),
    "modules/providers/amagami/src/Assets.cpp": (
        "AmagamiAssets::listAsset(",
        "source_->voiceAssetIds()",
    ),
    "modules/providers/pak/src/LucaSystemAssets.cpp": (
        "LucaSystemAssets::listAsset(",
        'numeric(voiceIdToPak_, "voice")',
        'numeric(imageIdToPak_, "image")',
    ),
}

def read(path: str) -> str | None:
    source = ROOT / path
    return source.read_text(encoding="utf-8") if source.is_file() else None


def main() -> int:
    issues: list[str] = []
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    registered = set(manifest["games"].values())
    if registered != set(RUNTIMES):
        issues.append(
            "Port matrix differs from ProjectManifest: "
            f"missing={sorted(registered - set(RUNTIMES))}, "
            f"stale={sorted(set(RUNTIMES) - registered)}"
        )

    checked = set()
    for slug in sorted(registered & set(RUNTIMES)):
        source_path = RUNTIMES[slug]
        if source_path is None:
            # Example intentionally delegates node handling to VNManager.
            if not (ROOT / "games/example/src/DemoGame.cpp").is_file():
                issues.append("example: missing generic VNManager-based runtime")
            continue
        text = read(source_path)
        if text is None:
            issues.append(f"{slug}: missing {source_path}")
            continue
        for signature in REQUIRED_RUNTIME_METHODS:
            if signature not in text:
                issues.append(f"{slug}: missing {signature} ({source_path})")
        checked.add(source_path)

    for path, signatures in COMMON.items():
        text = read(path)
        if text is None:
            issues.append(f"missing shared bridge: {path}")
            continue
        for signature in signatures:
            if signature not in text:
                issues.append(f"{path}: missing {signature}")

    for issue in issues:
        print(f"[Port debug contracts] {issue}", file=sys.stderr)
    if issues:
        return 1
    print(
        f"[Port debug contracts] {len(registered)} games, "
        f"{len(checked)} native runtime implementations, "
        f"{len(COMMON)} shared/provider sources checked; "
        "C++ builds and commercial-data tests still required"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
