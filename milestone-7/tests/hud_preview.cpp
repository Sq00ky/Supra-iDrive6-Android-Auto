// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Render the production driver's serialized polygons, without a car or a second implementation of the glyphs.
#include "cluster/hud6wa_display.hpp"
#include "cluster/hudterm_shapes.hpp"
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace nbt;
using namespace nbt::hudterm;
using namespace nbt::kombinav;

struct PreviewPort : Port {
    std::vector<Batch> batches;
    SubmitResult submit(const Batch& b) { batches.push_back(b); return kQueued; }
    Status status() const { return kReady; }
    uint32_t epoch() const { return 0; }
    bool waiting(int) const { return false; }
    void reset() {}
    bool waitIdle(uint32_t) { return true; }
    bool measuredSize(int& w, int& h) const { w = 126; h = 77; return true; }
};

// Follow the actual double-buffer switches and route updates before painting the visible layers.
static void polygons(const PreviewPort& port) {
    std::map<int, Picture> layers;
    std::map<int, int> colours;
    std::map<int, bool> visible;
    visible[k6wa.bufB] = visible[k6wa.routeB] = true;
    for (size_t b = 0; b < port.batches.size(); ++b)
        for (size_t i = 0; i < port.batches[b].calls.size(); ++i) {
            const Call& c = port.batches[b].calls[i];
            const std::vector<uint8_t>& p = c.payload;
            int h = rdBe16(&p[0]);
            if (c.fkt == kRemoveAllItems) layers[h].clear();
            else if (c.fkt == kSetFillColor) colours[h] = p[2];
            else if (c.fkt == kSetVisibility) visible[h] = p[2] != 0;
            else if (c.fkt == kSwitchVisibility) { visible[h] = false; visible[rdBe16(&p[2])] = true; }
            else if (c.fkt == kAddPolygon) {
                Item it; it.colour = colours[h]; it.poly.type = (PolyType)p[4];
                for (size_t j = 0; j < p[5]; ++j)
                    it.poly.pts.push_back(shapes::pt((int16_t)rdBe16(&p[6 + j * 4]),
                                                   (int16_t)rdBe16(&p[8 + j * 4])));
                layers[h].push_back(it);
            }
        }
    const int order[] = { k6wa.bufA, k6wa.bufB, k6wa.routeA, k6wa.routeB };
    for (unsigned h = 0; h < sizeof(order) / sizeof(order[0]); ++h) {
        if (!visible[order[h]]) continue;
        const Picture& pic = layers[order[h]];
        for (size_t i = 0; i < pic.size(); ++i) {
            const char* colour = pic[i].colour == k6wa.fill ? "#ffe4ab" : pic[i].colour == k6wa.edge ? "#000" : "#777";
            printf("<polygon fill=\"%s\" points=\"", colour);
            for (size_t j = 0; j < pic[i].poly.pts.size(); ++j)
                printf("%d,%d ", pic[i].poly.pts[j].x, pic[i].poly.pts[j].y);
            puts("\"/>");
        }
    }
}

int main(int argc, char** argv) {
    int stage = 0, mode = 0; char extra;
    int matrix = argc == 4 ? (strcmp(argv[3], "ccw") == 0 ? 1 : strcmp(argv[3], "cw") == 0 ? 2 : -1) : 0;
    if (argc > 4 || (argc >= 2 && sscanf(argv[1], "%d%c", &stage, &extra) != 1) || stage < 0 || stage > 4
        || (argc >= 3 && sscanf(argv[2], "%d%c", &mode, &extra) != 1) || mode < 0 || mode > 1 || matrix < 0) {
        fputs("usage: hud_preview [0=far, 1..4=near route stage] [0=parametric, 1=Mapbox] [ccw|cw ring matrix]\n", stderr);
        return 2;
    }
    // An inherited tuning pin would make a supposed default preview misleading.
    unsetenv("NBT_HUD_CANVAS"); unsetenv("NBT_HUD_GLYPH");
    struct RingCase { int angle, number; };
    const RingCase rings[] = {
        {1, 5}, {45, 5}, {90, 5}, {135, 5}, {180, 5}, {225, 5}, {270, 5}, {315, 5}, {360, 5},
        {0, 5}, {0, 12}, {0, 99}, {90, 12}, {180, 99}
    };
    const int ringCases = (int)(sizeof(rings) / sizeof(rings[0]));
    const int matrixAngles[] = { 0, 1, 45, 90, 135, 180, 225, 270, 315, 359, 360 };
    const int perMode = matrix ? 9 * (int)(sizeof(matrixAngles)/sizeof(matrixAngles[0])) : 61 + 2 * ringCases;
    const int columns = matrix ? 9 : 8, rows = (perMode + columns - 1) / columns;
    const int width = columns * 148, header = matrix ? 66 : 46;
    printf("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n", width, header + rows * 110, width, header + rows * 110);
    puts("<rect width=\"100%\" height=\"100%\" fill=\"#111\"/><g fill=\"#ddd\" font-family=\"sans-serif\" font-size=\"11\">");
    printf("<text x=\"12\" y=\"20\">F30 126x77 — %s, %s; production polygons (not a photograph of the HUD)</text>\n", mode ? "Mapbox" : "Parametric", stage ? "near route stages" : "plain arrows");
    if (matrix) printf("<text x=\"12\" y=\"40\">%s: input angles include 1/359 (drawn as 15/345); columns = no number, 1..8.</text>\n", matrix == 1 ? "CCW / right-hand traffic" : "CW / left-hand traffic");
    {
        setenv("NBT_HUD_GLYPHS", mode ? "1" : "0", 1);
        for (int n = 0; n < perMode; ++n) {
            int extra = n - 61, a = extra >= 0 ? extra % ringCases : -1;
            int type = extra >= 0 ? (extra < ringCases ? 34 : 32) : n;
            int angle = a >= 0 ? rings[a].angle : 270;
            int number = a >= 0 ? rings[a].number : 5;
            if (matrix) { type = matrix == 1 ? 34 : 32; angle = matrixAngles[n / 9]; number = n % 9; }
            NavView v;
            v.aa_type = type; v.road = "Example road"; v.dist_text = "100"; v.aa_unit = 1;
            v.has_exit_angle = angle != 0; v.exit_angle = angle;
            v.has_exit_number = number != 0; v.exit_number = number;
            v.has_step_m = true; v.step_m = stage ? 40 : 1000;
            v.has_step_s = true; v.step_s = stage ? 3 : 100;
            PreviewPort port;
            Hud6waDisplay d(port, k6wa, false);
            Settings settings; settings.junction = stage != 0; d.setSettings(settings);
            if (d.showManeuver(v, 0, true, true) != ClusterDisplay::kOk) return 1;
            if (stage && stage < 4) { v.step_m = stage * 10; d.sameManeuver(v, 1); if (d.keepUp(v, 1) != ClusterDisplay::kOk) return 1; }
            ShapeSpec rendered = shapeFor(type, v.has_exit_angle, v.exit_angle);
            int cell = n;
            printf("<g data-type=\"%d\" data-has-angle=\"%d\" data-angle=\"%d\" data-rendered-angle=\"%d\" data-has-number=\"%d\" data-number=\"%d\" transform=\"translate(%d,%d)\"><text x=\"0\" y=\"11\">type %d", type, (int)v.has_exit_angle, angle, rendered.exit, (int)v.has_exit_number, number, 10 + cell % columns * 148, header - 10 + cell / columns * 110, type);
            if (extra >= 0 || matrix) {
                if (v.has_exit_angle) {
                    printf(" a%d", angle);
                    if (rendered.exit != angle) printf(" to%d", rendered.exit);
                } else printf(" a?");
                if (v.has_exit_number) printf(" #%d", number); else printf(" #none");
            }
            if (stage) printf(" %s%d", d.nearShown() ? "near" : "arrow", d.routeStage());
            puts("</text><svg x=\"0\" y=\"18\" width=\"126\" height=\"77\" viewBox=\"0 0 126 77\"><rect width=\"126\" height=\"77\" fill=\"black\"/>");
            polygons(port);
            puts("</svg></g>");
        }
    }
    puts("</g></svg>");
    return 0;
}
