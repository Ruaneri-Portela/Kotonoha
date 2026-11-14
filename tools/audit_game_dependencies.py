#!/usr/bin/env python3
"""Reject native multimedia and Emscripten details leaking into game ports."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1] / "games"
EXTENSIONS = {".cpp", ".cc", ".c", ".h", ".hpp", ".hh", ".mm"}
BANNED_SOURCE = {
    "Emscripten": re.compile(
        r"__EMSCRIPTEN__|\bEM_ASM(?:_INT|_DOUBLE)?\b|#\s*include\s*[<\"]emscripten/|\bemscripten_[A-Za-z0-9_]+\b", re.I),
    "FFmpeg": re.compile(
        r"#\s*include\s*[<\"]libav(?:codec|format|util|filter|device|swscale|swresample)/|\bavcodec_(?:send_packet|receive_frame|open2)\b|\bavformat_(?:open_input|find_stream_info)\b", re.I),
    "libass": re.compile(
        r"#\s*include\s*[<\"]ass/ass\.h|\bass_(?:render_frame|process_data|library_init|renderer_init)\b", re.I),
}
BANNED_CMAKE = re.compile(
    r"\bEMSCRIPTEN\b|emscripten|--preload-file|-sSIDE_MODULE|-sMAIN_MODULE|\blibass\b|\bFFMPEG\b|\bffmpeg\b", re.I)

def main():
    findings = []
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file() or any(p in {"vendor", "game-assets"} for p in path.parts):
            continue
        if path.suffix not in EXTENSIONS and path.name != "CMakeLists.txt":
            continue
        rules = {"build/platform": BANNED_CMAKE} if path.name == "CMakeLists.txt" else BANNED_SOURCE
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            for label, pattern in rules.items():
                if pattern.search(line):
                    findings.append(f"{path.relative_to(ROOT)}:{number} {label}: {line.strip()[:120]}")
    if findings:
        print("\n".join(findings))
        raise SystemExit(f"{len(findings)} forbidden game-level dependencies")
    print("[Kotonoha] Games have no direct Emscripten/FFmpeg/libass implementation")

if __name__ == "__main__":
    main()
