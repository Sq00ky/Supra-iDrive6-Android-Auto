#include "bridge.hpp"
#include "cluster/profiles/video.hpp"
#include "router.hpp"
#include "manifest.hpp"
#include "atomic.hpp"
#include "credits.hpp"
#include "encoded_queue.hpp"
#include <stdlib.h>
#include <string.h>
#include "native/qnx_abi.hpp"
#include <pthread.h>
#include <sys/wait.h>
#include <signal.h>
#include "service_spawn.hpp"
#include "service_log.hpp"
#include "../video_settings.hpp"

using namespace aa_cluster;
using kombi::pip::detail::millis;

namespace {

static EncodedQueue queue;
// The low state bit enables decoding; the upper bits identify the epoch.
// Router writes state on the session thread; only the worker writes connected.
static unsigned state, connected, resync_flag;
static unsigned io_timeout_ms = 3000;
static unsigned session_live, wanted;
static uint32_t setting_checked;
static Credits credits;
static ClusterAck acknowledge;
static Router router;
static uint8_t manifest[16384];

static unsigned load(const unsigned* value) {
    return atomic_load(value);
}

static unsigned exchange(unsigned* value, unsigned next) {
    return atomic_exchange(value, next);
}

static void store(unsigned* value, unsigned next) {
    atomic_store(value, next);
}

static bool ready() {
    return load(&connected) != 0;
}

static bool resync() {
    return exchange(&resync_flag, 0) != 0;
}

static unsigned change_state(bool active) {
    // Increment the generation even when the active bit does not change.
    unsigned next = ((load(&state) + 2) & ~1u) | (active ? 1 : 0);
    store(&state, next);
    return next;
}

static bool enqueue(unsigned epoch, const uint8_t* config, unsigned config_size,
                    const uint8_t* frame, unsigned frame_size, uint64_t pts) {
    return queue.push(epoch, config, config_size, frame, frame_size, pts);
}

// Only the socket worker owns the helper PID. The helper owns its temporary
// alias/lock and watches this daemon independently, including after SIGKILL.
struct Service {
    pid_t pid;
    ServiceLog logs;
    // A failed launch waits for the setting to cycle; do not respawn in a loop.
    bool attempted, external;
    Service() : pid(-1), attempted(false), external(false) {
        const char* external_setting =
            video_environment("NBT_CLUSTER_EXTERNAL_PLAYER", "NBT_G30_CLUSTER_EXTERNAL_PLAYER");
        external = external_setting && !strcmp(external_setting, "1");
    }

    void stop() {
        if (pid <= 0) {
            logs.finish();
            return;
        }
        if (waitpid(pid, 0, WNOHANG) == 0) {
            kill(pid, SIGTERM);
            // The helper escalates only its own player, then removes the alias.
            for (unsigned i = 0; i < 120; ++i) {
                if (waitpid(pid, 0, WNOHANG) == pid) {
                    pid = -1;
                    logs.finish();
                    return;
                }
                usleep(100000);
            }
            printf("[AA cluster] service cleanup still pending pid=%ld\n", (long)pid);
            return; // Never kill the cleanup owner or launch a competing helper.
        }
        pid = -1;
        logs.finish();
    }

    bool tick() {
        if (external) {
            return true;
        }
        int status = 0;
        if (pid > 0 && waitpid(pid, &status, WNOHANG) == pid) {
            logs.finish();
            printf("[AA cluster] service ended pid=%ld exit=%d signal=%d; toggle map off/on to "
                   "retry\n",
                   (long)pid, WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                   WIFSIGNALED(status) ? WTERMSIG(status) : 0);
            pid = -1;
        }
        if (!load(&session_live)) {
            stop();
            attempted = false;
            return false;
        }
        if (!load(&wanted)) {
            if (pid <= 0) {
                attempted = false;
            }
            return pid > 0;
        }
        if (pid > 0) {
            return true;
        }
        if (attempted) {
            return false;
        }
        attempted = true;
        // Retain an explicit legacy supervisor override for existing benches.
        // Normal installations launch the player's compiled supervisor directly.
        const char* path = video_environment("NBT_CLUSTER_SERVICE", "NBT_G30_CLUSTER_SERVICE");
        bool legacy = path && *path;
        if (!legacy) {
            path = video_environment("NBT_CLUSTER_PLAYER", "NBT_G30_CLUSTER_PLAYER");
            if (!path || !*path) {
                path = "/opt/aa/kombi-player";
            }
        }
        if (access(path, legacy ? R_OK : X_OK)) {
            printf("[AA cluster] player/supervisor missing: %s\n", path);
            return false;
        }
        int log_fd = -1;
        pid = spawn_service(path, legacy, &log_fd);
        if (pid < 0) {
            int error = errno;
            printf("[AA cluster] service spawn failed errno=%d: %s\n", error, strerror(error));
            return false;
        }
        if (!logs.start(log_fd)) {
            printf("[AA cluster] kombi-player log relay failed; stopping helper\n");
            stop();
            return false;
        }
        printf("[AA cluster] service start pid=%ld method=qnx_spawn\n", (long)pid);
        return pid > 0;
    }
};

// The worker alone touches the loopback socket. At most 4 MiB are queued and
// every I/O operation has a deadline; the AA receive/ACK thread cannot block.
static bool transfer(int fd, const void* data, unsigned size, bool sending) {
    // Screen/Guidance startup and a large IDR can backpressure this worker.
    // Keep the receive/ACK thread independent, but allow the same bounded
    // three-second assembly interval as the player rather than dropping a
    // partially sent access unit after only 500 ms.
    unsigned done = 0;
    uint32_t began = millis(), deadline = began + io_timeout_ms;
    while (done < size && int32_t(deadline - millis()) > 0) {
        PollFD pending = {fd, short(sending ? 2 : 1), 0};
        int result = poll(&pending, 1, 25);
        if (result < 0 && *__get_errno_ptr() == 4) {
            continue; // EINTR
        }
        if (result < 0 || pending.revents & 0x60) {
            break;
        }
        if (!result) {
            continue;
        }
        result = sending ? send(fd, (uint8_t*)data + done, size - done, 0)
                         : recv(fd, (uint8_t*)data + done, size - done, 0);
        if (result < 0 && (*__get_errno_ptr() == 4 || *__get_errno_ptr() == 11)) {
            continue; // EINTR/EAGAIN on QNX
        }
        if (result <= 0) {
            break;
        }
        done += unsigned(result);
    }
    unsigned elapsed = millis() - began;
    if (done != size || elapsed >= 500) {
        printf(
            "[AA cluster] local_io direction=%s bytes=%u/%u elapsed_ms=%u complete=%u errno=%d\n",
            sending ? "send" : "receive", done, size, elapsed, unsigned(done == size),
            *__get_errno_ptr());
    }
    return done == size;
}

// Return a connected, nonblocking socket only after the player handshake.
static int connect_decoder() {
    int fd = socket(2, 1, 0);
    if (fd < 0) {
        return -1;
    }
    struct {
        uint8_t length, family;
        uint16_t port;
        uint32_t address;
        uint8_t pad[8];
    } loopback = {16, 2, uint16_t((Port >> 8) | (Port << 8)), 0x0100007f, {}};
    // Loopback connect is immediate; no Ethernet destination is used.
    if (connect(fd, &loopback, sizeof(loopback)) < 0) {
        close(fd);
        return -1;
    }
    if (fcntl(fd, 4, fcntl(fd, 3, 0) | 0x80) < 0) {
        close(fd);
        return -1;
    }
    uint8_t hello[8];
    if (!transfer(fd, hello, 8, false) || be32(hello) != 0x41435031 || be32(hello + 4) != 1) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool send_frame(int fd, const EncodedQueue::Item& item, unsigned current) {
    uint8_t packet_header[HeaderSize];
    header(packet_header, Video, current, item.len, item.pts);
    unsigned first = QueueBytes - item.offset;
    if (first > item.len) {
        first = item.len;
    }
    bool ok = transfer(fd, packet_header, sizeof(packet_header), true) &&
              transfer(fd, queue.data(item.offset), first, true) &&
              (first == item.len || transfer(fd, queue.data(0), item.len - first, true));
    // Return a phone credit only after this complete access unit
    // reached the decoder socket. The session thread alone sends
    // AA/TLS messages; this worker publishes epoch-tagged credits.
    if (ok && acknowledge) {
        ok = credits.publish(item.epoch);
    }
    return ok;
}

static void* worker(void*) {
    int fd = -1;
    unsigned sent_state = ~0u;
    uint32_t retry = 0;
    Service service;
    for (;;) {
        if (!service.tick()) {
            if (fd >= 0) {
                store(&connected, 0);
                close(fd);
                fd = -1;
            }
            queue.discard();
            usleep(20000);
            continue;
        }
        if (fd < 0) {
            // Old compressed packets must not survive decoder disconnection.
            queue.discard();
            if (int32_t(millis() - retry) < 0) {
                usleep(20000);
                continue;
            }
            retry = millis() + 1000;
            fd = connect_decoder();
            if (fd < 0) {
                continue;
            }
            sent_state = ~0u;
            store(&connected, 1);
            printf("[AA cluster] decoder connected on loopback:%u\n", Port);
        }
        bool ok = true;
        unsigned current = load(&state);
        uint8_t packet_header[HeaderSize];
        if (current != sent_state) {
            header(packet_header, current & 1 ? Reset : Stop, current, 0, 0);
            ok = transfer(fd, packet_header, sizeof(packet_header), true);
            sent_state = current;
        }
        const EncodedQueue::Item* item = queue.front();
        if (ok && item) {
            const EncodedQueue::Item& frame = *item;
            if (frame.epoch == current && (current & 1)) {
                ok = send_frame(fd, frame, current);
            }
            // Stale epochs are discarded without returning a phone credit.
            queue.pop();
        } else if (ok) {
            // Detect EOF while idle, including after the bounded player exits.
            PollFD pending = {fd, 1, 0};
            int result = poll(&pending, 1, 10);
            if (result < 0 || pending.revents & 0x60) {
                ok = false;
            }
            if (result > 0 && pending.revents & 1) {
                uint8_t byte;
                ok = recv(fd, &byte, 1, 0) > 0;
            }
        }
        if (!ok) {
            store(&connected, 0);
            store(&resync_flag, 1);
            close(fd);
            fd = -1;
            printf("[AA cluster] decoder disconnected; main video unaffected\n");
        }
    }
    return 0;
}
} // namespace

extern "C" void idriver_cluster_configure_acks(ClusterAck callback) {
    if (!acknowledge) {
        acknowledge = callback;
    }
}

extern "C" unsigned idriver_cluster_ack_window() {
    return router.enabled && acknowledge ? 4 : 0;
}

extern "C" void idriver_cluster_tick(void* session) {
    if (!router.enabled) {
        return;
    }
    uint32_t now = millis();
    if (!setting_checked || uint32_t(now - setting_checked) >= 250) {
        setting_checked = now;
        bool on = setting_enabled();
        if (router.allowed != on) {
            printf("[AA cluster] setting %s (live)\n", on ? "on" : "off");
        }
        store(&wanted, on);
        router.allow(session, on);
    }
    router.tick(session);
}

extern "C" void idriver_cluster_drain_acks(void* session) {
    if (!router.enabled || !acknowledge) {
        return;
    }
    unsigned count = credits.take(load(&state));
    while (count--) {
        acknowledge(session, 2);
    }
}

extern "C" void idriver_cluster_init(ClusterSetup setup, ClusterFocus focus) {
    store(&session_live, 1);
    store(&wanted, setting_enabled());
    setting_checked = 0;
    if (router.enabled) {
        return;
    }
    const char* timeout =
        video_environment("NBT_CLUSTER_LOCAL_IO_TIMEOUT_MS", "NBT_G30_LOCAL_IO_TIMEOUT_MS");
    if (timeout) {
        int ms = atoi(timeout);
        if (ms >= 500 && ms <= 15000) {
            io_timeout_ms = unsigned(ms);
        }
    }
    router.hooks = {setup, focus, ready, resync, change_state, enqueue, millis};
    router.allowed = setting_enabled();
    pthread_t thread = 0;
    if (pthread_create(&thread, 0, worker, 0)) {
        printf("[AA cluster] worker creation failed\n");
        return;
    }
    pthread_detach(thread);
    router.enabled = true;
    printf("[AA cluster] independent display_id=2 channel=2 enabled; decoder must be ready before "
           "focus\n");
    printf("[AA cluster] local_io_timeout_ms=%u deferred_ack_window=%u\n", io_timeout_ms,
           idriver_cluster_ack_window());
}

extern "C" int idriver_cluster_receive(void* session, unsigned channel, const uint8_t* bytes,
                                       size_t size) {
    if (router.receive(session, channel, bytes, size)) {
        return 1;
    }
    return acknowledge && router.deferred ? 2 : 0;
}

extern "C" void idriver_cluster_end() {
    router.end();
    store(&session_live, 0);
}

extern "C" const uint8_t* idriver_cluster_manifest(const uint8_t* bytes, size_t size) {
    if (router.enabled && rewrite_cluster_video_layout(bytes, size, manifest, sizeof(manifest))) {
        printf("[AA cluster] SDR cluster=%ux%u H264, no DHU square-display margins\n",
               unsigned(nbt::cluster::VideoProfile::SourceWidth),
               unsigned(nbt::cluster::VideoProfile::SourceHeight));
        return manifest;
    }
    return bytes;
}
