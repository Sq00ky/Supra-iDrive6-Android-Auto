#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Keep the XML controls and their registered callbacks independent."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
source = (root / 'rhmiapp/nbt_aad_app.cpp').read_text()
constants = dict(re.findall(r'const unsigned\s+(\w+)\s*=\s*(\d+)\s*;', source))
registrations = re.findall(r'addAction\(self,\s*(\w+),\s*&([\w<>]+)\)', source)
handlers = {}
for action, callback in registrations:
    action = constants.get(action, action)
    assert action.isdigit(), ('unresolved action ID', action)
    assert action not in handlers, ('duplicate callback registration', action,
                                    handlers.get(action), callback)
    handlers[action] = callback

apps = ET.parse(root / 'rhmiapp/NbtAad.xml').getroot().findall('pluginApp')
for app in apps:
    actions = [a.get('id') for a in app.find('actions')]
    assert len(actions) == len(set(actions)), 'duplicate XML action declaration'
    for action in app.find('actions').findall('raAction'):
        assert action.get('id') in handlers, ('unregistered XML action', action.attrib)

main = apps[0]
cyrillic = next(c for c in main.iter('checkbox')
                if c.get('text') == 'Cyrillic street names in HUD')
vehicle = next(s for s in main.find('hmiStates') if s.get('text') == 'Car manufacturer')
auto = next(c for c in vehicle.iter('radiobutton') if c.get('text') == 'Auto')
assert cyrillic.get('action') != auto.get('action'), 'Cyrillic and manufacturer Auto share an action'
assert handlers[cyrillic.get('action')] == 'onHudCyrillicToggle'
assert handlers[auto.get('action')] == 'onVehiclePick<0>'
print('test_rhmi_actions: all passed')
