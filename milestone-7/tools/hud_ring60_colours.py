#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Make an RGB proposal from hud_preview's serialized polygons, not measured cluster colours."""
import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path, help="Native tests/build/hud_preview executable")
parser.add_argument("output", type=Path)
parser.add_argument("--matrix", choices=("ccw", "cw"), help="Full angle/number matrix in the night RGB proposal")
args = parser.parse_args()
ns = {"s": "http://www.w3.org/2000/svg"}
cases = {}
for direction in ("ccw", "cw"):
    svg = subprocess.check_output([str(args.binary.resolve()), "0", "0", direction], text=True)
    for cell in ET.fromstring(svg).findall(".//s:g[@data-type]", ns):
        angle, number = int(cell.get("data-angle")), int(cell.get("data-number"))
        polygons = []
        for p in cell.findall(".//s:polygon", ns):
            points = [[int(v) for v in pair.split(",")] for pair in p.get("points").split()]
            polygons.append(dict(role="arrow" if p.get("fill") == "#ffe4ab" else "road", points=points))
        cases["%s/%d/%d" % (direction, angle, number)] = polygons

cells = [(0,1),(1,2),(90,3),(135,4),(180,5),(225,6),(270,7),(359,8),(360,8)]
rows = []
for theme, name in (("day", "День"), ("night", "Ночь")):
    for direction, traffic in (("ccw", "правостороннее"), ("cw", "левостороннее")):
        row = '<div class="row"><div class="label">%s<br>%s</div>' % (name, traffic)
        for angle, number in cells:
            caption = "Нет угла" if not angle else "%d°" % (30 if angle == 1 else 330 if angle == 359 else angle)
            row += '<div class="cell"><div>%s · №%d</div><canvas width="126" height="77" data-case="%s/%d/%d" data-theme="%s"></canvas></div>' % (caption, number, direction, angle, number, theme)
        rows.append(row + "</div>")
if args.matrix:
    rows = []
    for angle in (0,1,45,90,135,180,225,270,315,359,360):
        label = "Нет угла" if not angle else "1° → 30°" if angle == 1 else "359° → 330°" if angle == 359 else "%d°" % angle
        row = '<div class="row"><div class="label">%s · %s</div>' % (label,args.matrix.upper())
        for number in range(9):
            row += '<div class="cell"><div>%s</div><canvas width="126" height="77" data-case="%s/%d/%d" data-theme="night"></canvas></div>' % ("№%d" % number if number else "Без номера",args.matrix,angle,number)
        rows.append(row + "</div>")
template = '''<!doctype html><meta charset="utf-8"><style>
body{background:#10151a;color:#e3e8ec;font:18px Arial;margin:22px}h1{font-size:30px}p{color:#bac4ce}
.row{display:flex;margin-top:25px}.label{width:205px;flex-shrink:0;padding-top:50px}.cell{width:278px;text-align:center;flex-shrink:0}
canvas{width:252px;height:154px;image-rendering:pixelated;background:#000;margin-top:12px;outline:1px solid #28313a}
</style><h1>F30 · Ø60 · цифры +25% · принятые контуры</h1>
<p>Сериализованные полигоны C++-драйвера. У 360° вход доходит до уровня острия; 180° сохранены.</p>
ROWS
<p>Кадр 126×77 → показ 2×. RGB — целевая цветовая схема: физическая палитра и яркость не измерены.</p>
<script>const cases=CASES,palettes={day:{arrow:'#f1f1ea',road:'#E6A064'},night:{arrow:'#E6A064',road:'#815d43'}};
for(const c of document.querySelectorAll('canvas')){
 const g=c.getContext('2d'),palette=palettes[c.dataset.theme];g.fillStyle='#000';g.fillRect(0,0,126,77);
 // One nonzero fill per consecutive colour prevents Canvas AA seams between touching simple polygons.
 // Keep every native vertex; orient subpaths alike so overlapping pieces form a union.
 let role=null;const flush=()=>{if(role!==null){g.fillStyle=palette[role];g.fill();}};
 for(const q of cases[c.dataset.case]){
  if(q.role!==role){flush();g.beginPath();role=q.role;}
  let p=q.points;const area=p.reduce((a,v,i)=>a+v[0]*p[(i+1)%p.length][1]-p[(i+1)%p.length][0]*v[1],0);
  if(area<0)p=p.slice().reverse();
  p.forEach(([x,y],i)=>i?g.lineTo(x,y):g.moveTo(x,y));g.closePath();
 }flush();
}</script>'''
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(template.replace("ROWS", "\n".join(rows)).replace("CASES", json.dumps(cases, separators=(",", ":"))))
