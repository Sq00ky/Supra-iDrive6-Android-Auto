// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "car/gps.hpp"

#include "most/client.hpp"

#include <stdio.h>

namespace nbt {
namespace car {

bool CarGps::attach(most::Client& client) {
    if (!client.addRoute(kCarGpsFBlock, kCarGpsInst, kCarGpsFkt, most::kOpStatus, &CarGps::statusTramp, this)) {
        printf("[GPS] no room on the MOST client: car GPS disabled\n");
        return false;
    }
    printf("[GPS] car GPS: reading MOST %02X/%02X fkt %03X\n", kCarGpsFBlock, kCarGpsInst, kCarGpsFkt);
    return true;
}

void CarGps::statusTramp(const most::View& m, void* self) { static_cast<CarGps*>(self)->onStatus(m); }

void CarGps::onStatus(const most::View& m) {
    CarFix f;
    memset(&f, 0, sizeof(f));
    CarGpsFilter::Result r = filter_.feed(m.data, m.len, f);
    if (r.repeat) return;
    if (r.stateChanged) {
        if (r.ok)
            printf("[GPS] car GPS fix: %.6f, %.6f (UTC %04u-%02u-%02u %02u:%02u:%02u)\n", f.lat_e7 / 1e7, f.lon_e7 / 1e7,
                   f.year, f.mon, f.day, f.hh, f.mm, f.ss);
        else
            printf("[GPS] car GPS: no fix (%02X/%02X fkt %03X, %u bytes)\n", m.fblock, m.inst, m.fkt, (unsigned)m.len);
    }
    if (!r.ok) return;
    // Speed, heading, height and the DOPs are raw values with units still to confirm (car/gps.hpp). Position and UTC
    // time on the same line let a drive log be checked against the distance and direction travelled between lines.
    if (r.logFields) {
        if (f.ext)
            printf("[GPS] fix #%u %02u:%02u:%02u %.6f, %.6f fix=%u hdop=%u vdop=%u speed=%d heading=%u height=%d%s\n",
                   r.fixNo, f.hh, f.mm, f.ss, f.lat_e7 / 1e7, f.lon_e7 / 1e7, f.gpsFix, f.hdop, f.vdop, f.speed,
                   f.heading, f.height, r.fixChanged ? " (fix changed)" : "");
        else
            printf("[GPS] fix #%u %.6f, %.6f (%u-byte payload, no extra fields)\n", r.fixNo, f.lat_e7 / 1e7, f.lon_e7 / 1e7,
                   (unsigned)m.len);
    }
}

} // namespace car
} // namespace nbt
