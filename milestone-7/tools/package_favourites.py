#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Package a compiled, existing-install update. No SDK or factory libraries included."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--commit', required=True, help='Exact source commit of the compiled files')
parser.add_argument('--base', required=True, help='Upstream base commit')
args = parser.parse_args()
for value in (args.commit, args.base):
    if len(value) != 40 or any(c not in '0123456789abcdef' for c in value):
        parser.error('Commit IDs must be full lowercase SHA-1 values')
out = args.output.resolve()
if out.exists():
    parser.error('Output directory already exists; use a new directory to preserve prior releases')
archive = out.parent / (out.name + '.zip')
if archive.exists():
    parser.error('Archive already exists; choose a new output name')
payload = ['nbt-aad', 'nbt-aa-favourites']
plugin = ['libNbtAad.so', 'NbtAad.manifest', 'NbtAad.xml', 'NbtAad_ALL_Texts.zip', 'NbtAad_ALL_Images.zip']
sources = {name: root / name for name in payload}
sources.update({'release/' + name: root / 'rhmiapp' / name for name in plugin})
for name, path in sources.items():
    if not path.is_file() or not path.stat().st_size:
        parser.error('Missing built file: ' + str(path))
    if name in payload or name.endswith('.so'):
        data = path.read_bytes()[:20]
        if data[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 40:
            parser.error('Not a 32-bit little-endian ARM ELF: ' + str(path))
out.mkdir(parents=True)
(out / 'release').mkdir()
for name, path in sources.items():
    shutil.copyfile(path, out / name)
for name in ('favourites-update.sh', 'favourites-restore.sh'):
    # A Windows checkout may use CRLF; the QNX shell requires LF.
    (out / name).write_text((root / 'tools' / name).read_text(), newline='\n')
readme = (root / 'FAVOURITE-KEYS.md').read_text().replace('../LICENSE.md', 'LICENSE.md')
readme = readme.replace('](update.md)', '](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/blob/main/milestone-7/update.md)')
(out / 'README.md').write_text(readme)
shutil.copyfile(root.parent / 'LICENSE.md', out / 'LICENSE.md')
metadata = {
    'name': 'BMW Favorite Keys for NBT-AAD', 'source_commit': args.commit,
    'upstream_base': args.base, 'built_for': 'QNX SDP 6.5 ARMv7 little-endian',
    'reference_platform': 'BMW NBT Evo ID6, 1440x540 or 1280x480/1180x480 touchscreen, existing NBT-AAD installation',
    'validation': 'QNX cross-build and host tests; latest-upstream port awaits car validation',
    'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'files': {name: {'bytes': (out / name).stat().st_size,
                     'sha256': hashlib.sha256((out / name).read_bytes()).hexdigest()}
              for name in sources},
}
(out / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
# POSIX cksum is available on the head unit even when sha256sum is not.
with (out / 'PAYLOAD.cksum').open('w', newline='\n') as log:
    for name in sources:
        result = subprocess.run(['cksum', name], cwd=out, check=True, text=True, capture_output=True)
        log.write(result.stdout)
with (out / 'SHA256SUMS.txt').open('w', newline='\n') as log:
    for path in sorted(out.rglob('*')):
        if path.is_file() and path.name != 'SHA256SUMS.txt':
            log.write(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.relative_to(out).as_posix() + '\n')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for path in sorted(out.rglob('*')):
        if path.is_file():
            z.write(path, path.relative_to(out).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    for name, info in metadata['files'].items():
        assert hashlib.sha256(z.read(name)).hexdigest() == info['sha256']
print(json.dumps({'archive': str(archive), 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(), 'payload_files': len(sources)}))
