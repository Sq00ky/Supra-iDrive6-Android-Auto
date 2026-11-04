// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_CCS_INPUT_HPP
#define CAR_CCS_INPUT_HPP

#include <stddef.h>
#include <stdint.h>
#include "car/ccs_config.hpp"
#include "car/ccs_decoder.hpp"
#include "car/ptt.hpp"
#include "most/client.hpp"

namespace nbt {
namespace car {

typedef void (*CcsEventCb)(const CcsEvent& ev, void* user);

// iDrive controller input for the daemon (default on; NBT_KNOB=0 disables).
// A consumer of the observe-only MOST client: it receives copies of the controller's status messages (FBlock 0xE5, any
// instance: CcsDecoder picks its own) and of the steering-wheel voice button (FBlock 0x15, any instance), and decodes
// them. Nothing is ever sent to the controller. The HMI keeps receiving the same events.
// The event callback runs on the DSI callback thread: see the handler rules in most/client.hpp.
class CcsInput {
public:
    CcsInput();

    // Before the client's start(). False: the client has no room for the routes or keys, and the controller stays silent.
    bool attach(most::Client& client, const CcsConfig& cfg, CcsEventCb cb, void* user);

private:
    static void controllerTramp(const most::View& m, void* self);
    static void pttTramp(const most::View& m, void* self);
    void onController(const most::View& m);
    void onPtt(const most::View& m);

    CcsDecoder dec_;
    PttTracker ptt_;
    bool pttAssistant_;
    CcsEventCb cb_;
    void* user_;
    unsigned statusLogged_;    // CCS35up status messages seen, for the first few log lines
    unsigned ccsLogged_;       // older CCS protocol lines logged
};

} // namespace car
} // namespace nbt

#endif
