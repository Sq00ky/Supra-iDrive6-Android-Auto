// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_VOLUME_INPUT_HPP
#define CAR_VOLUME_INPUT_HPP

#include "car/volume_level.hpp"
#include "most/client.hpp"

namespace nbt {
namespace car {

// level 0..max as the car applied it (max > 0). On the DSI callback thread: see the handler rules in most/client.hpp.
typedef void (*VolumeCb)(int level, int max, void* user);

// The car's master volume for the daemon's volume overlay. A consumer of the observe-only MOST client: it receives
// copies of AudioMaster's level status (car/volume_level.hpp) and adds no keys and no requests. The HMI keeps receiving
// the same messages and still draws its own volume bar.
class VolumeInput {
public:
    VolumeInput();

    // Before the client's start(). False: the client has no room for the route, and the volume stays silent.
    bool attach(most::Client& client, VolumeCb cb, void* user);

private:
    static void statusTramp(const most::View& m, void* self);
    static void linkTramp(most::LinkState s, void* self);
    void onStatus(const most::View& m);
    void onLink(most::LinkState s);

    VolumeLevel level_;
    VolumeCb cb_;
    void* user_;
};

} // namespace car
} // namespace nbt

#endif
