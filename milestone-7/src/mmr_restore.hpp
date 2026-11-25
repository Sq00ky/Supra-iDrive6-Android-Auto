// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MMR_RESTORE_HPP
#define MMR_RESTORE_HPP

#include "mmr_pps.hpp"

namespace nbt {

// The actual mm-renderer calls, also supplied by the native failure/order test.
struct MmrRestoreApi {
    int (*contextParameters)(void*, void*);
    int (*outputAttach)(void*, const char*, const char*);
    int (*outputParameters)(void*, int, void*);
    int (*inputAttach)(void*, const char*, const char*);
    int (*inputParameters)(void*, void*);
    int (*play)(void*);
    int (*stop)(void*);
    int (*inputDetach)(void*);
    int (*outputDetach)(void*, unsigned);
    void* (*dictNew)();
    void* (*dictSet)(void*, const char*, const char*);
};

inline bool mmrRestoreChecked(const char* stage, int rc) {
    if (rc < 0) printf("[VideoManager] factory restore %s failed: rc=%d\n", stage, rc);
    return rc >= 0;
}

inline void* mmrRestoreDict(const MmrRestoreApi& api, const std::vector<PpsAttr>& attrs, const char* stage) {
    void* d = api.dictNew();
    if (!d) printf("[VideoManager] factory restore %s: strm_dict_new failed\n", stage);
    for (size_t i = 0; d && i < attrs.size(); ++i) {
        d = api.dictSet(d, attrs[i].name.c_str(), attrs[i].value.c_str());
        if (!d) printf("[VideoManager] factory restore %s: strm_dict_set failed for %s\n", stage, attrs[i].name.c_str());
    }
    return d;   // the mmr_*_parameters call takes ownership
}

// Restore only the supplied factory context. Every captured setting and attachment must succeed.
// On failure undo this attempt's attachments; its owner retains the snapshot for an explicit later retry.
inline bool restoreMmrContext(void* ctx, const MmrCtxSnapshot& snap, const MmrRestoreApi& api) {
    if (!ctx || !snap.hasOutput()) return false;
    if (!api.outputAttach) { printf("[VideoManager] factory restore: mmr_output_attach missing\n"); return false; }
    if (!api.stop) { printf("[VideoManager] factory restore: mmr_stop missing\n"); return false; }
    if (!api.inputDetach) { printf("[VideoManager] factory restore: mmr_input_detach missing\n"); return false; }
    if (!api.outputDetach) { printf("[VideoManager] factory restore: mmr_output_detach missing\n"); return false; }
    bool needsDict = !snap.param.empty();
    if (needsDict && !api.contextParameters) {
        printf("[VideoManager] factory restore: mmr_context_parameters missing\n"); return false;
    }
    for (size_t i = 0; i < snap.outputs.size(); ++i) {
        if (ppsGet(snap.outputs[i], "url").empty()) continue;
        if (!ppsSettings(snap.outputs[i]).empty()) {
            needsDict = true;
            if (!api.outputParameters) { printf("[VideoManager] factory restore: mmr_output_parameters missing\n"); return false; }
        }
    }
    if (snap.hasInput()) {
        if (!api.inputAttach) { printf("[VideoManager] factory restore: mmr_input_attach missing\n"); return false; }
        if (!api.play) { printf("[VideoManager] factory restore: mmr_play missing\n"); return false; }
        if (!ppsSettings(snap.input).empty()) {
            needsDict = true;
            if (!api.inputParameters) { printf("[VideoManager] factory restore: mmr_input_parameters missing\n"); return false; }
        }
    }
    if (needsDict && !api.dictNew) { printf("[VideoManager] factory restore: strm_dict_new missing\n"); return false; }
    if (needsDict && !api.dictSet) { printf("[VideoManager] factory restore: strm_dict_set missing\n"); return false; }

    if (!snap.param.empty()) {
        void* d = mmrRestoreDict(api, snap.param, "context parameters");
        if (!d || !mmrRestoreChecked("mmr_context_parameters", api.contextParameters(ctx, d))) return false;
    }
    std::vector<unsigned> attached;
    bool ok = true, inputAttached = false, playTried = false;
    for (size_t i = 0; ok && i < snap.outputs.size(); ++i) {
        const std::vector<PpsAttr>& output = snap.outputs[i];
        const std::string url = ppsGet(output, "url");
        if (url.empty()) continue;
        int id = api.outputAttach(ctx, url.c_str(), ppsGet(output, "type", "video").c_str());
        if (!mmrRestoreChecked("mmr_output_attach", id)) { ok = false; break; }
        attached.push_back((unsigned)id);
        const std::vector<PpsAttr> settings = ppsSettings(output);
        if (!settings.empty()) {
            void* d = mmrRestoreDict(api, settings, "output parameters");
            ok = d && mmrRestoreChecked("mmr_output_parameters", api.outputParameters(ctx, id, d));
        }
    }
    if (ok && snap.hasInput()) {
        ok = mmrRestoreChecked("mmr_input_attach", api.inputAttach(ctx, ppsGet(snap.input, "url").c_str(),
                              ppsGet(snap.input, "type", "track").c_str()));
        inputAttached = ok;
        const std::vector<PpsAttr> settings = ppsSettings(snap.input);
        if (ok && !settings.empty()) {
            void* d = mmrRestoreDict(api, settings, "input parameters");
            ok = d && mmrRestoreChecked("mmr_input_parameters", api.inputParameters(ctx, d));
        }
        if (ok) { playTried = true; ok = mmrRestoreChecked("mmr_play", api.play(ctx)); }
    }
    if (!ok) {
        if (playTried) mmrRestoreChecked("cleanup mmr_stop", api.stop(ctx));
        if (inputAttached) mmrRestoreChecked("cleanup mmr_input_detach", api.inputDetach(ctx));
        for (size_t i = attached.size(); i > 0; --i) {
            int rc = api.outputDetach(ctx, attached[i - 1]);
            if (rc < 0) printf("[VideoManager] factory restore cleanup mmr_output_detach(%u) failed: rc=%d\n", attached[i - 1], rc);
        }
    }
    return ok;
}

} // namespace nbt
#endif
