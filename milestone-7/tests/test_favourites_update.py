#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Exercise the real updater/restore flow against isolated files and mocked QNX probes."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
names = ['libNbtAad.so', 'NbtAad.manifest', 'NbtAad.xml', 'NbtAad_ALL_Texts.zip', 'NbtAad_ALL_Images.zip']
with tempfile.TemporaryDirectory(prefix='nbt-update-test-') as tmp:
    tmp = Path(tmp)
    package, aa, plugin = tmp / 'package', tmp / 'opt/aa', tmp / 'opt/hmi/ID5/data/ro/Plugins/NbtAad'
    for path in (package / 'release', aa, plugin, tmp / 'bin', tmp / 'opt/sys/bin', tmp / 'opt/sys/lib', tmp / 'tmp'):
        path.mkdir(parents=True, exist_ok=True)
    for name, text in {'uname': 'case "$1" in -s) echo QNX;; -m) echo armv7;; esac',
                       'id': 'echo 0', 'pidin': 'cat "'+str(tmp / 'pids')+'"'}.items():
        path = tmp / 'bin' / name
        path.write_text('#!/bin/sh\n' + text + '\n')
        path.chmod(0o755)
    (tmp / 'pids').write_text('')
    lm = tmp / 'opt/sys/bin/LayerManagerControl'
    lm.write_text('#!/bin/sh\necho "original size: x=1440, y=540"\n')
    lm.chmod(0o755)
    (tmp / 'opt/sys/lib/libilmClient.gpp.so.1').touch()
    (aa / 'nbt-aad').write_text('old daemon')
    for name in names:
        (plugin / name).write_text('old ' + name)
    for name in ['nbt-aad', 'nbt-aa-favourites'] + ['release/' + n for n in names]:
        (package / name).write_text('new ' + name)
    crc = ''.join(subprocess.check_output(['cksum', name], cwd=package, text=True)
                  for name in ['nbt-aad', 'nbt-aa-favourites'] + ['release/' + n for n in names])
    (package / 'PAYLOAD.cksum').write_text(crc)
    # Rewrite absolute target roots only in the test copy, never in shipping scripts.
    for name in ('favourites-update.sh', 'favourites-restore.sh'):
        script = (root / 'tools' / name).read_text().replace('/tmp/nbt-', str(tmp / 'tmp') + '/nbt-').replace('/opt/', str(tmp) + '/opt/')
        (package / name).write_text(script)
    env = dict(os.environ, PATH=str(tmp / 'bin') + ':' + os.environ['PATH'])
    def run(name, *args, ok=True):
        p = subprocess.run(['sh', str(package / name), *args], env=env, text=True, capture_output=True)
        assert (p.returncode == 0) == ok, (args, p.stdout, p.stderr)
        return p
    update = 'favourites-update.sh'
    run(update, '--check')
    # 1280x480 is the Supra panel and is supported (rhmiapp-98); an unknown panel is still refused and the
    # message names the sizes that do work.
    lm.write_text('#!/bin/sh\necho "original size: x=1280, y=480"\n')
    run(update, '--check')
    lm.write_text('#!/bin/sh\necho "original size: x=1024, y=600"\n')
    assert '1280x480' in run(update, '--check', ok=False).stderr
    lm.write_text('#!/bin/sh\necho "original size: x=1440, y=540"\n')
    (package / 'nbt-aad').write_text('corrupt')
    assert 'Checksum mismatch' in run(update, '--check', ok=False).stderr
    (package / 'nbt-aad').write_text('new nbt-aad')
    (tmp / 'pids').write_text('123 /opt/aa/nbt-aad\n')
    assert 'Stop Android Auto' in run(update, '--install', ok=False).stderr
    assert (aa / 'nbt-aad').read_text() == 'old daemon'
    (tmp / 'pids').write_text('')
    run(update, '--install')
    backups = list(aa.glob('favourites-backup-*'))
    assert len(backups) == 1
    assert (aa / 'nbt-aad').read_text() == 'new nbt-aad'
    assert (aa / 'nbt-aa-favourites').stat().st_mode & 0o111
    assert (backups[0] / 'nbt-aad').read_text() == 'old daemon'
    for name in names:
        assert (plugin / name).read_text() == 'new release/' + name
    run('favourites-restore.sh', str(backups[0]))
    assert (aa / 'nbt-aad').read_text() == 'old daemon'
    assert not (aa / 'nbt-aa-favourites').exists()
    for name in names:
        assert (plugin / name).read_text() == 'old ' + name
    (aa / 'nbt-aa-favourites').write_text('old helper')
    run(update, '--install')
    second = [b for b in aa.glob('favourites-backup-*') if b != backups[0]]
    assert len(second) == 1
    run('favourites-restore.sh', str(second[0]))
    assert (aa / 'nbt-aa-favourites').read_text() == 'old helper'
print('test_favourites_update: all passed')
