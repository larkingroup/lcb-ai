#!/usr/bin/env python3
"""Package an already tested native Linux build without models or user data."""
import argparse
import hashlib
from pathlib import Path
import shutil
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def package(build, output):
    name = 'lcb-ai-v0.10-linux-x86_64'
    library = build / 'liblcb_native.so'
    if not library.is_file():
        raise SystemExit('Build the native library first: cmake --build build-linux')
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        app = Path(temporary) / name
        (app / 'src/linux').mkdir(parents=True)
        (app / 'assets').mkdir()
        (app / 'licenses').mkdir()
        shutil.copy2(library, app / library.name)
        for filename in ('app.py', 'backend.py', 'cli.py'):
            shutil.copy2(ROOT / 'src/linux' / filename, app / 'src/linux' / filename)
        shutil.copy2(ROOT / 'assets/lcb-icon.svg', app / 'assets/lcb-icon.svg')
        shutil.copy2(ROOT / 'vendor/cjson/LICENSE', app / 'licenses/cJSON-LICENSE')
        shutil.copy2(ROOT / 'README.md', app / 'README.md')
        for executable, script in (('lcb-ai', 'app.py'), ('lcb-cli', 'cli.py')):
            launcher = app / executable
            launcher.write_text('#!/bin/sh\n'
                'app_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1\n'
                'export LCB_NATIVE_LIBRARY="$app_root/liblcb_native.so"\n'
                f'exec python3 "$app_root/src/linux/{script}" "$@"\n')
            launcher.chmod(0o755)
        (app / 'RUNNING.txt').write_text(
            'LCB-AI v0.10 — Linux x86_64\n\n'
            'Built and tested on Fedora 44 KDE.\n'
            'Requires system Python 3 and PySide6 (Fedora: sudo dnf install python3-pyside6).\n'
            'Extract this whole folder, then run ./lcb-ai. Do not move the launcher alone.\n'
            'Choose a native Linux llama-server and GGUF model, then Load model.\n'
            'The engine and models are separate downloads; llama.cpp b10566 was tested.\n'
            'Chats and settings live in your XDG data directory, not this folder.\n'
            'See README.md for source build instructions and data migration.\n')
        archive = output / (name + '.tar.gz')
        with tarfile.open(archive, 'w:gz') as tar:
            tar.add(app, arcname=name)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / 'SHA256SUMS.txt').write_text(f'{digest}  {archive.name}\n')
    print(archive)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-linux')
    parser.add_argument('--output', type=Path, default=ROOT / 'local/release-v0.10')
    args = parser.parse_args()
    package(args.build_dir.resolve(), args.output.resolve())
