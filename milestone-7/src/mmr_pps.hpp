// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MMR_PPS_HPP
#define MMR_PPS_HPP

// PPS objects of an mm-renderer context (/pps/services/multimedia/renderer/context/<name>/{output0,input,param}).
// Each line is "name:encoding:value" (encoding usually empty: "url::screen:/any?..."); the first line is the object
// header "[n]@output0" / "@output0". Used to snapshot the factory CarPlay video context before nbt-aad replaces it,
// and to rebuild it afterwards (see VideoManager::handBackDecoder). Plain C++98, host-tested (tests/test_mmr_pps.cpp).

#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>

namespace nbt {

struct PpsAttr {
    std::string name, value;
};

inline std::vector<PpsAttr> ppsParse(const std::string& text) {
    std::vector<PpsAttr> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty() || line[0] == '@' || line[0] == '[') continue;   // object header
        size_t c1 = line.find(':');
        if (c1 == std::string::npos || c1 == 0) continue;
        size_t c2 = line.find(':', c1 + 1);
        if (c2 == std::string::npos) continue;
        PpsAttr a;
        a.name = line.substr(0, c1);
        a.value = line.substr(c2 + 1);
        out.push_back(a);
    }
    return out;
}

inline std::string ppsGet(const std::vector<PpsAttr>& attrs, const char* name, const char* def = "") {
    for (size_t i = 0; i < attrs.size(); ++i)
        if (attrs[i].name == name) return attrs[i].value;
    return def;
}

inline bool ppsReadFile(const char* path, std::string& out) {
    out.clear();
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

inline bool ppsWriteFile(const char* path, const std::string& data) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

// "output0" -> 0, "output12" -> 12; any other PPS object name of a context directory -> -1.
inline int ppsOutputId(const char* name) {
    if (!name || strncmp(name, "output", 6) != 0 || !name[6]) return -1;
    int id = 0;
    for (const char* p = name + 6; *p; ++p) {
        if (*p < '0' || *p > '9' || id > 100000) return -1;
        id = id * 10 + (*p - '0');
    }
    return id;
}

// The settings of an object (everything but url / type), to hand back through mmr_*_parameters().
inline std::vector<PpsAttr> ppsSettings(const std::vector<PpsAttr>& a) {
    std::vector<PpsAttr> out;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].name != "url" && a[i].name != "type") out.push_back(a[i]);
    return out;
}

// What the factory CarPlay had attached to its decoder context (cpvideoctx<pid>) when nbt-aad detached it
// (v1.9.50-cpdetach): Android Auto rebuilds exactly this when it hands the decoder back
// (VideoManager::reattachFactoryContext). Outputs are kept sorted by id.
struct MmrCtxSnapshot {
    std::vector<int> outputIds;
    std::vector<std::vector<PpsAttr> > outputs;   // parallel to outputIds
    std::vector<PpsAttr> input;
    std::vector<PpsAttr> param;
    void clear() { outputIds.clear(); outputs.clear(); input.clear(); param.clear(); }
    bool hasOutput() const {
        for (size_t i = 0; i < outputs.size(); ++i)
            if (!ppsGet(outputs[i], "url").empty()) return true;
        return false;
    }
    bool hasInput() const { return !ppsGet(input, "url").empty(); }
    // One PPS object of the context directory, by its object (file) name.
    void add(const std::string& obj, const std::string& text) {
        std::vector<PpsAttr> a = ppsParse(text);
        int id = ppsOutputId(obj.c_str());
        if (id >= 0) {
            size_t at = 0;
            while (at < outputIds.size() && outputIds[at] < id) ++at;
            outputIds.insert(outputIds.begin() + at, id);
            outputs.insert(outputs.begin() + at, a);
        } else if (obj == "input") {
            input = a;
        } else if (obj == "param") {
            param = a;
        }
    }
};

} // namespace nbt

#endif // MMR_PPS_HPP
