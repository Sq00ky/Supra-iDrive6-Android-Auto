// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Reading iDrive's own SOME/IP requests to the KOMBI off the wire (cluster/ric_media.hpp is the user). Header-only and
// free of I/O: an Ethernet frame goes in, whole SOME/IP messages come out. Host-tested (tests/test_ric_sniff.cpp)
// against a frame from the car capture of 2026-11-10.
//
// Nothing is sent, nothing is changed and no interface is put into promiscuous mode: the head unit's own traffic to
// the KOMBI is already visible on its interface (that is how tcpdump recorded it).
#ifndef CLUSTER_RIC_SNIFF_HPP
#define CLUSTER_RIC_SNIFF_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>

#include "cluster/ric_list.hpp"

namespace nbt {
namespace riclist {

// One TCP segment of an IPv4 packet. data points into the frame it was parsed from.
struct TcpSeg {
    uint32_t src, dst;      // host byte order
    uint16_t sport, dport;
    uint32_t seq;
    bool syn;
    const uint8_t* data;
    size_t len;
};

// An Ethernet frame (with or without one 802.1Q tag) -> its TCP segment. False for anything else, for IP fragments
// and for frames too short for their own headers. A frame the capture cut short gives the bytes that are there.
inline bool parseTcp(const uint8_t* f, size_t n, TcpSeg* s) {
    if (n < 14) return false;
    size_t o = 14;
    unsigned et = ((unsigned)f[12] << 8) | f[13];
    if (et == 0x8100) {
        if (n < 18) return false;
        et = ((unsigned)f[16] << 8) | f[17];
        o = 18;
    }
    if (et != 0x0800 || n < o + 20) return false;
    const uint8_t* ip = f + o;
    if ((ip[0] >> 4) != 4 || ip[9] != 6) return false;
    const size_t ihl = (size_t)(ip[0] & 15) * 4;
    if (ihl < 20) return false;
    if ((((unsigned)ip[6] & 0x1F) << 8 | ip[7]) != 0) return false;     // not the first fragment
    size_t tot = ((size_t)ip[2] << 8) | ip[3];
    const size_t avail = n - o;
    if (tot > avail) tot = avail;
    if (tot < ihl + 20) return false;
    const uint8_t* t = ip + ihl;
    const size_t doff = (size_t)(t[12] >> 4) * 4;
    if (doff < 20 || ihl + doff > tot) return false;
    s->src = ((uint32_t)ip[12] << 24) | ((uint32_t)ip[13] << 16) | ((uint32_t)ip[14] << 8) | ip[15];
    s->dst = ((uint32_t)ip[16] << 24) | ((uint32_t)ip[17] << 16) | ((uint32_t)ip[18] << 8) | ip[19];
    s->sport = (uint16_t)((t[0] << 8) | t[1]);
    s->dport = (uint16_t)((t[2] << 8) | t[3]);
    s->seq = ((uint32_t)t[4] << 24) | ((uint32_t)t[5] << 16) | ((uint32_t)t[6] << 8) | t[7];
    s->syn = (t[13] & 0x02) != 0;
    s->data = t + doff;
    s->len = tot - ihl - doff;
    return true;
}

struct SomeipMsg {
    uint16_t service, method, client, session, sport;
    uint8_t type, rc;
    Bytes body;
};

// SOME/IP messages out of the TCP segments of a few connections (one stream per source port, oldest dropped). A
// retransmission is ignored; after a missed segment, or bytes that are not a SOME/IP header, the stream starts again
// at the next segment (iDrive sends one message per segment, so that is a message boundary).
class StreamSniffer {
public:
    enum { kStreams = 4, kMaxMessage = 4096 };

    StreamSniffer() : tick_(0) {
        for (int i = 0; i < kStreams; ++i) s_[i].used = false;
    }

    void feed(const TcpSeg& seg, std::vector<SomeipMsg>* out) {
        if (!seg.len && !seg.syn) return;
        Stream* st = stream(seg.sport);
        if (seg.syn) {
            st->buf.clear();
            st->next = seg.seq + 1;
            st->synced = true;
            if (!seg.len) return;
        } else if (!st->synced) {
            st->next = seg.seq;
            st->synced = true;
        }
        const int32_t d = (int32_t)(seg.seq - st->next);
        if (d < 0) return;                       // seen before
        if (d > 0) st->buf.clear();              // a segment is missing
        st->buf.insert(st->buf.end(), seg.data, seg.data + seg.len);
        st->next = seg.seq + (uint32_t)seg.len;
        size_t o = 0;
        while (st->buf.size() - o >= 16) {
            const uint8_t* p = &st->buf[o];
            const uint32_t len = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 8) | p[7];
            if (len < 8 || len > (uint32_t)kMaxMessage || p[12] != 0x01) {      // not a SOME/IP header: start again
                st->buf.clear();
                o = 0;
                break;
            }
            if (st->buf.size() - o < 8 + (size_t)len) break;
            SomeipMsg m;
            m.service = (uint16_t)((p[0] << 8) | p[1]);
            m.method = (uint16_t)((p[2] << 8) | p[3]);
            m.client = (uint16_t)((p[8] << 8) | p[9]);
            m.session = (uint16_t)((p[10] << 8) | p[11]);
            m.type = p[14];
            m.rc = p[15];
            m.sport = seg.sport;
            m.body.assign(p + 16, p + 8 + len);
            out->push_back(m);
            o += 8 + (size_t)len;
        }
        if (o) st->buf.erase(st->buf.begin(), st->buf.begin() + (long)o);
    }

private:
    struct Stream {
        bool used, synced;
        uint16_t port;
        uint32_t next;
        unsigned long stamp;
        Bytes buf;
    };

    Stream* stream(uint16_t port) {
        Stream* oldest = &s_[0];
        for (int i = 0; i < kStreams; ++i) {
            if (s_[i].used && s_[i].port == port) { s_[i].stamp = ++tick_; return &s_[i]; }
            if (!s_[i].used) { oldest = &s_[i]; break; }
            if (s_[i].stamp < oldest->stamp) oldest = &s_[i];
        }
        oldest->used = true;
        oldest->synced = false;
        oldest->port = port;
        oldest->next = 0;
        oldest->stamp = ++tick_;
        oldest->buf.clear();
        return oldest;
    }

    Stream s_[kStreams];
    unsigned long tick_;
};

// The kernel-side packet filter (classic BPF, same layout as struct bpf_insn): TCP over IPv4 to one address, with or
// without one 802.1Q tag. Ports and everything else are checked in parseTcp's caller.
struct FilterInsn {
    uint16_t code;
    uint8_t jt, jf;
    uint32_t k;
};
enum { kFilterLen = 15 };

inline FilterInsn filterStmt(uint16_t code, uint32_t k) {
    FilterInsn i;
    i.code = code; i.jt = 0; i.jf = 0; i.k = k;
    return i;
}
// "if A == k go to instruction t, else to f" placed at instruction self.
inline FilterInsn filterJeq(uint32_t k, int self, int t, int f) {
    FilterInsn i;
    i.code = 0x15; i.jt = (uint8_t)(t - self - 1); i.jf = (uint8_t)(f - self - 1); i.k = k;
    return i;
}
inline void kombiFilter(uint32_t ip, FilterInsn* f) {
    f[0] = filterStmt(0x28, 12);            // A = ethertype
    f[1] = filterJeq(0x0800, 1, 2, 6);
    f[2] = filterStmt(0x30, 23);            // A = IP protocol
    f[3] = filterJeq(6, 3, 4, 14);
    f[4] = filterStmt(0x20, 30);            // A = destination address
    f[5] = filterJeq(ip, 5, 13, 14);
    f[6] = filterJeq(0x8100, 6, 7, 14);     // one VLAN tag: the same three checks, 4 bytes further on
    f[7] = filterStmt(0x28, 16);
    f[8] = filterJeq(0x0800, 8, 9, 14);
    f[9] = filterStmt(0x30, 27);
    f[10] = filterJeq(6, 10, 11, 14);
    f[11] = filterStmt(0x20, 34);
    f[12] = filterJeq(ip, 12, 13, 14);
    f[13] = filterStmt(0x06, 0xFFFF);       // take the whole packet
    f[14] = filterStmt(0x06, 0);            // drop
}

}  // namespace riclist
}  // namespace nbt

#endif
