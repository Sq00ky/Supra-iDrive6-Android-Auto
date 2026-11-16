// The only component allowed to own the physical PiP session. Control,
// ScreenOutput, the selected resource profile and row-0 metadata preserve the protocol and
// pixel layout verified by the standalone demo in the car.
#include "decoder.hpp"
#include "protocol.hpp"
#include "native/screen_output.hpp"
#include "native/control.hpp"
#ifndef NBT_CLUSTER_HOST_TEST
#include <signal.h>
#include "../video_settings.hpp"
#include "supervisor.hpp"
#endif

using namespace aa_cluster;

namespace {

struct Player {
    kombi::pip::screen::ScreenOutput output;
    Control control;
    Decoder decoder;
    bool active, marked, cancelled;
    unsigned number, epoch, last_present;
    uint8_t pixels[nbt::cluster::VideoProfile::ContentWidth *
                   nbt::cluster::VideoProfile::ContentHeight * 4];

    Player()
        : output{}, control{-1, -1, -1, 0, 1, 0, 0}, active(false), marked(false), cancelled(false),
          number(0), epoch(0), last_present(0) {}

    bool stop() {
        // Guidance releases its handle before Screen buffers are destroyed. Keep
        // the ownership marker on cleanup failure so the supervisor can recover.
        bool ok = control.close();
        ok = output.close() && ok;
        decoder.close();
        active = false;
#ifndef NBT_CLUSTER_HOST_TEST
        if (marked && ok) {
            unlink(ActiveMarker);
            marked = false;
        }
#endif
        output = {};
        control = {-1, -1, -1, 0, 1, 0, 0};
        return ok;
    }

    bool pump() {
        if (expired()) {
            return false;
        }
        // AA legitimately stops encoding an unchanged map/onboarding screen.
        // Keep its last frame until focus/session Stop, transport EOF, control
        // failure or the existing 65-second trial deadline. Frame cadence is
        // not a connection-health signal; the AA session owns that watchdog.
        return !active || control.pump();
    }

    bool read(int fd, uint8_t* destination, unsigned size) {
        unsigned done = 0;
        // Silence before a packet is allowed. Once a partial packet stalls,
        // enforce the original three-second assembly deadline.
        uint32_t deadline = millis() + 3000;
        while (done < size && !expired()) {
            if (!pump()) {
                printf("AA_CLUSTER_READ_STOP reason=control bytes=%u/%u\n", done, size);
                return false;
            }
            PollFD pollfd = {fd, PollRead, 0};
            int result = poll(&pollfd, 1, 20);
            if (result < 0 && *__get_errno_ptr() == 4) {
                continue;
            }
            if (result < 0 || pollfd.revents & PollErrorOrHangup) {
                printf("AA_CLUSTER_READ_STOP reason=poll bytes=%u/%u events=%d errno=%d\n", done,
                       size, pollfd.revents, *__get_errno_ptr());
                return false;
            }
            if (!result) {
                if (done && int32_t(millis() - deadline) > 0) {
                    printf("AA_CLUSTER_READ_STOP reason=partial_timeout bytes=%u/%u\n", done, size);
                    return false;
                }
                continue;
            }
            result = recv(fd, destination + done, size - done, 0);
            if (result < 0 && *__get_errno_ptr() == 4) {
                continue;
            }
            if (result <= 0) {
                printf("AA_CLUSTER_READ_STOP reason=transport bytes=%u/%u rc=%d errno=%d\n", done,
                       size, result, *__get_errno_ptr());
                return false;
            }
            done += unsigned(result);
        }
        if (done != size) {
            printf("AA_CLUSTER_READ_STOP reason=trial_end bytes=%u/%u\n", done, size);
        }
        return done == size;
    }

    bool start_output() {
#ifndef NBT_CLUSTER_HOST_TEST
        // The arrow renderer uses the same Guidance handle. Its normal
        // worker clears the arrow when the map preference becomes enabled.
        unsigned wait_started = millis();
        while (access("/dev/shmem/nbt-aad-kombi-nav.shown", F_OK) == 0) {
            if (expired() || millis() - wait_started > 4000) {
                return false;
            }
            usleep(20000);
        }
        int marker = open(ActiveMarker, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (marker < 0) {
            return false;
        }
        char owner[32];
        int owner_size = snprintf(owner, sizeof(owner), "%ld\n", (long)getpid());
        bool saved = write(marker, owner, owner_size) == owner_size;
        close(marker);
        marked = true;
        if (!saved) {
            return false;
        }
#endif
        unsigned began = millis();
        if (!output.inspect(nbt::cluster::VideoProfile::ScreenContextFlags) || !output.create()) {
            printf("AA_CLUSTER_OUTPUT_START_FAILED stage=screen elapsed_ms=%u errno=%d "
                   "interrupted=%d\n",
                   millis() - began, *__get_errno_ptr(), int(interrupted));
            return false;
        }
        printf("AA_CLUSTER_SCREEN_READY elapsed_ms=%u\n", millis() - began);
        fflush(0);
        if (!control.start()) {
            printf("AA_CLUSTER_OUTPUT_START_FAILED stage=guidance elapsed_ms=%u interrupted=%d\n",
                   millis() - began, int(interrupted));
            return false;
        }
        active = true;
        return true;
    }

    bool packet(const uint8_t* packet_header, const uint8_t* payload, bool backlog) {
        const unsigned kind = be32(packet_header + 4);
        const unsigned packet_epoch = be32(packet_header + 8);
        const unsigned payload_size = be32(packet_header + 12);
        if (kind == Stop) {
            epoch = packet_epoch;
            return stop();
        }
        if (kind == Reset) {
            if (!stop()) {
                return false;
            }
            epoch = packet_epoch;
            return decoder.open();
        }
        if (packet_epoch != epoch) {
            printf("AA_CLUSTER_EPOCH_ERROR received=%u expected=%u\n", packet_epoch, epoch);
            return false;
        }
        // Decode every reference frame while draining backlog, but limit
        // presentation work to one frame per 200 ms until caught up.
        bool present = !active || !backlog || millis() - last_present >= 200;
        int status =
            decoder.decode(payload, payload_size, be64(packet_header + 16), pixels, present);
        if (status < 0) {
            printf("AA_CLUSTER_DECODE_ERROR await_new_IDR\n");
            return false;
        }
        if (!status) {
            return true;
        }
        if (!active && !start_output()) {
            return false;
        }
        // No colour swap, exact factory map profile and backing geometry.
        if (!output.present(pixels, (0x5678u + number) & 65535, false)) {
            int error = *__get_errno_ptr();
            cancelled = interrupted && error == 4;
            printf("%s frames=%u errno=%d interrupted=%d\n",
                   cancelled ? "AA_CLUSTER_PRESENT_CANCELLED" : "AA_CLUSTER_PRESENT_FAILED", number,
                   error, int(interrupted));
            return false;
        }
        ++number;
        last_present = millis();
        if (number == 1 || number % 30 == 0) {
            printf("AA_CLUSTER_POST frames=%u pts=%llu source=%ux%u content=%ux%u\n", number,
                   (unsigned long long)be64(packet_header + 16),
                   unsigned(nbt::cluster::VideoProfile::SourceWidth),
                   unsigned(nbt::cluster::VideoProfile::SourceHeight),
                   unsigned(nbt::cluster::VideoProfile::ContentWidth),
                   unsigned(nbt::cluster::VideoProfile::ContentHeight));
            fflush(0);
        }
        return true;
    }
};
} // namespace

extern "C" int demo_main() {
    signal(1, on_signal);
    signal(2, on_signal);
    signal(15, on_signal);
    signal(13, (void (*)(int))1); // SIGPIPE cannot prevent normal cleanup.
    end_time = millis() + 65000;
    Player* player = new Player;
    uint8_t* payload = (uint8_t*)malloc(MaxPayload);
    int server = socket(2, 1, 0), client = -1, result = 1;
    Address local = {
        16, 2, uint16_t((aa_cluster::Port >> 8) | (aa_cluster::Port << 8)), 0x0100007f, {}};
    if (!payload || server < 0 || bind(server, &local, sizeof(local)) < 0 ||
        listen(server, 1) < 0) {
        goto done;
    }
    printf("AA_CLUSTER_READY loopback_port=%u deadline_seconds=%u\n", aa_cluster::Port,
           service_mode ? 0 : 65);
    fflush(0);
    while (!expired()) {
        PollFD pending_connection = {server, PollRead, 0};
        int poll_result = poll(&pending_connection, 1, 100);
        if (poll_result < 0 || pending_connection.revents & PollErrorOrHangup) {
            goto done;
        }
        if (poll_result > 0) {
            client = accept(server, 0, 0);
            break;
        }
    }
    if (client < 0) {
        goto done;
    }
    {
        uint8_t hello[8];
        aa_cluster::put32(hello, 0x41435031);
        aa_cluster::put32(hello + 4, 1);
        if (send(client, hello, 8, 0) != 8) {
            goto done;
        }
        uint8_t packet_header[HeaderSize];
        while (!expired() && player->read(client, packet_header, sizeof(packet_header))) {
            if (!valid_header(packet_header)) {
                printf("AA_CLUSTER_INVALID_HEADER\n");
                goto done;
            }
            unsigned payload_size = be32(packet_header + 12);
            if (payload_size && !player->read(client, payload, payload_size)) {
                if (expired()) {
                    result = 0;
                }
                goto done;
            }
            PollFD pending = {client, PollRead, 0};
            bool backlog = poll(&pending, 1, 0) > 0 && (pending.revents & PollRead);
            if (!player->packet(packet_header, payload, backlog)) {
                if (player->cancelled) {
                    result = 0;
                }
                goto done;
            }
        }
        result = 0;
    }
done:
    // All exits share PiP cleanup, including signal interruption and partial setup.
    if (client >= 0) {
        close(client);
    }
    if (server >= 0) {
        close(server);
    }
    if (!player->stop()) {
        result = 3;
    }
    printf("AA_CLUSTER_DONE status=%d frames=%u unchanged=%u coalesced=%u\n", result,
           player->number, player->decoder.unchanged(), player->decoder.coalesced());
    fflush(0);
    delete player;
    free(payload);
    return result;
}

#ifndef NBT_CLUSTER_HOST_TEST
int main(int argc, char** argv) {
    setvbuf(stdout, 0, _IOLBF, 0);
    setvbuf(stderr, 0, _IONBF, 0);
    if (argc == 3 && !strcmp(argv[1], "--supervise")) {
        return supervise_player(argv[0], argv[2]);
    }
    if (argc == 2 && !strcmp(argv[1], "--inspect")) {
        kombi::pip::screen::ScreenOutput output = {};
        bool ok = output.inspect();
        bool closed = output.close();
        if (ok && closed) {
            printf("SCREEN_INSPECT_OK secondary=%ux%u\n",
                   unsigned(nbt::cluster::VideoProfile::BackingWidth),
                   unsigned(nbt::cluster::VideoProfile::BackingHeight));
        }
        return ok && closed ? 0 : 1;
    }
    if (argc == 2 && !strcmp(argv[1], "--service")) {
        service_mode = true;
    } else if (argc != 1) {
        return 2;
    }
    return demo_main();
}
#endif
