#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Adapt the original CC0 Mapbox wing pieces; the unchanged shaft/tip/neck remain the reference.

Input: git show cd1d9f8:milestone-7/src/cluster/mapbox_glyphs.hpp > /tmp/mapbox-original.hpp
Run: python3 tools/gen_mapbox_wings.py /tmp/mapbox-original.hpp
No network, third-party packages or runtime floating-point geometry is required.
"""
import hashlib
import math
from pathlib import Path
import re
import sys

# name, tip, backwards axis (None = tip to neck midpoint), shaft-edge pair,
# fixed shoulder pair, actual return-edge tangent pair, affected head pieces.
FAMILIES = [
    ('kArrowstraight', (120,26), (0,1), [(126,62),(114,62)], [(128,61),(112,61)], [(147,67),(93,67)], [0,2,3,4,5]),
    ('kArrowright', (164,54), (-1,0), [(128,61),(128,48)], [(129,63),(129,46)], [(123,81),(123,28)], list(range(2,8))),
    ('kArrowslightright', (154,33), None, [(133,60),(124,50)], [(135,60),(125,48)], [(143,74),(113,38)], list(range(7,14))),
    ('kArrowsharpright', (161,93), None, [(131,70),(140,61)], [(132,73),(143,63)], [(114,81),(153,46)], list(range(6))),
    ('kArrow180right', (137,120), (0,-1), [(131,86),(143,86)], [(129,86),(145,86)], [(111,79),(163,79)], list(range(6))),
    ('kMerge', (120,22), (0,1), [(126,55),(114,55)], [(128,53),(112,53)], [(143,56),(97,56)], list(range(26,33))),
    ('kFork', (159,28), None, [(146,60),(136,53)], [(149,59),(135,50)], [(160,70),(121,44)], list(range(19,26))),
    ('kOfframp', (156,32), None, [(136,58),(127,48)], [(137,59),(126,47)], [(145,72),(114,37)], list(range(8,15))),
    ('kRbArrow', (-328,0), (1,0), [(-156,32),(-157,-32)], [(-166,42),(-168,-40)], [(-151,118),(-152,-120)], list(range(6))),
]
FAMILY_KEYS = dict(zip((row[0] for row in FAMILIES), (1,7,3,5,9,26,38,29,0)))
SOURCE_SHA = '4dcf1d9a8d194befc2f3c777bba97e9ed9876f6527c31fe7be833457cc84d0f5'
ROOT = Path(__file__).resolve().parents[1]
PATTERN = re.compile(r'const Piece (\w+)\[\] = \{(.*?)\n\};\nconst unsigned \1N = \d+;', re.S)


def parse(body):
    pieces = []
    for line in body.splitlines():
        if not line.strip():
            continue
        tone, count = map(int, re.search(r'\{\s*(\d+),\s*(\d+),', line).groups())
        points = [tuple(map(int, p)) for p in re.findall(r'\{\s*(-?\d+),\s*(-?\d+)\s*\}', line)]
        assert len(points) == count
        pieces.append((tone, points))
    return pieces


def area(points):
    return sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(points, points[1:]+points[:1]))


def rounded(x):
    return math.floor(x + .5) if x >= 0 else math.ceil(x - .5)


def fingerprint(pieces):
    value = 2166136261
    for tone, points in pieces:
        for n in [tone, len(points)] + [n for p in points for n in p]:
            value = ((value ^ (n & 0xffff)) * 16777619) & 0xffffffff
    return value


def emit_piece(piece):
    tone, points = piece
    return '  { %d, %d, { %s } }' % (tone, len(points), ', '.join('{%d,%d}' % p for p in points))


def curved_bodies():
    def arc(cx, cy, radius, start, end, steps):
        return [(cx + radius*math.cos(math.radians(start+(end-start)*i/steps)),
                 cy + radius*math.sin(math.radians(start+(end-start)*i/steps))) for i in range(steps+1)]
    # Sharp right: a concentric 135-degree bend with the original 13-unit entry shaft and diagonal neck.
    radius = 28/math.sqrt(2)
    cx, cy = 85.5+radius, 43.5+radius
    sharp = ([(79,120), (79,cy)] + arc(cx,cy,radius+6.5,180,315,12)[1:] +
             [(140,61), (131,70)] + arc(cx,cy,radius-6.5,315,180,12) + [(92,120)])
    # The two U-turn semicircles meet the original shaft edges with vertical tangents.
    uturn = ([(90,120), (90,55)] + arc(116.5,55,26.5,180,360,16)[1:] +
             [(143,86), (131,86), (131,55)] + arc(117,55,14,360,180,16)[1:] + [(103,120)])
    return {name: [(rounded(x*10), rounded(y*10)) for x,y in points] for name,points in
            [('kArrowsharpright',sharp), ('kArrow180right',uturn)]}


def generate(source):
    original = {m[1]: parse(m[2]) for m in PATTERN.finditer(source)}
    generated = {}
    records = []
    bounds = []
    curves = curved_bodies()
    for name, tip, axis, shaft, shoulders, tangents, selected in FAMILIES:
        old = original[name]
        if axis is None:
            axis = (shoulders[0][0] + shoulders[1][0] - 2*tip[0], shoulders[0][1] + shoulders[1][1] - 2*tip[1])
        length = math.hypot(*axis)
        back = (axis[0]/length, axis[1]/length)
        side = (back[1], -back[0])
        def project(p):
            return ((p[0]-tip[0])*back[0] + (p[1]-tip[1])*back[1],
                    (p[0]-tip[0])*side[0] + (p[1]-tip[1])*side[1])
        sides = []
        for edge, shoulder, tangent in zip(shaft, shoulders, tangents):
            a,b = project(shoulder)
            bu,bv = project(tangent)
            sign = 1 if b > 0 else -1
            b, bv = abs(b), abs(bv)
            h = abs(project(edge)[1])
            maximum = max(abs(project(p)[1]) for i in selected for p in old[i][1] if project(p)[1]*sign > 0)
            target = h + .7*(maximum-h)
            ratio = (target-b)/(maximum-b)
            new_bv = b + ratio*(bv-b)
            new_bu = a + (new_bv-b)/math.tan(math.radians(80))
            shear = (new_bu-bu)/(bv-b)
            assert 0 < h < b < bv <= maximum and 0 < ratio < 1
            sides.append((sign,a,b,h,maximum,target,ratio,shear,shoulder,tangent))
        new = []
        unchanged = []
        unchanged_mask = 0
        for i, (tone, points) in enumerate(old):
            if name in curves and i >= 6:
                continue   # replace the overlapping integer body quads with one smooth contour below
            changed = []
            for p in points:
                u,v = project(p)
                if i in selected:
                    for sign,a,b,h,maximum,target,ratio,shear,shoulder,tangent in sides:
                        q = v*sign
                        if q > b + 1e-8:
                            u += shear*(q-b)
                            v = sign*(b + ratio*(q-b))
                            p = (rounded(tip[0] + back[0]*u + side[0]*v),
                                 rounded(tip[1] + back[1]*u + side[1]*v))
                            break
                changed.append(p)
            if name == 'kOfframp' and i == 1:
                # Continue the existing constant-width diagonal shaft into the head; leave its bend/base untouched.
                for j in (0, 3):
                    changed[j] = (changed[j][0]+6, changed[j][1]-5)
            if not area(changed):
                assert i in selected and changed != points, (name, i, 'collapsed non-wing piece')
                continue
            if changed == points:
                unchanged_mask |= 1 << len(new)
                unchanged.append((tone, points))
            new.append((tone, changed))
        generated[name] = new
        allpoints = [p for _,points in old for p in points]
        bounds.append((name,min(x for x,y in allpoints),min(y for x,y in allpoints),max(x for x,y in allpoints),max(y for x,y in allpoints)))
        axial = [(p[0]-tip[0])*back[0] + (p[1]-tip[1])*back[1] for p in allpoints]
        records.append((name,tip,back,side,sides,shaft,min(axial),max(axial),unchanged_mask,fingerprint(unchanged)))
    def replace(match):
        name = match[1]
        if name not in generated:
            return match[0]
        pieces = generated[name]
        return 'const Piece %s[] = {\n%s\n};\nconst unsigned %sN = %d;' % (name, ',\n'.join(map(emit_piece,pieces)),name,len(pieces))
    header = PATTERN.sub(replace, source)
    header = header.replace('// hudglyphs.h — GENERATED by tools/hud_glyphs.py, do not edit.',
                            '// Derived Mapbox glyphs — GENERATED by tools/gen_mapbox_wings.py, do not edit.')
    text = ['\n// Smooth bodies in tenths of a 6WA unit; the six original, adapted head pieces remain above.',
            '// One simple contour avoids the irregular outline of overlapping, separately rounded body quads.']
    for name,points in curves.items():
        text += ['const short %sBody[][2] = { %s };' % (name, ', '.join('{%d,%d}' % p for p in points))]
    text += ['struct CurveBody { int family; unsigned n; const short (*p)[2]; };',
             'const CurveBody kCurveBodies[] = {']
    text += ['  { %d, %d, %sBody },' % (FAMILY_KEYS[name],len(points),name) for name,points in curves.items()]
    text += ['};', '\n// Pre-wing bounds keep the on-car icon fit, tip and shaft positions unchanged.',
            'struct OriginalBounds { int family; short x0, y0, x1, y1; };',
            'const OriginalBounds kOriginalBounds[] = {']
    text += ['  { %d, %d, %d, %d, %d },' % (FAMILY_KEYS[row[0]], *row[1:]) for row in bounds if row[0] != 'kRbArrow']
    text += ['};\n']
    header = header.replace('} } } // namespace nbt::hudterm::mbx', '\n'.join(text)+'\n} } } // namespace nbt::hudterm::mbx')
    fixture = ['// GENERATED by tools/gen_mapbox_wings.py; anchors and hashes, not a second copy of the glyphs.',
               'struct WingSideCheck { double sign, a, b, h, maximum, target; short sx, sy, bx, by; };',
               'struct WingCheck { int family; const mbx::Piece* glyph; unsigned n; short tx, ty, shaft[4]; double ux, uy, vx, vy, minU, maxU; unsigned long long unchanged; unsigned hash; WingSideCheck side[2]; };',
               'static const WingCheck kWingChecks[] = {']
    for name,tip,back,side,sides,shaft,minu,maxu,mask,hash_ in records:
        head = '  { %d, mbx::%s, mbx::%sN, %d,%d, { %d,%d,%d,%d }, %.15g,%.15g, %.15g,%.15g, %.15g,%.15g, 0x%xULL, 0x%xU, {' % (FAMILY_KEYS[name],name,name,*tip,*shaft[0],*shaft[1],*back,*side,minu,maxu,mask,hash_)
        rows = []
        for sign,a,b,h,maximum,target,ratio,shear,shoulder,tangent in sides:
            rows.append('{ %d, %.15g, %.15g, %.15g, %.15g, %.15g, %d,%d,%d,%d }' % (sign,a,b,h,maximum,target,*shoulder,*tangent))
        fixture.append(head+', '.join(rows)+' } },')
    fixture += ['};','struct PreservedGlyphCheck { const mbx::Piece* glyph; unsigned n, hash; };','static const PreservedGlyphCheck kPreservedGlyphChecks[] = {']
    for name, pieces in original.items():
        if name not in generated:
            fixture.append('  { mbx::%s, mbx::%sN, 0x%xU },' % (name,name,fingerprint(pieces)))
    fixture += ['};','']
    return header, '\n'.join(fixture)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: gen_mapbox_wings.py ORIGINAL_MAPBOX_GLYPHS_HPP')
    data = Path(sys.argv[1]).read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA:
        raise SystemExit('original Mapbox header does not match the documented cd1d9f8 source')
    header, fixture = generate(data.decode())
    (ROOT/'milestone-7/src/cluster/mapbox_glyphs.hpp').write_text(header)
    (ROOT/'milestone-7/tests/mapbox_wing_fixtures.hpp').write_text(fixture)
