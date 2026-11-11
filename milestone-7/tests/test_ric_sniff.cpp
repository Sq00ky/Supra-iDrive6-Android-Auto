// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: SOME/IP messages out of sniffed Ethernet frames (src/cluster/ric_sniff.hpp), against a real frame from the
// car capture, and the kernel packet filter run through an interpreter of the instructions it uses.
#include "cluster/ric_sniff.hpp"
#include "ric_frames.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace nbt::riclist;
using namespace ricf;

static const uint32_t kHu = 0xA030C777, kKombi = 0xA030C760;

// Classic BPF as the kernel runs it, for the five instruction kinds kombiFilter uses. A load past the end drops.
static unsigned runFilter(const FilterInsn* f, int n, const Bytes& p) {
    uint32_t a = 0;
    for (int pc = 0; pc < n; ++pc) {
        const FilterInsn& i = f[pc];
        if (i.code == 0x28) { if (i.k + 2 > p.size()) return 0; a = ((uint32_t)p[i.k] << 8) | p[i.k + 1]; }
        else if (i.code == 0x30) { if (i.k + 1 > p.size()) return 0; a = p[i.k]; }
        else if (i.code == 0x20) {
            if (i.k + 4 > p.size()) return 0;
            a = ((uint32_t)p[i.k] << 24) | ((uint32_t)p[i.k + 1] << 16) | ((uint32_t)p[i.k + 2] << 8) | p[i.k + 3];
        }
        else if (i.code == 0x15) pc += (a == i.k) ? i.jt : i.jf;
        else if (i.code == 0x06) return i.k;
        else assert(!"instruction the interpreter does not know");
    }
    assert(!"ran off the end of the filter");
    return 0;
}

int main() {
    // ---- the real frame --------------------------------------------------------------------------------------
    const Bytes pkt = hex(kListPacket);
    assert(pkt.size() == 178);
    TcpSeg s;
    assert(parseTcp(&pkt[0], pkt.size(), &s));
    assert(s.src == kHu && s.dst == kKombi && s.sport == 65486 && s.dport == 30502);
    assert(s.seq == 0x32EBBAB2u && !s.syn && s.len == 124);
    StreamSniffer sn;
    std::vector<SomeipMsg> out;
    sn.feed(s, &out);
    assert(out.size() == 1);
    assert(out[0].service == 0xB00E && out[0].method == 0x000B && out[0].client == 0x0001 && out[0].session == 0x000F);
    assert(out[0].type == 0x00 && out[0].sport == 65486);
    assert(out[0].body == hex(kListBody));
    // the same segment again is a retransmission
    out.clear();
    sn.feed(s, &out);
    assert(out.empty());

    // ---- short / foreign frames ------------------------------------------------------------------------------
    for (size_t n = 0; n < 54; ++n) assert(!parseTcp(&pkt[0], n, &s));          // shorter than its own headers
    assert(parseTcp(&pkt[0], 54, &s) && s.len == 0);                             // headers only (cut capture)
    assert(parseTcp(&pkt[0], 100, &s) && s.len == 46);
    const Bytes body = hex(kCommit);
    const Bytes msg = someip(0xB00E, 0x0004, 0x0001, 0x0010, 0x00, body);
    Bytes f = ethTcp(kHu, 65486, kKombi, 30502, 1000, msg, false, false, 17);    // UDP
    assert(!parseTcp(&f[0], f.size(), &s));
    f = ethTcp(kHu, 65486, kKombi, 30502, 1000, msg);
    f[12] = 0x08; f[13] = 0x06;                                                  // ARP
    assert(!parseTcp(&f[0], f.size(), &s));
    f = ethTcp(kHu, 65486, kKombi, 30502, 1000, msg);
    f[14 + 7] = 0x08;                                                            // a later IP fragment
    assert(!parseTcp(&f[0], f.size(), &s));
    f = ethTcp(kHu, 65486, kKombi, 30502, 1000, msg, false, true);               // one VLAN tag
    assert(parseTcp(&f[0], f.size(), &s) && s.dst == kKombi && s.dport == 30502 && s.len == msg.size());

    // ---- reassembly ------------------------------------------------------------------------------------------
    {
        StreamSniffer a;
        std::vector<SomeipMsg> o;
        const Bytes m1 = someip(0xB00E, 0x000B, 1, 0x20, 0, hex(kListBody));
        const Bytes m2 = someip(0xB00E, 0x0004, 1, 0x21, 0, hex(kCommit));
        // a SYN, then one message split over two segments, then two messages in one segment
        Bytes g = ethTcp(kHu, 40000, kKombi, 30502, 499, Bytes(), true);
        assert(parseTcp(&g[0], g.size(), &s)); a.feed(s, &o);
        Bytes h1(m1.begin(), m1.begin() + 30), h2(m1.begin() + 30, m1.end());
        g = ethTcp(kHu, 40000, kKombi, 30502, 500, h1);
        assert(parseTcp(&g[0], g.size(), &s)); a.feed(s, &o);
        assert(o.empty());
        g = ethTcp(kHu, 40000, kKombi, 30502, 530, h2);
        assert(parseTcp(&g[0], g.size(), &s)); a.feed(s, &o);
        assert(o.size() == 1 && o[0].method == 0x000B && o[0].body == hex(kListBody));
        Bytes two = m2;
        two.insert(two.end(), m1.begin(), m1.end());
        g = ethTcp(kHu, 40000, kKombi, 30502, 500 + (uint32_t)m1.size(), two);
        assert(parseTcp(&g[0], g.size(), &s)); a.feed(s, &o);
        assert(o.size() == 3 && o[1].method == 0x0004 && o[1].session == 0x21 && o[2].method == 0x000B);
        // a missed segment: the half message is dropped and the stream starts again at the next segment
        uint32_t seq = 500 + (uint32_t)m1.size() + (uint32_t)two.size();
        g = ethTcp(kHu, 40000, kKombi, 30502, seq, h1);
        assert(parseTcp(&g[0], g.size(), &s)); a.feed(s, &o);
        seq += 30 + 500;                                                          // 500 bytes never seen
        g = ethTcp(kHu, 40000, kKombi, 30502, seq, m2);
        assert(parseTcp(&g[0], g.size(), &s)); a.feed(s, &o);
        assert(o.size() == 4 && o[3].method == 0x0004);
        // bytes that are no SOME/IP header are dropped, and the next segment parses again
        seq += (uint32_t)m2.size();
        g = ethTcp(kHu, 40000, kKombi, 30502, seq, Bytes(40, 0xEE));
        assert(parseTcp(&g[0], g.size(), &s)); a.feed(s, &o);
        assert(o.size() == 4);
        g = ethTcp(kHu, 40000, kKombi, 30502, seq + 40, m2);
        assert(parseTcp(&g[0], g.size(), &s)); a.feed(s, &o);
        assert(o.size() == 5);
        // a stream first seen mid-connection (no SYN) starts at its first segment; other ports are separate streams
        for (uint16_t port = 41000; port < 41006; ++port) {
            g = ethTcp(kHu, port, kKombi, 30502, 7777, m2);
            assert(parseTcp(&g[0], g.size(), &s)); a.feed(s, &o);
        }
        assert(o.size() == 11 && o[10].sport == 41005);
        // a length that cannot be a message
        Bytes bad = m2;
        bad[4] = 0x7F;
        g = ethTcp(kHu, 42000, kKombi, 30502, 1, bad);
        assert(parseTcp(&g[0], g.size(), &s)); a.feed(s, &o);
        assert(o.size() == 11);
    }

    // ---- the kernel filter -----------------------------------------------------------------------------------
    FilterInsn flt[kFilterLen];
    kombiFilter(kKombi, flt);
    assert(runFilter(flt, kFilterLen, pkt) == 0xFFFF);
    assert(runFilter(flt, kFilterLen, ethTcp(kHu, 1, kKombi, 30502, 1, msg)) == 0xFFFF);
    assert(runFilter(flt, kFilterLen, ethTcp(kHu, 1, kKombi, 30502, 1, msg, false, true)) == 0xFFFF);     // VLAN tag
    assert(runFilter(flt, kFilterLen, ethTcp(kHu, 1, kKombi + 1, 30502, 1, msg)) == 0);                   // other host
    assert(runFilter(flt, kFilterLen, ethTcp(kKombi, 30502, kHu, 1, 1, msg)) == 0);                       // the reply
    assert(runFilter(flt, kFilterLen, ethTcp(kHu, 1, kKombi, 30502, 1, msg, false, false, 17)) == 0);     // UDP
    assert(runFilter(flt, kFilterLen, ethTcp(kHu, 1, kKombi + 1, 30502, 1, msg, false, true)) == 0);
    f = ethTcp(kHu, 1, kKombi, 30502, 1, msg);
    f[12] = 0x86; f[13] = 0xDD;                                                                           // IPv6
    assert(runFilter(flt, kFilterLen, f) == 0);
    assert(runFilter(flt, kFilterLen, Bytes(20, 0)) == 0);                                                // runt
    for (int i = 0; i < kFilterLen; ++i)        // every jump lands inside the program
        if (flt[i].code == 0x15) assert(i + 1 + flt[i].jt < kFilterLen && i + 1 + flt[i].jf < kFilterLen);

    // ---- the records of a BPF read -------------------------------------------------------------------------------
    {
        // the header as the kernel writes it on the car: 18 bytes (not sizeof(struct bpf_hdr) = 20), records padded to 4
        Bytes buf;
        for (int r = 0; r < 3; ++r) {
            const Bytes& fr = r == 1 ? msg : pkt;                   // 178, 20, 178 bytes
            const uint32_t cap = (uint32_t)fr.size();
            const uint16_t hl = 18;
            Bytes rec(18, 0xAA);
            memcpy(&rec[8], &cap, 4);
            memcpy(&rec[12], &cap, 4);
            memcpy(&rec[16], &hl, 2);
            rec.insert(rec.end(), fr.begin(), fr.end());
            while (rec.size() % 4) rec.push_back(0xBB);
            buf.insert(buf.end(), rec.begin(), rec.end());
        }
        std::vector<Bytes> got;
        assert(bpfRecords(&buf[0], buf.size(), 4, &got) == 3 && got.size() == 3);
        assert(got[0] == pkt && got[1] == msg && got[2] == pkt);
        got.clear();
        assert(bpfRecords(&buf[0], buf.size() - 1, 4, &got) == 2);   // the last record is cut: not delivered
        got.clear();
        assert(bpfRecords(&buf[0], 17, 4, &got) == 0 && bpfRecords(&buf[0], 0, 4, &got) == 0);
        Bytes bad = buf;
        bad[16] = 4;                                                 // a header length no record can have
        assert(bpfRecords(&bad[0], bad.size(), 4, &got) == 0);
        bad = buf;
        bad[11] = 0x7F;                                              // a captured length past the buffer
        assert(bpfRecords(&bad[0], bad.size(), 4, &got) == 0 && got.empty());
        // a 20-byte header (a kernel that pads it) works the same way
        Bytes rec(20, 0);
        const uint32_t cap = (uint32_t)msg.size();
        const uint16_t hl = 20;
        memcpy(&rec[8], &cap, 4);
        memcpy(&rec[16], &hl, 2);
        rec.insert(rec.end(), msg.begin(), msg.end());
        assert(bpfRecords(&rec[0], rec.size(), 4, &got) == 1 && got[0] == msg);
    }
    printf("test_ric_sniff OK\n");
    return 0;
}
