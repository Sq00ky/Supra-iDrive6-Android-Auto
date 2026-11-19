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
# Order and neighbours differ from the tree this was written on: main has added the gauge-cluster page
# since. What matters is that the favourites pages are present and wired to each other.
by_id = {s.get('id'): s for s in states}
assert len(by_id) == len(states), 'duplicate state id'
# rhmiapp-98: the favourites submenu is gone. One checkbox on the Settings page replaces it, and the car
# manufacturer choice has its own page.
assert '16' not in by_id and '17' not in by_id, 'favourites submenu should be gone'
assert by_id['15'].get('text') == 'Version'
assert by_id['18'].get('text') == 'Car manufacturer' and by_id['18'].get('parentId') == '14'
rows = [c for c in app.iter('checkbox') if c.get('text') == 'Disable Favorites Bar']
assert len(rows) == 1, 'the Disable Favorites Bar checkbox is missing'
assert len([c for c in by_id['18'].iter('radiobutton')]) == 5, 'manufacturer page needs five radios'
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
        assert '300' not in texts, 'favourite colour texts should be gone'
        assert '309' not in texts, 'the favourites submenu texts should be gone'
        assert '320' in texts, 'the status texts should be there'
        assert '320' in texts, 'the status texts should be there'
        assert texts['45'] != texts['48']
# Every text id the XML references must exist in every language file, or the HMI refuses the plugin.
import re as _re
_xml = (root / 'rhmiapp/NbtAad.xml').read_text()
_tmodels = dict(_re.findall(r'<textIdModel id="(\d+)" textId="(\d+)"', _xml))
_used = sorted({int(_tmodels[m]) for m in _re.findall(r'\b(?:textModel|model)="(\d+)"', _xml) if m in _tmodels})
with zipfile.ZipFile(root / 'rhmiapp/NbtAad_ALL_Texts.zip') as z:
    for name in z.namelist():
        have = {int(l.split('=', 1)[0]) for l in z.read(name).decode().splitlines()
                if l.split('=', 1)[0].strip().isdigit()}
        missing = [i for i in _used if i not in have]
        assert not missing, (name, 'text ids missing', missing)

with zipfile.ZipFile(root / 'rhmiapp/NbtAad_ALL_Images.zip') as z:
    assert z.read('55001.png') == (root / 'plugin/res/aa_row_icon_63x54.png').read_bytes()
print('test_favourite_resources: all passed')
