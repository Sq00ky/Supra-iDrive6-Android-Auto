// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "most/trace.hpp"
#include "most/client.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace most {

namespace {

enum { kPayloadBytesLogged = 32, kBadRegistryLines = 4 };

uint32_t fnv1a(const uint8_t* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

bool sameRegistry(const std::vector<RegistryEntry>& a, const std::vector<RegistryEntry>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].address != b[i].address || a[i].key != b[i].key) return false;
    return true;
}

} // namespace

TraceFilter::TraceFilter() { memset(types_, 0, sizeof(types_)); }

TraceFilter::Type* TraceFilter::lookup(const View& m) {
    uint32_t start = (((uint32_t)m.fblock << 20) ^ ((uint32_t)m.inst << 13) ^ ((uint32_t)m.fkt << 4) ^ m.op) * 2654435761u;
    for (uint32_t i = 0; i < (uint32_t)kMaxTypes; ++i) {
        Type& t = types_[(start + i) % kMaxTypes];
        if (!t.used) {
            t.used = true;
            t.fblock = m.fblock; t.inst = m.inst; t.fkt = m.fkt; t.op = m.op;
            return &t;
        }
        if (t.fblock == m.fblock && t.inst == m.inst && t.fkt == m.fkt && t.op == m.op) return &t;
    }
    return NULL;
}

TraceFilter::Decision TraceFilter::feed(const View& m) {
    Type* t = lookup(m);
    if (!t) return kTableFull;
    uint32_t h = fnv1a(m.data, m.len);
    bool changed = t->seen == 0 || h != t->hash || m.len != t->len;
    t->hash = h;
    t->len = (uint32_t)m.len;
    ++t->seen;
    if (t->lines >= kLinesPerType) return kSkip;
    if (t->seen > (uint32_t)kFirstLines && !changed) return kSkip;
    ++t->lines;
    return t->lines == kLinesPerType ? kLogFinal : kLog;
}

Trace::Trace() : tableFullLogged_(false), badRegistryLogged_(0) {}

bool Trace::attach(Client& client) {
    if (!client.setTap(&Trace::tapTramp, this)) return false;
    if (!client.setInfoTap(&Trace::infoTramp, this)) return false;
    printf("[MOSTLOG] logging MOST traffic: first %d of each message type, then changes, at most %d lines per type\n",
           (int)TraceFilter::kFirstLines, (int)TraceFilter::kLinesPerType);
    return true;
}

void Trace::tapTramp(const View& m, MsgClass c, void* self) { static_cast<Trace*>(self)->onMessage(m, c); }
void Trace::infoTramp(uint32_t id, const uint8_t* p, uint32_t len, void* self) { static_cast<Trace*>(self)->onInfo(id, p, len); }

void Trace::onMessage(const View& m, MsgClass c) {
    TraceFilter::Decision d = filter_.feed(m);
    if (d == TraceFilter::kTableFull && !tableFullLogged_) {
        tableFullLogged_ = true;
        printf("[MOSTLOG] more than %d message types: new ones are not logged\n", (int)TraceFilter::kMaxTypes);
    }
    if (d != TraceFilter::kLog && d != TraceFilter::kLogFinal) return;
    printf("[MOSTLOG] +%u %s tx=%u var=%u/%u %04X->%04X %02X/%02X fkt=%03X %s(%X) tag=%u len=%u:", m.rxMs, className(c),
           m.txType, m.varTag, m.varVal, m.src, m.dst, m.fblock, m.inst, m.fkt, opName(m.op), m.op, m.hdrTag, (unsigned)m.len);
    for (size_t i = 0; i < m.len && i < (size_t)kPayloadBytesLogged; ++i) printf(" %02x", m.data[i]);
    if (m.len > (size_t)kPayloadBytesLogged) printf(" ...");
    if (d == TraceFilter::kLogFinal) printf("  (line cap reached for this message type)");
    printf("\n");
}

void Trace::onInfo(uint32_t id, const uint8_t* p, uint32_t len) {
    if (id == kInfoChangeInstId && len >= 3) {
        printf("[MOSTLOG] instance change: key %04X -> inst %02X\n", rdLe16(p), p[2]);
        return;
    }
    if (id != kAttrRegistry) return;
    std::vector<RegistryEntry> reg;
    if (!decodeRegistry(p, len, reg)) {
        if (badRegistryLogged_ < (unsigned)kBadRegistryLines) {
            ++badRegistryLogged_;
            printf("[MOSTLOG] registry: %u bytes that do not decode\n", len);
        }
        return;
    }
    if (sameRegistry(reg, registry_)) return;
    registry_ = reg;
    printf("[MOSTLOG] registry: %u entries (address, FBlock<<8|Inst)\n", (unsigned)reg.size());
    for (size_t i = 0; i < reg.size(); ++i) printf("[MOSTLOG]   %04X %04X\n", reg[i].address, reg[i].key);
}

} // namespace most
} // namespace nbt
