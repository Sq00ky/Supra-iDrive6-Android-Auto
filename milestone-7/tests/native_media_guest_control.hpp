// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_NATIVE_MEDIA_GUEST_CONTROL_HPP
#define NBT_NATIVE_MEDIA_GUEST_CONTROL_HPP

// Instrumented offline RHMI build only. Never included in the production SO.
namespace nbt { namespace native_media {
namespace guest {
struct Control {
    pthread_t reader;
    void* appTimer;
    unsigned sequence, window, audioMode;
    char action[32];
    volatile int pending;
    bool started;
    Control() : appTimer(0), sequence(0), window(0), audioMode(0), pending(0), started(false) {
        action[0] = 0;
    }
};
static Control control;

inline void result(int ok) {
    using namespace detail;
    char text[384];
    void* owner = pointer(0x038302c0 + 0xd4);
    const unsigned vptr = owner ? word((uintptr_t)owner + 0x33c) : 0;
    const int size = snprintf(text, sizeof(text),
        "sequence=%u\naction=%s\nresult=%d\nrunning=%u\ninstalled=%d\npending=%d\n"
        "stopping=%d\nstopped=%d\ncontent_vptr=%08x\nwindow=%08x\naudio_mode=%u\nend=%u\n",
        control.sequence, control.action, ok, state.running ? 1u : 0u,
        __sync_fetch_and_add(&state.installed, 0), __sync_fetch_and_add(&state.pending, 0),
        __sync_fetch_and_add(&state.stopping, 0), __sync_fetch_and_add(&state.stopped, 0), vptr, control.window, control.audioMode, control.sequence);
    int fd = open("/dev/shmem/nbt-aad-native-test-result", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) { write(fd, text, size); close(fd); }
    __sync_lock_test_and_set(&control.pending, 0);
}

static void onPartial(void*, unsigned, unsigned, unsigned short) {
    using namespace detail;
    void* owner = context();
    int ok = 0;
    if (state.getUnit() == state.unit && owner && state.saved) {
        // Simulate one native label updating while our artist is still in row1.
        ((SetText)0x0235a558)((char*)owner + 0x33c, 0, &state.originalText[0]);
        ok = 1;
    }
    result(ok);
}

static void onObserve(void*, unsigned, unsigned, unsigned short) {
    using namespace detail;
    void* app = state.unit ? pointer((uintptr_t)state.unit + 0x3e8) : 0;
    if (state.getUnit() != state.unit || !app || word((uintptr_t)app) != 0x02c8e9d0) {
        result(0); return;
    }
    control.window = ((CurrentWindow)0x00f83334)(app);
    control.audioMode = word((uintptr_t)app + 0x6d8);
    result(1);
}

static void onCommand(void*, unsigned, unsigned, unsigned short) {
    using namespace detail;
    int ok = 1;
    if (!strcmp(control.action, "stop")) stop();
    else if (!strcmp(control.action, "start")) ok = start(state.log) ? 1 : 0;
    else if (!strcmp(control.action, "queue-stop")) { wake(); stop(); }
    else if (!strcmp(control.action, "partial")) {
        if (state.timer && state.setTimer(state.timer, &control, (void*)&onPartial, 0, 10, 0, 0) > 0) return;
        ok = 0;
    } else if (!strcmp(control.action, "status")) {
        if (state.timer && state.setTimer(state.timer, &control, (void*)&onObserve, 0, 10, 0, 0) > 0) return;
        ok = 0;
    } else ok = 0;
    result(ok);
}

static void* reader(void*) {
    unsigned last = 0;
    for (;;) {
        if (!__sync_fetch_and_add(&control.pending, 0)) {
            int fd = open("/dev/shmem/nbt-aad-native-test-command", O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                char line[96]; const ssize_t size = read(fd, line, sizeof(line) - 1); close(fd);
                if (size > 0) {
                    line[size] = 0;
                    unsigned sequence = 0; char action[32];
                    if (sscanf(line, "%u %31s", &sequence, action) == 2 && sequence && sequence != last) {
                        last = sequence;
                        control.sequence = sequence; strcpy(control.action, action);
                        __sync_lock_test_and_set(&control.pending, 1);
                        if (detail::state.setTimer(control.appTimer, &control, (void*)&onCommand, 0, 10, 0, 0) <= 0)
                            result(0);
                    }
                }
            }
        }
        usleep(20000);
    }
    return 0;
}
} // namespace guest

inline void guestStart(void* appUnit) {
    if (guest::control.started) return;
    guest::control.appTimer = ((detail::GetTimer)(*(void***)appUnit)[3])(appUnit);
    if (!guest::control.appTimer) return;
    guest::control.started = pthread_create(&guest::control.reader, 0, &guest::reader, 0) == 0;
}
} }
#endif
