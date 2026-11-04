// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "car/gps.hpp"

#include "most/client.hpp"

#include <stdio.h>

namespace nbt {
namespace car {

CarGps::CarGps() : haveStatus_(false), statusMs_(0), usableLogged_(-1), accuracyE3_(0), cb_(NULL), satCb_(NULL), user_(NULL) {
    memset(&status_, 0, sizeof(status_));
}

bool CarGps::attach(most::Client& client, uint32_t accuracyE3, LocationCb cb, SatellitesCb satCb, void* user) {
    accuracyE3_ = accuracyE3;
    cb_ = cb;
    satCb_ = satCb;
    user_ = user;
    if (!client.addRoute(kCarGpsFBlock, kCarGpsInst, kCarGpsFkt, most::kOpStatus, &CarGps::fixTramp, this) ||
        !client.addRoute(kCarStatusFBlock, kCarStatusInst, kCarStatusFkt, most::kOpStatus, &CarGps::statusTramp, this)) {
        printf("[GPS] no room on the MOST client: car GPS disabled\n");
        cb_ = NULL;     // one route may have fitted
        satCb_ = NULL;
        return false;
    }
    printf("[GPS] car GPS: MOST %02X/%02X fkt %03X -> Android Auto LOCATION, accuracy %u m\n", kCarGpsFBlock, kCarGpsInst,
           kCarGpsFkt, (unsigned)(accuracyE3 / 1000));
    printf("[GPS] car GPS: MOST %02X/%02X fkt %03X -> Android Auto GPS_SATELLITE\n", kCarStatusFBlock, kCarStatusInst,
           kCarStatusFkt);
    return true;
}

void CarGps::fixTramp(const most::View& m, void* self) { static_cast<CarGps*>(self)->onFix(m); }
void CarGps::statusTramp(const most::View& m, void* self) { static_cast<CarGps*>(self)->onStatus(m); }

void CarGps::onStatus(const most::View& m) {
    CarGpsStatus s;
    memset(&s, 0, sizeof(s));
    CarGpsStatusFilter::Result r = statusFilter_.feed(m.data, m.len, m.rxMs, s);
    if (r.repeat) return;
    if (r.logBad)
        printf("[GPS] status: %u-byte payload on %02X/%02X fkt %03X, expected 10\n", (unsigned)m.len, m.fblock, m.inst, m.fkt);
    if (!r.ok) return;
    // Speed and the error bytes are raw values with units still to confirm (the layout above). The longest gap shows how
    // often this message really changes.
    if (r.logFields)
        printf("[GPS] status #%u used=%u receiver=%u quality=%u hdop=%u err=%u/%u speed=%u time=%u, longest gap %u ms%s\n",
               r.statusNo, s.satsUsed, s.receiver, s.quality, s.hdop, s.errLat, s.errLon, s.speed, s.timeOfDay,
               (unsigned)r.gapMs, r.receiverChanged ? " (receiver changed)" : "");
    haveStatus_ = true;
    status_ = s;
    statusMs_ = m.rxMs;
    if (satCb_) {
        SatelliteStatus st;
        st.used = satellitesUsed(s);
        st.rx_ms = m.rxMs;
        satCb_(st, user_);
    }
}

void CarGps::onFix(const most::View& m) {
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
    const bool usable = positionUsable(haveStatus_, status_, statusMs_, m.rxMs);
    if ((int)usable != usableLogged_) {
        if (usable)
            printf("[GPS] position goes to the phone: 3D fix, %u satellites\n", status_.satsUsed);
        else if (!haveStatus_)
            printf("[GPS] position held back: no status from %02X/%02X fkt %03X yet\n", kCarStatusFBlock, kCarStatusInst,
                   kCarStatusFkt);
        else
            printf("[GPS] position held back: receiver=%u, status %u ms old\n", status_.receiver, (unsigned)(m.rxMs - statusMs_));
        usableLogged_ = usable ? 1 : 0;
    }
    if (usable && cb_) cb_(toLocationFix(f, accuracyE3_, m.rxMs), user_);
}

} // namespace car
} // namespace nbt
