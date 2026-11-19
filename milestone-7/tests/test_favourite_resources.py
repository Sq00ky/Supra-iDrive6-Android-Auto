#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Catch resource collisions when rebasing favourites onto newer settings pages."""
import ast
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile

root = Path(__file__).resolve().parents[1]
app = ET.parse(root / 'rhmiapp/NbtAad.xml').getroot().find('pluginApp')
states = app.find('hmiStates')
assert [s.get('id') for s in states] == ['10', '12', '13', '11', '14', '15', '16', '17']
assert states[5].get('text') == 'Version'
assert states[6].get('text') == 'BMW Favorite Keys'
assert states[7].get('parentId') == '16'
for section in ('actions', 'models', 'events'):
    ids = [n.get('id') for n in app.find(section)]
    assert len(ids) == len(set(ids)), ('duplicate ID', section)
state_ids = {s.get('id') for s in states}
for n in app.find('actions'):
    if n.tag == 'hmiAction':
        assert n.get('target') in state_ids
models = {m.get('id') for m in app.find('models')}
for n in app.iter():
    for key in ('model', 'textModel', 'imageModel', 'previewModel', 'targetModel'):
        if n.get(key):
            assert n.get(key) in models, (n.tag, key, n.get(key))
# Python dict literals silently overwrite duplicate keys: detect them in source too.
tree = ast.parse((root / 'rhmiapp/make_db.py').read_text())
for n in ast.walk(tree):
    if isinstance(n, ast.Dict):
        keys = [k.value for k in n.keys if isinstance(k, ast.Constant)]
        assert len(keys) == len(set(keys)), 'duplicate text ID in make_db.py'
with zipfile.ZipFile(root / 'rhmiapp/NbtAad_ALL_Texts.zip') as z:
    for name in z.namelist():
        texts = dict(line.split('=', 1) for line in z.read(name).decode().splitlines())
        assert texts['48'] == 'BMW Favorite Keys'
        assert texts['57'] == 'Show Favorites Keys Bar'
        assert texts['58'] == 'bar color'
        assert texts['59'] == 'custom color'
        assert texts['45'] != texts['48']
with zipfile.ZipFile(root / 'rhmiapp/NbtAad_ALL_Images.zip') as z:
    assert z.read('55001.png') == (root / 'plugin/res/aa_row_icon_63x54.png').read_bytes()
print('test_favourite_resources: all passed')
