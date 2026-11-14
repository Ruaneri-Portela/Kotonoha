#!/usr/bin/env python3
"""Generate the shared version header for a native Xcode target."""
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[3]
output = Path(os.environ['DERIVED_FILE_DIR']) / 'Kotonoha/core/BuildVersion.hpp'


def git(*args):
    try:
        return subprocess.check_output(
            ['git', '-C', str(root), *args], stderr=subprocess.DEVNULL,
            text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return ''


match = re.search(r'project\(Kotonoha\s+VERSION\s+([0-9.]+)',
                  (root / 'CMakeLists.txt').read_text())
if not match:
    raise RuntimeError('Cannot read Kotonoha project version')
base = match.group(1)
commit = git('rev-parse', '--verify', 'HEAD')
branch, short, dirty = 'unknown', 'unknown', 0
if re.fullmatch(r'[0-9a-fA-F]+', commit):
    short = commit[:12]
    branch = (git('symbolic-ref', '--quiet', '--short', 'HEAD')
              or os.environ.get('GITHUB_HEAD_REF')
              or os.environ.get('GITHUB_REF_NAME') or 'detached')
    dirty = int(bool(git('status', '--porcelain', '--untracked-files=no')))
else:
    commit = 'unknown'
branch = re.sub(r'[^A-Za-z0-9._/-]', '_', branch)
label = re.sub(r'[^A-Za-z0-9.-]', '-', branch) or 'unknown'
full = f'{base}+{label}.{short}' + ('.dirty' if dirty else '')
values = dict(CMAKE_PROJECT_VERSION=base, KOTONOHA_VERSION_FULL=full,
              KOTONOHA_GIT_BRANCH=branch, KOTONOHA_GIT_COMMIT=commit,
              KOTONOHA_GIT_SHORT=short, KOTONOHA_GIT_DIRTY=str(dirty))
content = (root / 'buildsystems/cmake/BuildVersion.hpp.in').read_text()
for key, value in values.items():
    content = content.replace(f'@{key}@', value)
content = content.replace('during CMake configuration', 'during the Xcode build')
# Preserve the timestamp when metadata is unchanged to avoid recompilation.
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != content:
    output.write_text(content)
