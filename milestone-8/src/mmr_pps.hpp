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

} // namespace nbt

#endif // MMR_PPS_HPP
