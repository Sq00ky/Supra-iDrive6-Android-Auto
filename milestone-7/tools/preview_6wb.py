#!/usr/bin/env python3
"""Generate a standalone browser preview using the local 6WB C++ renderer.

Run from milestone-7 with Python 3 and a host GCC-compatible C++ compiler:
    python tools/preview_6wb.py
On Windows use a MinGW-w64 g++ (MSYS2), or run with python3 and g++ in WSL.
Use --cxx to select the host compiler and --output to change the HTML path.
The QNX toolchain and the vehicle are not used.
"""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile


CAPTURE = r"""
#include "cluster/hud6wb_display.hpp"
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace nbt::hudterm;
using namespace nbt::kombinav;

class PreviewPort : public Port {
public:
    SubmitResult submit(const Batch&) override { return kNotReady; }
    Status status() const override { return kOffline; }
    uint32_t epoch() const override { return 0; }
    bool waiting() const override { return false; }
    void reset() override {}
    bool waitIdle(uint32_t) override { return false; }
};

void size(const Surface& s) {
    std::cout << '[' << s.a << ',' << s.b << ',' << s.width << ',' << s.height << ']';
}

// The 6WA translation unit supplies hudUnitText. Its transport diagnostics
// are unreachable in this preview, which calls only the 6WB renderer.
namespace nbt { namespace hudterm {
const char* statusName(Status) { throw std::logic_error("Preview has no transport"); }
const char* submitResultName(SubmitResult) { throw std::logic_error("Preview has no transport"); }
} }

int main() {
    const std::string street((std::istreambuf_iterator<char>(std::cin)),
                             std::istreambuf_iterator<char>());
    Bmw6wbProfile p;
    if (!validProfile(p)) throw std::runtime_error("Invalid 6WB profile");
    PreviewPort port;
    Hud6wbDisplay display(port, p, true);
    std::cout << "{\"handles\":[" << p.base.street << ',' << p.base.distance << ','
              << p.base.unit << ',' << p.bar << "],\"surfaces\":[";
    size(p.main); std::cout << ','; size(p.lanes); std::cout << ','; size(p.icons);
    std::cout << "],\"scenes\":[";

    struct Scenario { int type, angle, exit; };
    const Scenario scenes[] = {
        {30,90,1}, {30,180,2}, {30,270,3}, {30,315,4}, {30,0,0},
        {32,90,1}, {32,180,2}, {32,270,3}, {32,315,4},
        {2,0,0}, {7,0,0}, {8,0,0}, {5,0,0}, {6,0,0}, {9,0,0}, {10,0,0},
        {11,0,0}, {12,0,0}, {25,0,0}, {26,0,0}, {39,0,0}
    };

    bool first = true;
    for (const Scenario& scene : scenes) {
        for (unsigned extras = 0; extras < 4; ++extras) {
            NavView v;
            v.aa_type = scene.type;
            v.has_exit_angle = scene.angle != 0;
            v.exit_angle = scene.angle;
            v.has_exit_number = scene.exit != 0;
            v.exit_number = scene.exit;
            v.road = street;
            v.dist_text = "350";
            v.aa_unit = 1;
            v.has_step_m = true;
            v.step_m = 350;
            v.has_next = (extras & 1) != 0;
            v.next_type = 8;
            if (extras & 2) {
                v.lanes.push_back(AaLane(1, LaneDirection(4, false)));
                v.lanes.push_back(AaLane(1, LaneDirection(1, true)));
                v.lanes.push_back(AaLane(1, LaneDirection(5, false)));
            }
            Batch batch = display.render(v);
            if (!first) std::cout << ',';
            first = false;
            std::cout << '[';
            for (size_t i = 0; i < batch.calls.size(); ++i) {
                const Call& call = batch.calls[i];
                if (!callOk(call, handlesOf(p), true))
                    throw std::runtime_error("Invalid HUDTerminal call in preview");
                if (i) std::cout << ',';
                std::cout << '[' << call.fkt;
                for (uint8_t byte : call.payload) std::cout << ',' << unsigned(byte);
                std::cout << ']';
            }
            std::cout << ']';
        }
    }
    std::cout << "]}";
}
"""


HTML = r"""<!doctype html>
<html lang="it">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Anteprima BMW 6WB</title>
<style>
:root { color-scheme:dark; font:16px system-ui,sans-serif; background:#111418; color:#e9edf2 }
body { max-width:60rem; margin:3rem auto; padding:0 1rem }
h1 { margin:0 0 .6rem; letter-spacing:-.035em }
p { color:#adb8c6; line-height:1.5 }
main { display:grid; grid-template-columns:1fr 19rem; gap:1.2rem; margin-top:1.5rem }
section, aside { background:#191e25; border:1px solid #343d48; border-radius:1rem; padding:1.1rem }
section { text-align:center }
#cluster { position:relative; margin:auto; max-width:30rem; overflow:hidden; border-radius:.7rem;
background:radial-gradient(ellipse at top,#19222a,#080a0d 72%); color:#f5f8ff }
svg { display:block; width:100%; height:auto }
label { display:block; margin:0 0 1rem }
select { display:block; margin-top:.45rem; width:100%; font:inherit; padding:.55rem; border-radius:.45rem }
input { accent-color:#81b4ed }
input[type=range] { display:block; width:100%; margin-top:.5rem }
small { color:#adb8c6 }
@media(max-width:700px) { main { grid-template-columns:1fr } body { margin-top:1.25rem } }
</style>
<h1>Anteprima navigazione BMW 6WB</h1>
<p>Comandi generati dal renderer C++ locale. Font, colori e disposizione sul quadro sono approssimati.</p>
<main>
<section>
<div id="cluster">
<svg id="display" viewBox="0 0 360 335" role="img" aria-label="Anteprima indicazioni 6WB"></svg>
</div>
<p id="status" role="status"></p>
</section>
<aside>
<label>Manovra<select id="maneuver">
<option>Rotatoria antioraria · 1ª uscita</option>
<option selected>Rotatoria antioraria · 2ª uscita</option>
<option>Rotatoria antioraria · 3ª uscita</option>
<option>Rotatoria antioraria · 4ª uscita</option>
<option>Rotatoria · uscita sconosciuta</option>
<option>Rotatoria oraria · 1ª uscita</option>
<option>Rotatoria oraria · 2ª uscita</option>
<option>Rotatoria oraria · 3ª uscita</option>
<option>Rotatoria oraria · 4ª uscita</option>
<option>Prosegui dritto</option>
<option>Svolta a sinistra</option>
<option>Svolta a destra</option>
<option>Svolta leggera a sinistra</option>
<option>Svolta leggera a destra</option>
<option>Svolta stretta a sinistra</option>
<option>Svolta stretta a destra</option>
<option>Inversione a U a sinistra</option>
<option>Inversione a U a destra</option>
<option>Bivio a sinistra</option>
<option>Bivio a destra</option>
<option>Arrivo</option>
</select></label>
<label><input id="lanes" type="checkbox" checked> Corsie</label>
<label><input id="next" type="checkbox"> Manovra successiva (bitmap)</label>
<label>Dimensione via nell’anteprima <output id="fontValue"></output>
<input id="font" type="range" min="12" max="34" value="20"></label>
<small>Rigenera la pagina dopo una modifica ai sorgenti per vedere la nuova resa.</small>
</aside>
</main>
<script>
const data = __CAPTURE_DATA__;
const get = id => document.getElementById(id);
const maneuver = get('maneuver'), lanes = get('lanes'), next = get('next'), font = get('font');
const svg = get('display');
const latin = new TextDecoder('iso-8859-15');
const word = (p, i) => p[i] * 256 + p[i + 1];
const signed = n => n >= 32768 ? n - 65536 : n;

function replay(calls) {
    const states = new Map();
    const surface = id => {
        if (!states.has(id)) states.set(id, {visible:false, polygons:[], width:1});
        return states.get(id);
    };
    for (const [method, ...p] of calls) {
        const s = surface(word(p, 0));
        switch (method) {
            case 0x204: s.visible = Boolean(p[2]); break;
            case 0x20e: s.visible = false; surface(word(p, 2)).visible = true; break;
            case 0x210: s.text = latin.decode(Uint8Array.from(p.slice(3, -1))); break;
            case 0x219: s.width = p[2]; break;
            case 0x21d: {
                const points = [];
                for (let i = 6; i < p.length; i += 4)
                    points.push([signed(word(p, i)), signed(word(p, i + 2))]);
                s.polygons.push({filled:p[4] === 3, points, width:s.width});
                break;
            }
            case 0x220: s.polygons = []; break;
            case 0x227: s.level = p[5]; break;
            case 0xc05: s.bitmap = p.slice(10); break;
        }
    }
    return states;
}

function element(tag, attributes, parent = svg) {
    const node = document.createElementNS('http://www.w3.org/2000/svg', tag);
    for (const [k,v] of Object.entries(attributes)) node.setAttribute(k,v);
    parent.append(node);
    return node;
}

function bitmapImage(bytes, width, height) {
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext('2d');
    const pixels = ctx.createImageData(width,height);
    let pos = 0;
    for (let i = 0; i < bytes.length && pos < pixels.data.length;) {
        const code = bytes[i++], count = code & 127;
        if (!count) break;
        const repeat = Boolean(code & 128);
        for (let j = 0; j < count && pos < pixels.data.length; ++j) {
            const k = i + (repeat ? 0 : j * 4);
            pixels.data.set([bytes[k+3], bytes[k+2], bytes[k+1], bytes[k]], pos);
            pos += 4;
        }
        i += repeat ? 4 : count * 4;
    }
    ctx.putImageData(pixels,0,0);
    return canvas.toDataURL();
}

function drawSurface(state, spec, x, y) {
    const [a,b,w,h] = spec;
    const s = [state.get(a),state.get(b)].find(item => item?.visible);
    if (!s) return;
    const layer = element('svg', {x:x-w/2,y,width:w,height:h,viewBox:`0 0 ${w} ${h}`});
    for (const poly of s.polygons) {
        element(poly.filled ? 'polygon' : 'polyline', {
            points:poly.points.map(point => point.join(',')).join(' '),
            fill:poly.filled ? 'currentColor' : 'none', stroke:'currentColor',
            'stroke-width':poly.width, 'stroke-linejoin':'round', 'stroke-linecap':'round'
        }, layer);
    }
    if (s.bitmap) element('image',{width:w,height:h,href:bitmapImage(s.bitmap,w,h)},layer);
}

function drawText(state, id, x, y, size) {
    const s = state.get(id);
    if (s?.visible) element('text',{x,y,fill:'currentColor','font-size':size,
        'text-anchor':'middle','font-weight':600}).textContent = s.text ?? '';
}

function draw() {
    const calls = data.scenes[maneuver.selectedIndex*4 + Number(next.checked) + 2*Number(lanes.checked)];
    const state = replay(calls);
    const [street,distance,unit,bar] = data.handles;
    svg.replaceChildren();
    drawText(state,street,180,45,font.value);
    drawSurface(state,data.surfaces[0],180,10);
    drawText(state,distance,160,216,34);
    drawText(state,unit,207,216,17);
    const gauge = state.get(bar);
    if (gauge?.visible) {
        element('rect',{x:302,y:85,width:7,height:113,rx:3,fill:'#3e4856'});
        const height = 113 * gauge.level / 255;
        element('rect',{x:302,y:198-height,width:7,height,rx:3,fill:'#a5caff'});
    }
    drawSurface(state,data.surfaces[1],180,240);
    drawSurface(state,data.surfaces[2],220,284);
    get('fontValue').value = font.value + ' px';
    const bitmaps = calls.filter(call => call[0] === 0xc05);
    get('status').textContent = calls.length + ' comandi HUDTerminal' +
        (bitmaps.length ? ' · bitmap C05 ' + bitmaps[0].slice(1).length + ' byte' :
            next.checked ? ' · bitmap C05 assente' : '');
}
for (const control of [maneuver,lanes,next,font]) control.addEventListener('input',draw);
draw();
</script>
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++", help="host compiler executable")
    parser.add_argument("--output", type=Path, default=Path("6wb-preview.html"))
    parser.add_argument("--street", default="Viale Italia", help="testo inviato al renderer")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    cluster = root / "src" / "cluster"
    sources = [cluster / name for name in (
        "hud6wb_display.cpp", "hud6wb_artwork.cpp", "hud6wb_codec.cpp", "hud6wa_display.cpp"
    )]
    with tempfile.TemporaryDirectory(prefix="6wb-preview-") as tmp:
        capture = Path(tmp) / "capture.cpp"
        binary = Path(tmp) / "capture.exe"
        capture.write_text(CAPTURE, encoding="utf-8")
        command = [args.cxx, "-std=c++11", "-O2", "-I", str(root / "src"),
                   str(capture), *(str(source) for source in sources), "-o", str(binary)]
        try:
            subprocess.run(command, check=True)
            data = subprocess.check_output([str(binary)], input=args.street.encode("utf-8")).decode("ascii")
        except FileNotFoundError as exc:
            parser.exit(1, f"Host compiler unavailable: {exc.filename}\n")
        except subprocess.CalledProcessError as exc:
            parser.exit(exc.returncode, f"Failed: {shlex.join(exc.cmd)}\n")
    args.output.write_text(HTML.replace("__CAPTURE_DATA__", data), encoding="utf-8")
    print(args.output.resolve())


if __name__ == "__main__":
    main()
