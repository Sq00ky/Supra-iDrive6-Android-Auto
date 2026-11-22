// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "mmr_restore.hpp"
#include <assert.h>
#include <set>

using namespace nbt;

struct Fake {
    std::vector<std::string> calls;
    std::set<unsigned> outputs;
    bool input;
    int failAt, call, dictAt, dictionaries, setAt, sets, nextOutput;
    Fake() : input(false), failAt(0), call(0), dictAt(0), dictionaries(0), setAt(0), sets(0), nextOutput(10) {}
    bool run(const std::string& name) { calls.push_back(name); return ++call != failAt; }
};

static Fake* f;
typedef std::vector<PpsAttr> Dict;
static void* dictNew() {
    if (++f->dictionaries == f->dictAt) return NULL;
    return new Dict;
}
static void* dictSet(void* d, const char* name, const char* value) {
    if (++f->sets == f->setAt) { delete (Dict*)d; return NULL; }
    PpsAttr a; a.name = name; a.value = value; ((Dict*)d)->push_back(a); return d;
}
static void checkDict(void* d, const char* key, const char* value) {
    assert(d && ((Dict*)d)->size() == 1 && ppsGet(*(Dict*)d, key) == value);
    delete (Dict*)d;
}
static int contextParameters(void* ctx, void* d) {
    assert(ctx == f); checkDict(d, "prefetch_mode", "0"); return f->run("context params") ? 0 : -1;
}
static int outputAttach(void* ctx, const char* url, const char* type) {
    assert(ctx == f && std::string(type) == "video");
    assert(std::string(url) == "screen:/factory-a" || std::string(url) == "screen:/factory-b");
    if (!f->run(std::string("output attach ") + url)) return -1;
    unsigned id = ++f->nextOutput; assert(f->outputs.insert(id).second); return (int)id;
}
static int outputParameters(void* ctx, int id, void* d) {
    assert(ctx == f && f->outputs.count((unsigned)id));
    checkDict(d, "cadence", "1"); return f->run("output params") ? 0 : -1;
}
static int inputAttach(void* ctx, const char* url, const char* type) {
    assert(ctx == f && !f->input && std::string(url) == "mdevpush:/factory" && std::string(type) == "track");
    if (!f->run("input attach")) return -1;
    f->input = true; return 0;
}
static int inputParameters(void* ctx, void* d) {
    assert(ctx == f && f->input); checkDict(d, "ColorSpaceMode", "1"); return f->run("input params") ? 0 : -1;
}
static int play(void* ctx) { assert(ctx == f && f->input && f->outputs.size() == 2); return f->run("play") ? 0 : -1; }
static int stop(void* ctx) { assert(ctx == f); f->run("stop"); return 0; }
static int inputDetach(void* ctx) { assert(ctx == f && f->input); f->run("input detach"); f->input = false; return 0; }
static int outputDetach(void* ctx, unsigned id) {
    assert(ctx == f && f->outputs.erase(id) == 1);
    char name[40]; snprintf(name, sizeof(name), "output detach %u", id); f->run(name); return 0;
}
static MmrRestoreApi api() {
    MmrRestoreApi a = { contextParameters, outputAttach, outputParameters, inputAttach, inputParameters,
                        play, stop, inputDetach, outputDetach, dictNew, dictSet };
    return a;
}
static MmrCtxSnapshot snapshot() {
    MmrCtxSnapshot s;
    s.add("param", "@param\nprefetch_mode::0\n");
    s.add("output0", "@output0\nurl::screen:/factory-a\ntype::video\ncadence::1\n");
    s.add("output7", "@output7\nurl::screen:/factory-b\ntype::video\ncadence::1\n");
    s.add("input", "@input\nurl::mdevpush:/factory\ntype::track\nColorSpaceMode::1\n");
    return s;
}

int main() {
    const MmrCtxSnapshot s = snapshot();
    Fake good; f = &good;
    assert(restoreMmrContext(f, s, api()));
    const char* order[] = { "context params", "output attach screen:/factory-a", "output params",
                           "output attach screen:/factory-b", "output params", "input attach", "input params", "play" };
    assert(good.calls.size() == 8 && good.input && good.outputs.size() == 2);
    for (size_t i = 0; i < 8; ++i) assert(good.calls[i] == order[i]);

    // Every required MMR failure is false, unwinds this attempt, and permits a complete later retry.
    for (int fail = 1; fail <= 8; ++fail) {
        Fake broken; f = &broken; f->failAt = fail;
        assert(!restoreMmrContext(f, s, api()));
        assert(!f->input && f->outputs.empty());
        if (fail == 8) {
            assert(f->calls[8] == "stop" && f->calls[9] == "input detach");
            assert(f->calls[10] == "output detach 12" && f->calls[11] == "output detach 11");
        }
        f->failAt = 0; f->call = 0;
        assert(restoreMmrContext(f, s, api()));
    }
    // Dictionary creation or population cannot silently skip captured settings.
    for (int fail = 1; fail <= 4; ++fail) {
        Fake noDict; f = &noDict; f->dictAt = fail;
        assert(!restoreMmrContext(f, s, api())); assert(!f->input && f->outputs.empty());
        Fake noSetting; f = &noSetting; f->setAt = fail;
        assert(!restoreMmrContext(f, s, api())); assert(!f->input && f->outputs.empty());
    }
    // Missing required APIs are rejected before attaching anything.
    for (int missing = 0; missing < 11; ++missing) {
        Fake unavailable; f = &unavailable; MmrRestoreApi a = api();
        switch (missing) {
            case 0: a.contextParameters = NULL; break;
            case 1: a.outputAttach = NULL; break;
            case 2: a.outputParameters = NULL; break;
            case 3: a.inputAttach = NULL; break;
            case 4: a.inputParameters = NULL; break;
            case 5: a.play = NULL; break;
            case 6: a.stop = NULL; break;
            case 7: a.inputDetach = NULL; break;
            case 8: a.outputDetach = NULL; break;
            case 9: a.dictNew = NULL; break;
            case 10: a.dictSet = NULL; break;
        }
        assert(!restoreMmrContext(f, s, a)); assert(f->calls.empty());
    }
    // An idle factory context with no captured input is restored without inventing input or playing it.
    Fake idle; f = &idle; MmrCtxSnapshot noInput = s; noInput.input.clear();
    MmrRestoreApi idleApi = api(); idleApi.inputAttach = NULL; idleApi.inputParameters = NULL; idleApi.play = NULL;
    assert(restoreMmrContext(f, noInput, idleApi)); assert(!f->input && f->calls.size() == 5);
    // Empty settings need no dictionary/parameter API; absent outputs are never reported as restored.
    Fake minimal; f = &minimal; MmrCtxSnapshot plain;
    plain.add("output0", "@output0\nurl::screen:/factory-a\n");
    MmrRestoreApi plainApi = idleApi;
    plainApi.contextParameters = NULL; plainApi.outputParameters = NULL; plainApi.dictNew = NULL; plainApi.dictSet = NULL;
    assert(restoreMmrContext(f, plain, plainApi));
    MmrCtxSnapshot empty; assert(!restoreMmrContext(f, empty, api()));
    assert(!restoreMmrContext(NULL, s, api()));
    printf("test_mmr_restore OK\n");
    return 0;
}
