#!/usr/bin/env python3
"""Prepare the pinned BeeWare iOS CPython SDK for native Xcode builds."""
import argparse
import hashlib
import plistlib
import shutil
import subprocess
import tarfile
from pathlib import Path

SHA256 = '429080dcfab3778b75bbc6548cbfd754dc943cfc6092a92b72022541ff3212a0'
ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--archive', type=Path, required=True)
p.add_argument('--output', type=Path, default=ROOT / 'build/iOS-deps')
a = p.parse_args()
with a.archive.open('rb') as f:
    if hashlib.file_digest(f, 'sha256').hexdigest() != SHA256:
        raise SystemExit('Python SDK SHA-256 does not match the pinned 3.15-b3 archive')
a.output.mkdir(parents=True, exist_ok=True)
with tarfile.open(a.archive) as tar:
    tar.extractall(a.output, filter='data')
xc = a.output / 'Python.xcframework'
for sdk, folder in [('iphoneos', 'ios-arm64'), ('iphonesimulator', 'ios-arm64_x86_64-simulator')]:
    native = a.output / 'python/native' / sdk
    std = native / 'python/lib/python3.15'
    if native.exists():
        shutil.rmtree(native)
    shutil.copytree(xc / 'lib/python3.15', std)
    dynamic = std / 'lib-dynload'
    dynamic.mkdir(exist_ok=True)
    platform = xc / folder
    extdirs = sorted(platform.glob('lib-*/python3.15/lib-dynload'))
    if not extdirs:
        raise SystemExit(f'No Python extensions in {platform}')
    files = {}
    for directory in extdirs:
        for library in directory.glob('*.so'):
            files.setdefault(library.name, []).append(library)
    for filename, libraries in files.items():
        module = filename.split('.')[0]
        framework = native / 'Frameworks' / f'{module}.framework'
        framework.mkdir(parents=True)
        binary = framework / module
        subprocess.run(['xcrun', 'lipo', '-create', *map(str, libraries), '-output', str(binary)], check=True)
        subprocess.run(['xcrun', 'install_name_tool', '-id', f'@rpath/{module}.framework/{module}', str(binary)], check=True)
        metadata = dict(CFBundleExecutable=module, CFBundleIdentifier=f'org.python.extensions.{module.replace("_", "-")}', CFBundleName=module, CFBundlePackageType='FMWK', CFBundleVersion='1', MinimumOSVersion='15.0', CFBundleSupportedPlatforms=['iPhoneSimulator' if sdk == 'iphonesimulator' else 'iPhoneOS'])
        (framework / 'Info.plist').write_bytes(plistlib.dumps(metadata))
        marker = dynamic / (filename.removesuffix('.so') + '.fwork')
        marker.write_text(f'Frameworks/{module}.framework/{module}')
        (framework / f'{module}.origin').write_text(f'python/lib/python3.15/lib-dynload/{marker.name}')
print(f'Prepared {xc} and both SDK resource trees')
