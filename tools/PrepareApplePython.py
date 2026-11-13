#!/usr/bin/env python3
"""Prepare CPython Frameworks for iOS and macOS native Xcode builds."""
import plistlib
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
XCODE_PYTHON_DIR = ROOT / 'buildsystems/xcode/python'

def prepare_ios():
    ios_dir = XCODE_PYTHON_DIR / 'iOS'
    if not ios_dir.exists():
        print('[Kotonoha] iOS Python directory not found, skipping.')
        return

    xc = ios_dir / 'Python.xcframework'
    output_dir = ROOT / 'build/iOS-deps'
    output_dir.mkdir(parents=True, exist_ok=True)

    for sdk, folder in [('iphoneos', 'ios-arm64'), ('iphonesimulator', 'ios-arm64_x86_64-simulator')]:
        native = output_dir / 'python/native' / sdk
        std = native / 'python/lib/python3.15'
        if native.exists():
            shutil.rmtree(native)
        shutil.copytree(xc / 'lib/python3.15', std)
        dynamic = std / 'lib-dynload'
        dynamic.mkdir(exist_ok=True)
        platform = xc / folder
        extdirs = sorted(platform.glob('lib-*/python3.15/lib-dynload'))
        if not extdirs:
            print(f'[Kotonoha] Note: No Python extension dirs in {platform}')
            continue
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
            metadata = dict(
                CFBundleExecutable=module,
                CFBundleIdentifier=f'org.python.extensions.{module.replace("_", "-")}',
                CFBundleName=module,
                CFBundlePackageType='FMWK',
                CFBundleVersion='1',
                MinimumOSVersion='15.0',
                CFBundleSupportedPlatforms=['iPhoneSimulator' if sdk == 'iphonesimulator' else 'iPhoneOS']
            )
            (framework / 'Info.plist').write_bytes(plistlib.dumps(metadata))
            marker = dynamic / (filename.removesuffix('.so') + '.fwork')
            marker.write_text(f'Frameworks/{module}.framework/{module}')
            (framework / f'{module}.origin').write_text(f'python/lib/python3.15/lib-dynload/{marker.name}')
    print('[Kotonoha] Prepared iOS CPython SDK successfully.')

def prepare_macos():
    macos_dir = XCODE_PYTHON_DIR / 'macOS'
    if not macos_dir.exists():
        print('[Kotonoha] macOS Python directory not found, skipping.')
        return

    xc = macos_dir / 'Python.xcframework'
    output_dir = ROOT / 'build/macOS-deps'
    output_dir.mkdir(parents=True, exist_ok=True)

    native = output_dir / 'python/native/macosx'
    if native.exists():
        shutil.rmtree(native)

    # Copy Python.framework for macOS
    mac_framework = xc / 'macos-arm64_x86_64/Python.framework'
    if mac_framework.exists():
        dest_framework = output_dir / 'Frameworks/Python.framework'
        dest_framework.parent.mkdir(parents=True, exist_ok=True)
        if dest_framework.exists():
            shutil.rmtree(dest_framework)
        shutil.copytree(mac_framework, dest_framework, symlinks=True)
        print(f'[Kotonoha] Copied macOS Python.framework to {dest_framework}')

if __name__ == '__main__':
    print('[Kotonoha] Preparing CPython Frameworks for Apple platforms...')
    prepare_ios()
    prepare_macos()
    print('[Kotonoha] Done!')
