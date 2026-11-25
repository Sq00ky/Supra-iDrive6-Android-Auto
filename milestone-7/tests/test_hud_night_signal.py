#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Execute main's actual day/night forwarding block with recording endpoints."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/main.cpp').read_text()
mapper=re.search(r'static int dayNightToNight\(int v\) \{[^\n]+\}',source).group()
block=re.search(r'            int n = dayNightToNight\(g_dayNight\);.*?                night_posted = n;\n            }',source,re.S).group()
program="""#include <assert.h>
#include <stdio.h>
struct Endpoint { int calls;bool value;Endpoint():calls(0),value(false){}void postNightMode(bool v){++calls;value=v;} };
Endpoint g_session,g_kombiNav;
int g_dayNight=-1,night_posted=-1;
MAPPER
void tick(){BLOCK}
int main(){int states[]={-1,0,3,2,2,0,3,1,1,2};int calls[]={0,0,0,1,1,1,1,2,2,3};
for(int i=0;i<10;++i){g_dayNight=states[i];tick();assert(g_session.calls==calls[i]);assert(g_kombiNav.calls==calls[i]);assert(g_session.value==g_kombiNav.value);if(i>=3&&i<=6)assert(g_kombiNav.value);}
}
""".replace('MAPPER',mapper).replace('BLOCK',block)
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'signal.cpp').write_text(program)
 subprocess.run(['c++','-std=gnu++0x',str(p/'signal.cpp'),'-o',str(p/'signal')],check=True)
 subprocess.run([str(p/'signal')],check=True,capture_output=True)
print('[PASS] test_hud_night_signal.py: actual main block, initial/duplicate/invalid/day/night transitions')
