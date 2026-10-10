#!/usr/bin/env python3
"""Package the compiled Linux executables; Python is only a build-time helper."""
import argparse
import hashlib
from pathlib import Path
import platform
import shutil
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def package(build, output):
    name = 'lcb-ai-v0.15-linux-fltk-' + platform.machine()
    for filename in ('lcb-ai', 'lcb-cli'):
        binary = build / filename
        if not binary.is_file() or binary.read_bytes()[:4] != b'\x7fELF':
            raise SystemExit('Build the native executables first: cmake --build build-linux')
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        app = Path(temporary) / name
        (app / 'assets').mkdir(parents=True)
        (app / 'licenses').mkdir()
        for filename in ('lcb-ai', 'lcb-cli'):
            shutil.copy2(build / filename, app / filename)
        shutil.copy2(ROOT / 'assets/lcb-icon.svg', app / 'assets/lcb-icon.svg')
        shutil.copy2(ROOT / 'vendor/cjson/LICENSE', app / 'licenses/cJSON-LICENSE')
        shutil.copy2(ROOT / 'vendor/fltk/LICENSE', app / 'licenses/FLTK-LICENSE')
        shutil.copy2(ROOT / 'src/linux/lcb-ai.desktop', app / 'lcb-ai.desktop')
        shutil.copy2(ROOT / 'README.md', app / 'README.md')
        (app / 'RUNNING.txt').write_text(
            'LCB-AI v0.15 — native Linux / FLTK\n\n'
            'Run ./lcb-ai. Both executables are compiled ELF binaries.\n'
            'No Python or Qt runtime is required.\n'
            'System libraries: libcurl, OpenSSL, and the display/font libraries\n'
            'selected at build time. FLTK is static in the default source build;\n'
            'builds using an installed shared FLTK also need its runtime.\n'
            'Run ldd ./lcb-ai to identify missing system libraries.\n'
            'FLTK 1.4 supports native Wayland and X11; FLTK_BACKEND=x11 selects X11\n'
            'when both backends were built.\n'
            'Choose a Linux llama-server and GGUF model, then Load.\n'
            'Engines and models are separate downloads; llama.cpp b10566 is tested.\n'
            'Chats and settings use your existing XDG data directory.\n'
            'See README.md for build instructions and data migration.\n')
        archive = output / (name + '.tar.gz')
        with tarfile.open(archive, 'w:gz') as tar:
            tar.add(app, arcname=name)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / 'SHA256SUMS.txt').write_text(f'{checksum}  {archive.name}\n')
    print(archive)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-linux')
    parser.add_argument('--output', type=Path, default=ROOT / 'local/release-native-linux')
    args = parser.parse_args()
    package(args.build_dir.resolve(), args.output.resolve())
