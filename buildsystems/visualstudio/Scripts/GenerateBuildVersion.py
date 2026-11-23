#!/usr/bin/env python3
"""Generate the shared version header for the Visual Studio build.

Adapted from buildsystems/xcode/Scripts/GenerateBuildVersion.py. The output is
checked into generated/ so a fresh clone can compile before the first build;
this script refreshes it from Git and preserves the timestamp when unchanged.
"""
import argparse
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def git(*args):
    try:
        return subprocess.check_output(
            ['git', '-C', str(ROOT), *args], stderr=subprocess.DEVNULL,
            text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', default=str(Path(__file__).resolve().parents[1] / 'generated'))
    args = parser.parse_args()
    output = Path(args.out) / 'Kotonoha/core/BuildVersion.hpp'

    match = re.search(r'project\(Kotonoha\s+VERSION\s+([0-9.]+)',
                      (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8'))
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
    content = (ROOT / 'buildsystems/cmake/BuildVersion.hpp.in').read_text(encoding='utf-8')
    for key, value in values.items():
        content = content.replace(f'@{key}@', value)
    content = content.replace('during CMake configuration', 'during the Visual Studio build')
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding='utf-8') != content:
        output.write_text(content, encoding='utf-8')


if __name__ == '__main__':
    main()
