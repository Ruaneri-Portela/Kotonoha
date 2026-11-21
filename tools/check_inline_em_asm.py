#!/usr/bin/env python3
"""Detect JavaScript operators broken by C++ formatting inside EM_ASM macros.

Emscripten processes each EM_ASM/EM_ASM_INT/EM_ASM_DOUBLE body as JavaScript,
but C++ formatters may change '===' to '== =' (and '!==' to '!= ='), which
Acorn rejects late in the WASM link for every game. Keep this independent
of Emscripten and commercial game assets so CI can catch mistakes early.
"""
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (ROOT / "lib" / "src", ROOT / "lib" / "include",
                ROOT / "games", ROOT / "modules")
ASM = re.compile(r"\bEM_ASM(?:_INT|_DOUBLE)?\s*\(")
BAD_OPERATOR = re.compile(r"===|==\s+=|!==|!=\s+=")
EXTENSIONS = {".cpp", ".hpp", ".h", ".cc", ".cxx", ".mm"}


def strip_comments_and_strings(source: str) -> str:
    """Keep code positions/newlines but mask C++/inline-JS comments and quotes."""
    chars = list(source)
    i = 0
    n = len(chars)
    while i < n:
        if source.startswith("//", i):
            end = source.find("\n", i)
            if end < 0:
                end = n
        elif source.startswith("/*", i):
            close = source.find("*/", i + 2)
            end = n if close < 0 else close + 2
        elif source[i] in ("'", '"', "`"):
            quote = source[i]
            end = i + 1
            while end < n:
                if source[end] == "\\":
                    end += 2
                elif source[end] == quote:
                    end += 1
                    break
                else:
                    end += 1
            end = min(end, n)
        else:
            i += 1
            continue
        for j in range(i, end):
            if chars[j] != "\n":
                chars[j] = " "
        i = end
    return "".join(chars)


def issues(source: str) -> tuple[int, list[tuple[int, str]]]:
    clean = strip_comments_and_strings(source)
    total = 0
    problems: list[tuple[int, str]] = []
    for match in ASM.finditer(clean):
        open_paren = match.end() - 1
        depth = 1
        end = open_paren + 1
        while end < len(clean) and depth:
            char = clean[end]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            end += 1
        if depth:
            problems.append((source.count("\n", 0, match.start()) + 1,
                             "unclosed EM_ASM call"))
            continue
        total += 1
        snippet = clean[open_paren + 1:end - 1]
        for bad in BAD_OPERATOR.finditer(snippet):
            line = source.count("\n", 0, open_paren + 1 + bad.start()) + 1
            problems.append((line, "unsafe JavaScript strict-equality operator "
                                   "(use a C++ formatter-safe alternative)"))
    return total, problems


def main() -> int:
    count = 0
    failures: list[str] = []
    for source_root in SOURCE_ROOTS:
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix not in EXTENSIONS:
                continue
            total, problems = issues(path.read_text(encoding="utf-8"))
            count += total
            failures.extend(f"{path.relative_to(ROOT)}:{line}: {reason}"
                            for line, reason in problems)
    assert issues("EM_ASM({ if (x == = 1) {} });")[1]
    assert issues("EM_ASM({ if (x === 1) {} });")[1]
    assert issues("EM_ASM({ if (x != = 1) {} });")[1]
    assert not issues("EM_ASM({ // === == =\nif (x == 1) {} });")[1]
    assert not issues("EM_ASM({ const s = '==='; });")[1]
    if count == 0:
        failures.append("No inline WebAssembly macros found: scan is incomplete")
    for failure in failures:
        print(f"[Kotonoha EM_ASM] {failure}", file=sys.stderr)
    if failures:
        return 1
    print(f"[Kotonoha EM_ASM] Verified {count} inline browser JS macros "
          "across the engine and game ports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
