// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "cluster/hud_ring60.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace nbt::hudterm;

static uint32_t hash(const Picture& pic) {
    uint32_t h = 2166136261U;
    for (size_t i = 0; i < pic.size(); ++i) {
        h = (h ^ pic[i].colour)*16777619U;
        const std::vector<Point>& p = pic[i].poly.pts;
        h = (h ^ (uint32_t)p.size())*16777619U;
        for (size_t j = 0; j < p.size(); ++j) {
            h = (h ^ (uint32_t)p[j].x)*16777619U;
            h = (h ^ (uint32_t)p[j].y)*16777619U;
        }
    }
    return h;
}

static void valid(const Picture& pic) {
    for (size_t i = 0; i < pic.size(); ++i) {
        const std::vector<Point>& p = pic[i].poly.pts;
        assert(p.size() >= 3 && p.size() <= 99);
        for (size_t j = 0; j < p.size(); ++j) {
            assert(p[j].x >= 1 && p[j].x <= 124 && p[j].y >= 1 && p[j].y <= 75);
            for (size_t k = j+1; k < p.size(); ++k) assert(p[j].x != p[k].x || p[j].y != p[k].y);
            for (size_t k = j+2; k < p.size(); ++k) {
                if (j == 0 && k+1 == p.size()) continue;
                assert(!ring::crosses(p[j],p[(j+1)%p.size()],p[k],p[(k+1)%p.size()]));
            }
        }
    }
}

int main(int argc, char** argv) {
    const bool dump = argc == 2 && strcmp(argv[1],"--hashes") == 0;
    for (int angle = 0; angle <= 360; ++angle) {
        Picture bodies[2];
        for (int cw = 0; cw < 2; ++cw) {
            ShapeSpec spec; spec.kind = kShapeRoundabout; spec.exit = angle; spec.clockwise = cw != 0;
            assert(ring60::picture(spec,k6wa,bodies[cw],0)); valid(bodies[cw]);
            for (int n = 0; n <= 99; ++n) {
                Picture p;
                assert(ring60::picture(spec,k6wa,p,n)); valid(p);
                if (n) {
                    // Captions must remain wholly inside the ring, even for two-digit exits.
                    const int cy = bodies[cw][0].poly.pts[0].y - (angle ? 30 : ring60::kCirculation0[0].y);
                    for (size_t i = bodies[cw].size(); i < p.size(); ++i)
                        for (size_t j = 0; j < p[i].poly.pts.size(); ++j) {
                            int x = p[i].poly.pts[j].x-62, y = p[i].poly.pts[j].y-cy;
                            assert(x*x+y*y <= 22*22);
                        }
                }
                if (dump && n < 10) printf("%u%c",hash(p),n == 9 ? '\n' : ' ');
            }
            if (angle == 360) {
                const Poly& entry = bodies[cw][2].poly;
                const Poly& exit = bodies[cw][3].poly;
                int entryEnd = 0, exitEnd = 0;
                for (size_t i = 0; i < entry.pts.size(); ++i) entryEnd = std::max(entryEnd,(int)entry.pts[i].y);
                for (size_t i = 0; i < exit.pts.size(); ++i) exitEnd = std::max(exitEnd,(int)exit.pts[i].y);
                assert(entryEnd == exitEnd);
            }
        }
        assert(bodies[0].size() == bodies[1].size());
        for (size_t i = 0; i < bodies[0].size(); ++i) {
            const Poly& a = bodies[0][i].poly; const Poly& b = bodies[1][i].poly;
            assert(a.pts.size() == b.pts.size());
            for (size_t j = 0; j < a.pts.size(); ++j)
                assert(a.pts[j].x+b.pts[j].x == 124 && a.pts[j].y == b.pts[j].y);
        }
    }
    if (!dump) puts("ring60: all angles, both directions, numbers1..99, bounds and return-entry alignment passed");
    return 0;
}
