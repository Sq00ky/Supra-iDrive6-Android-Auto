// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "car_config.hpp"
#include "sdr_builder.hpp"
#include "dhu_sdp.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace nbt;

static unsigned varint(const std::vector<uint8_t>& p, size_t& i) {
    unsigned v = 0, shift = 0;
    for (;;) {
        assert(i < p.size() && shift < 32);
        unsigned b = p[i++];
        v |= (b & 127) << shift;
        if (!(b & 128)) return v;
        shift += 7;
    }
}

static std::vector<uint8_t> message(const std::vector<uint8_t>& p, unsigned field, int channel = -1) {
    size_t i = 0;
    while (i < p.size()) {
        unsigned key = varint(p, i);
        if ((key & 7) == 0) { varint(p, i); continue; }
        assert((key & 7) == 2);
        unsigned len = varint(p, i);
        assert(len <= p.size() - i);
        std::vector<uint8_t> body(p.begin() + i, p.begin() + i + len);
        i += len;
        if ((key >> 3) == field) {
            size_t j = 0;
            if (channel < 0 || (varint(body, j) == 8 && varint(body, j) == (unsigned)channel)) return body;
        }
    }
    assert(false);
    return std::vector<uint8_t>();
}

static unsigned scalar(const std::vector<uint8_t>& p, unsigned field) {
    size_t i = 0;
    while (i < p.size()) {
        unsigned key = varint(p, i);
        if ((key & 7) == 0) {
            unsigned v = varint(p, i);
            if ((key >> 3) == field) return v;
        } else {
            assert((key & 7) == 2);
            unsigned len = varint(p, i);
            assert(len <= p.size() - i);
            i += len;
        }
    }
    assert(false);
    return 0;
}

struct Case {
    const char* screen;
    const char* res;
    int dstW, dstH, shownW, shownH, dpi;
    double touch;
    bool native, mini;
};

static void check(const Case& c, const char* fps) {
    const char* clear[] = { "NBT_VID_RES", "NBT_VID_NATIVE", "NBT_VID_MW", "NBT_VID_MH", "NBT_VID_INSETS",
                            "NBT_VID_DPI", "NBT_TOUCH_CFG_H" };
    for (size_t i = 0; i < sizeof(clear) / sizeof(clear[0]); ++i) unsetenv(clear[i]);
    if (c.res) setenv("NBT_VID_RES", c.res, 1);
    setenv("NBT_SCREEN", c.screen, 1);
    setenv("NBT_VEHICLE", c.mini ? "mini" : "bmw", 1);
    setenv("NBT_VID_FPS", fps, 1);
    if (c.native) setenv("NBT_VID_NATIVE", "1", 1);
    char path[] = "/tmp/nbt-video-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE* f = fdopen(fd, "w");
    assert(f);
    fputs(c.mini ? "[Screen]\nWidth=1180\nHeight=480\n" : "[Screen]\nWidth=1280\nHeight=480\n", f);
    fclose(f);
    setenv("NBT_CARPLAY_INI", path, 1);
    carConfig();
    unlink(path);

    assert(mmrDstWidth() == c.dstW && mmrDstHeight() == c.dstH);
    assert(canvasColsShown() == c.shownW && canvasRowsShown() == c.shownH);
    assert(fabs(touchScale() - c.touch) < 0.000001);
    bool low = c.res && !strcmp(c.res, "1");
    bool high = c.res && !strcmp(c.res, "3");
    int w = low ? 800 : high ? 1920 : 1280, h = low ? 480 : high ? 1080 : 720;
    assert(abs(c.dstW * h - c.dstH * w) < w + h);

    std::vector<uint8_t> sdr(kDhuSdpProtobuf, kDhuSdpProtobuf + kDhuSdpProtobufLen);
    std::vector<uint8_t> other = message(sdr, 1, 2);
    assert(patchMainVideo(sdr));
    assert(patchTouchConfig(sdr));
    assert(message(sdr, 1, 2) == other);
    std::vector<uint8_t> video = message(message(message(sdr, 1, 6), 3), 4);
    assert(scalar(video, 1) == (low ? 1u : high ? 3u : 2u));
    assert(scalar(video, 2) == (!strcmp(fps, "30") ? 1u : 2u));
    assert(scalar(video, 5) == (unsigned)c.dpi);
    assert(scalar(video, 9) == (unsigned)c.dpi);
    std::vector<uint8_t> insets = message(message(video, 11), 1);
    assert(scalar(insets, 1) == 0);
    assert(scalar(insets, 2) == (unsigned)(h - c.shownH));
    int side = c.mini ? (low ? 25 : high ? 60 : 40) : 0;
    assert(scalar(insets, 3) == (unsigned)side);
    assert(scalar(insets, 4) == (unsigned)(w - c.shownW + side));
    if (low || high) assert(scalar(video, 3) == 0 && scalar(video, 4) == 0);
    std::vector<uint8_t> touch = message(message(message(sdr, 1, 7), 4), 2);
    assert(scalar(touch, 1) == (unsigned)w && scalar(touch, 2) == (unsigned)h);
    assert(fabs(defaultTouchOffsetX(carConfig()) - (c.mini ? (low ? -12.5 : high ? -30.0 : -20.0) : 0.0)) < 0.000001);
}

int main() {
    const Case cases[] = {
        { "1280x480", "1", 1280, 768, 800, 300, 88, 0.625, false, false },
        { "1440x540", "1", 1440, 864, 800, 300, 88, 800.0/1440, false, false },
        { "800x480", "1", 800, 480, 800, 480, 88, 1.0, false, false },
        { "1920x720", "1", 1920, 1152, 800, 300, 88, 800.0/1920, false, false },
        { "1180x480", "1", 1180, 708, 800, 325, 88, 800.0/1180, false, false },
        { "1024x600", "1", 1024, 614, 800, 468, 88, 800.0/1024, false, false },
        { "600x800", "1", 1333, 800, 360, 480, 88, 0.6, false, false },
        { "640x400", "1", 800, 480, 640, 400, 88, 1.0, true, false },
        { "1280x480", "1", 1280, 768, 800, 300, 88, 0.625, true, false },
        { "1280x480", "1", 1280, 768, 800, 300, 95, 0.625, false, true },
        { "1280x480", NULL, 1280, 720, 1280, 480, 140, 1.0, false, false },
        { "1440x540", "2", 1440, 810, 1280, 480, 140, 1280.0/1440, false, false },
        { "1280x480", "bad", 1280, 720, 1280, 480, 140, 1.0, false, false },
        { "1280x480", "1junk", 1280, 720, 1280, 480, 140, 1.0, false, false },
        { "1280x480", "3", 1280, 720, 1920, 720, 210, 1.5, false, false },
        { "800x480", "2", 853, 480, 1200, 720, 140, 1.5, false, false },
        { "1280x720", "2", 1280, 720, 1280, 720, 140, 1.0, false, false },
        { "1920x720", "2", 1920, 1080, 1280, 480, 140, 1280.0/1920, false, false },
        { "600x800", "2", 1422, 800, 540, 720, 140, 0.9, false, false },
        { "1280x480", "2", 1280, 720, 1280, 480, 152, 1.0, false, true },
        { "800x480", "3", 853, 480, 1800, 1080, 210, 2.25, false, false },
        { "1440x540", "3", 1440, 810, 1920, 720, 210, 1920.0/1440, false, false },
        { "1920x720", "3", 1920, 1080, 1920, 720, 210, 1.0, false, false },
        { "1920x1080", "3", 1920, 1080, 1920, 1080, 210, 1.0, false, false },
        { "600x800", "3", 1422, 800, 810, 1080, 210, 1.35, false, false },
        { "1280x480", "3", 1920, 1080, 1280, 480, 210, 1.0, true, false },
        { "1280x480", "3", 1280, 720, 1920, 720, 228, 1.5, false, true },
        { "1280x480", "4", 1280, 720, 1280, 480, 140, 1.0, false, false },
        { "1280x480", "5", 1280, 720, 1280, 480, 140, 1.0, false, false }
    };
    const char* rates[] = { "30", "60" };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        for (size_t rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
            pid_t pid = fork();
            assert(pid >= 0);
            if (!pid) { check(cases[i], rates[rate]); return 0; }
            int status;
            assert(waitpid(pid, &status, 0) == pid);
            if (!WIFEXITED(status) || WEXITSTATUS(status)) {
                printf("FAIL: screen=%s res=%s fps=%s native=%d mini=%d\n", cases[i].screen,
                       cases[i].res ? cases[i].res : "unset", rates[rate], cases[i].native, cases[i].mini);
                return 1;
            }
        }
    }
    puts("test_video_geometry OK");
}
