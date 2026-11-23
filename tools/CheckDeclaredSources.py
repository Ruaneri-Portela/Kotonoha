"""Validate native source references without requiring submodule checkout in CI.

Files owned by the repository must exist. Files inside declared, tracked Git
submodules can be absent during source-only metadata checks; their contents
are verified separately by actual native builds with initialized submodules.
"""

from functools import lru_cache
from pathlib import Path
import re
import subprocess


@lru_cache(maxsize=None)
def declared_gitlinks(root):
    root = Path(root)
    modules = root / ".gitmodules"
    if not modules.is_file():
        return frozenset()
    declared = set(re.findall(r"(?m)^\s*path\s*=\s*(\S+)\s*$",
                              modules.read_text(encoding="utf-8")))
    try:
        result = subprocess.run(
            ["git", "ls-files", "--stage", "--", ".gitmodules", *sorted(declared)],
            cwd=str(root), capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return frozenset()
    linked = set()
    for row in result.stdout.splitlines():
        metadata, _, path = row.partition("\t")
        if metadata.startswith("160000 ") and path in declared:
            linked.add(path)
    return frozenset(linked)


def source_reference_is_valid(root, source):
    """Do not mistake an uninitialized external gitlink for missing own code."""
    root = Path(root)
    candidate = Path(source)
    if candidate.is_absolute() or ".." in candidate.parts:
        return False
    relative = candidate.as_posix()
    if (root / relative).is_file():
        return True
    return any(relative.startswith(link + "/")
               for link in declared_gitlinks(root))
