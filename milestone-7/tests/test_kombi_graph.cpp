// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: kombi_nav graph builder (src/kombi_graph.hpp). The SHA-256 vectors were produced by the test client
// (Python) that drove a J29 cluster + HUD route-free with this graph (same template, same inputs), so a match means
// byte-identical frames.
#include "kombi_graph.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
using namespace nbt::kombinav;

// ---- minimal SHA-256 (FIPS 180-4) ----------------------------------------------------------------------------------
namespace {
struct Sha256 {
    uint32_t h[8];
    uint8_t buf[64];
    uint64_t len;
    size_t n;
    Sha256() : len(0), n(0) {
        static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
                                        0x1f83d9ab, 0x5be0cd19 };
        memcpy(h, iv, sizeof(h));
    }
    static uint32_t ror(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }
    void block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const uint8_t* p, size_t k) {
        len += k;
        while (k--) { buf[n++] = *p++; if (n == 64) { block(buf); n = 0; } }
    }
    std::string hex() {
        uint64_t bits = len * 8;
        uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (n != 56) update(&zero, 1);
        uint8_t l[8];
        for (int i = 0; i < 8; ++i) l[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(l, 8);
        char out[65];
        for (int i = 0; i < 8; ++i) snprintf(out + 8 * i, 9, "%08x", h[i]);
        return out;
    }
};

std::string hashFrames(const std::vector<Frame>& fr) {
    Sha256 s;
    for (size_t i = 0; i < fr.size(); ++i) {
        uint8_t m[2] = { (uint8_t)(fr[i].method >> 8), (uint8_t)fr[i].method };
        s.update(m, 2);
        if (!fr[i].body.empty()) s.update(&fr[i].body[0], fr[i].body.size());
    }
    return s.hex();
}

// The UTF-16BE text at the first BOM (ASCII only here) and the byte after its NUL.
std::string textAt(const Bytes& b, int* after) {
    for (size_t i = 18; i + 1 < b.size(); ++i) {
        if (b[i] != 0xFE || b[i + 1] != 0xFF) continue;
        size_t n = b[i - 1];
        std::string s;
        for (size_t k = i + 2; k + 1 < i + n - 2 + 1 && k + 1 < b.size(); k += 2) s += (char)b[k + 1];
        if (after) *after = i + n < b.size() ? b[i + n] : -1;
        return s;
    }
    return "<none>";
}
} // namespace

int main() {
    // byte-identical to the car-proven builder
    {
        std::vector<Frame> fr;
        Graph g(0xC8, 255), f(0xC8, -1);
        assert(g.start(6, "TEST ST", "0.3", 0x01, fr) && fr.size() == 97);
        assert(hashFrames(fr) == "f7995023fd50bc078537ed0cbe27d31a1f59185c3d89e3f81b83a734a7edb6c9");
        assert(f.start(7, "Main St", "600", 0x05, fr));
        assert(hashFrames(fr) == "ad422abdcd99af78568d72d1556d28522347ab1d3b1a8d03c41729b2b3316492");
        assert(g.start(10, "Stra\xc3\x9f" "e \xf0\x9f\x9a\x97", "-", -1, fr));   // "Straße 🚗": surrogate pair
        assert(hashFrames(fr) == "134d27d9c6ed90beae7d48d90625dbbfdae62478407188cde43ad86e41824091");
        assert(g.start(1, " ", "0.1", 0x01, fr));
        assert(hashFrames(fr) == "1d4ecf5a30cd22fa8294890f8362f614d8629a88654e55106ce8c884b6e7ab03");
        assert(g.start(6, std::string(40, 'X'), "0.5", 0x01, fr));
        assert(hashFrames(fr) == "f436f9e12d13c94ad2c12e04437665924aa07593e0e60ea3fe1dabc125d65815");
        assert(g.distance("450", 0x05, fr) && fr.size() == 1);
        assert(hashFrames(fr) == "86964e5eebd4b0f4abecacf722be406653fbec2138a64d756ba1d3a34535b2cc");
        assert(f.distance("12.5", 0x01, fr));
        assert(hashFrames(fr) == "ed4a713611ed02e68edbf853dcd96e8ab302a0a2894af779c6ae619334c1bba7");
        g.cancel(fr);
        assert(fr.size() == 6 && hashFrames(fr) == "179ca21bd1493713bba5d338d90c296530e072abca958015848a939a8dbcaeda");
    }
    // untouched = the template (factory direction 7, no text)
    {
        std::vector<Frame> fr;
        Graph g(0xC8, -1);
        assert(g.start(7, "", "", -1, fr));
        for (size_t i = 0; i < fr.size(); ++i) {
            assert(fr[i].method == tmpl::phase_b[i].method && fr[i].body.size() == tmpl::phase_b[i].len);
            assert(!memcmp(&fr[i].body[0], tmpl::phase_b[i].body, tmpl::phase_b[i].len));
        }
    }
    // only whitelisted methods, handle everywhere, varlen consistent; maneuver 0 patched, the others not
    {
        std::vector<Frame> fr, c;
        Graph g(0xC9, 255);
        assert(g.start(6, "ELM ST", "0.4", 0x01, fr));
        g.cancel(c);
        fr.insert(fr.end(), c.begin(), c.end());
        int desc0 = 0, dist0 = 0, road0 = 0;
        for (size_t i = 0; i < fr.size(); ++i) {
            const Bytes& b = fr[i].body;
            assert(methodAllowed(fr[i].method) && fr[i].method != 0x0001);
            assert(getU32(b, 0) == 0xC9);
            if (b.size() >= 17 && fr[i].method != 0x0002) assert(getU32(b, 9) == b.size() - 17);
            if (i >= 97 || modelIndex(b) != 0) continue;
            if (fr[i].method == 0x000A && modelType(b) == 15) { assert(b[33] == 6); ++desc0; }
            if (fr[i].method == 0x000A && modelType(b) == 29) {
                int u;
                assert(textAt(b, &u) == "0.4" && u == 0x01 && b[22] == 255);
                ++dist0;
            }
            if (fr[i].method == 0x0003 && modelType(b) == 3) { assert(textAt(b, NULL) == "ELM ST"); ++road0; }
        }
        assert(desc0 == 2 && dist0 == 2 && road0 == 2);
        // a later maneuver keeps the template's distance text
        bool other = false;
        for (size_t i = 0; i < 97; ++i)
            if (fr[i].method == 0x000A && modelType(fr[i].body) == 29 && modelIndex(fr[i].body) == 1) {
                assert(textAt(fr[i].body, NULL) == "400");
                other = true;
            }
        assert(other);
    }
    // cancel ends in guidance state 0; the fallback is the state-0 frame (+ the destination clear, same byte test)
    {
        std::vector<Frame> c, s0;
        Graph g;
        g.cancel(c);
        g.cancelState0(s0);
        assert(c[0].body[30] == 7 && c[1].body[30] == 5 && c[2].body[30] == 0);
        assert(s0.size() == 2 && s0[0].body == c[2].body && s0[1].body == c[5].body);
    }
    // the template's road names are the neutral ones (no captured street names), and it keeps the factory layout
    {
        const char* allowed[] = { "Main Street", "Second Road", "Third Drive", "Fourth Ave" };
        int roads = 0;
        for (size_t i = 0; i < tmpl::phase_b_count; ++i) {
            if (tmpl::phase_b[i].method != 0x0003) continue;
            Bytes b(tmpl::phase_b[i].body, tmpl::phase_b[i].body + tmpl::phase_b[i].len);
            std::string t = textAt(b, NULL);
            if (t == "<none>") continue;
            bool ok = false;
            for (size_t j = 0; j < sizeof(allowed) / sizeof(allowed[0]); ++j) ok = ok || t == allowed[j];
            assert(ok);
            ++roads;
        }
        assert(roads == 9 && tmpl::phase_b_count == 97 && tmpl::cancel_count == 6);
    }
    // UTF-16: BMP, a 4-byte character as a surrogate pair, invalid bytes as U+FFFD
    {
        Bytes u = utf16be("A\xc3\x9f\xf0\x9f\x9a\x97\xff");
        const uint8_t want[] = { 0, 'A', 0x00, 0xDF, 0xD8, 0x3D, 0xDE, 0x97, 0xFF, 0xFD };
        assert(u.size() == sizeof(want) && !memcmp(&u[0], want, sizeof(want)));
    }
    // lanes: byte-identical to the car-proven bridge (aa_hud_bridge Graph.start(lanes=), position, feed_position) for
    // the parked lane test's four junctions (lane row car-verified on the cluster and the HUD, 2026-11-01)
    {
        const struct { int dir; const char* road; uint8_t k[4]; uint8_t a[4]; size_t n; const char* sha; } cases[] = {
            { 6, "TEST ST", {1, 2, 2, 2}, {6, 1, 1, 1}, 4, "83148d1e9d09e178e99b074af6fbcecd06e6784361eb12de523e8225156ba38d" },
            { 7, "MAIN ST", {2, 2, 2, 1}, {6, 1, 1, 7}, 4, "4611b5d77a91891f1cab8759fdfff0421f47d264bfefb5e1afc85c3ab0603045" },
            { 7, "OAK AVE", {2, 2, 1, 0}, {6, 1, 7, 0}, 3, "23891cc1ef8230e1533641ad76fe730d20cece6beed2da1a2741826615372a2b" },
            { 2, "ELM WAY", {1, 1, 2, 2}, {2, 2, 1, 1}, 4, "99f104c94377a84c51ffd9b292f003b874fa14c9f13729c4ef1305c149e68801" },
        };
        for (size_t c = 0; c < 4; ++c) {
            KombiLanes l;
            for (size_t i = 0; i < cases[c].n; ++i) l.push_back(KombiLane(cases[c].k[i], cases[c].a[i]));
            Graph g(0xC8, 255);
            std::vector<Frame> fr, p;
            double pos = 0;
            assert(!g.feedPosition(true, 10.0, pos));                       // no lane graph yet: no feed
            assert(g.start(cases[c].dir, cases[c].road, "0.2", 0x01, fr, &l) && fr.size() == 93);
            assert(hashFrames(fr) == cases[c].sha);
            const struct { bool has; double step; double pos; const char* sha; } feeds[] = {
                { true, 395.0, 4132.0, "3982173efa6656487fe982ce2f3b7dae0cdcb2d1bc1112adbe00bac8e5a24e38" },
                { false, 0.0, 4132.0, "3982173efa6656487fe982ce2f3b7dae0cdcb2d1bc1112adbe00bac8e5a24e38" },
                { true, 10.0, 3962.0, "53221d352e214920da4314fd5d0211af5490e9705e2d64d14d30275a243d8c50" },
                { true, 500.0, 4132.0, "3982173efa6656487fe982ce2f3b7dae0cdcb2d1bc1112adbe00bac8e5a24e38" },
                { true, -5.0, 3952.0, "519972f83c9327d4f7682d468a7e069fb0a47d3c48bbb37a4748f682ebe01a26" },
            };
            for (size_t f = 0; f < 5; ++f) {
                assert(g.feedPosition(feeds[f].has, feeds[f].step, pos) && pos == feeds[f].pos);
                assert(g.position(pos, p) && p.size() == 1 && p[0].method == 0x0009);
                assert(hashFrames(p) == feeds[f].sha);
            }
            // one lane model, sent right before maneuver 0's 0x0F nodes which list it; no template lane model left
            int models = 0;
            for (size_t i = 0; i < fr.size(); ++i) {
                assert(methodAllowed(fr[i].method));
                if (fr[i].method == 0x0010) {
                    ++models;
                    assert(getU32(fr[i].body, 4) == kLaneModel0 && i + 1 < fr.size());
                    assert(modelType(fr[i + 1].body) == 15 && modelIndex(fr[i + 1].body) == 0);
                    assert(getU32(fr[i].body, 9) == fr[i].body.size() - 17);
                }
                if (fr[i].method == 0x000A && modelType(fr[i].body) == 15) {
                    uint32_t n = getU32(fr[i].body, 54);
                    assert(modelIndex(fr[i].body) == 0 ? (n == 4 && getU32(fr[i].body, 58) == kLaneModel0) : n == 0);
                }
            }
            assert(models == 2);
        }
        // no lanes = the arrows-only graph, byte for byte
        KombiLanes none;
        std::vector<Frame> a, b;
        Graph g(0xC8, 255);
        assert(g.start(6, "TEST ST", "0.3", 0x01, a) && g.start(6, "TEST ST", "0.3", 0x01, b, &none));
        assert(a.size() == 97 && hashFrames(a) == hashFrames(b));
    }
    // scope: no road shields, route-number icons or signposts: nothing we send is an image resource (model type 1,
    // methods 0x0002 / 0x0003, where the factory sends its freeway-shield bitmaps), with and without lanes
    {
        KombiLanes l(1, KombiLane(1, 6));
        std::vector<Frame> fr, c;
        Graph g(0xC8, 255);
        assert(g.start(6, "TEST ST", "0.3", 0x01, fr, &l));
        g.cancel(c);
        fr.insert(fr.end(), c.begin(), c.end());
        for (size_t i = 0; i < fr.size(); ++i) {
            assert(modelType(fr[i].body) != 1);
            if (fr[i].method == 0x0002 || fr[i].method == 0x0003) assert((getU32(fr[i].body, 4) >> 24) != 0x01);
        }
    }
    // SOME/IP framing
    {
        Bytes body(5, 0xAB);
        Bytes r = someipRequest(0x000A, body, 0x7E01, 0x1234);
        const uint8_t want[] = { 0xB0, 0x06, 0x00, 0x0A, 0, 0, 0, 13, 0x7E, 0x01, 0x12, 0x34, 1, 1, 0, 0 };
        assert(r.size() == 21 && !memcmp(&r[0], want, 16));
    }
    printf("test_kombi_graph OK\n");
    return 0;
}
