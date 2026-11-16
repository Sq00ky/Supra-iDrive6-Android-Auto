// Native cluster session: DisplaySink discovery/handshake and Guidance RPCs.
#pragma once
#include "pip.hpp"
#include "qnx_abi.hpp"
#include "presentation.hpp"
#include "factory_profile.hpp"
#include "cluster/guidance/graph_template.hpp"

using namespace kombi::pip::detail;
using nbt::cluster::VideoProfile;

// These process-wide flags belong to the single active native session.
static volatile int interrupted;
static uint32_t end_time;
static bool service_mode;

static void on_signal(int) {
    interrupted = 1;
}

// The owning AA session bounds service lifetime. Individual network operations
// still have deadlines; only standalone trials have a whole-process deadline.
static bool expired() {
    return interrupted || (!service_mode && int32_t(millis() - end_time) >= 0);
}

// QNX poll flags and the sockaddr_in byte layout verified on the car.
enum { PollRead = 1, PollWrite = 2, PollErrorOrHangup = 0x60 };
struct Address {
    uint8_t length, family;
    uint16_t port;
    uint32_t ip;
    uint8_t zero[8];
};

// Endpoint words use the selected target's sockaddr representation.
static const uint32_t ProducerIP = VideoProfile::ProducerIP;
static const uint32_t ClusterIP = VideoProfile::ClusterIP;
enum {
    DisplaySinkPort = VideoProfile::DisplaySinkPort,
    GuidancePort = VideoProfile::GuidancePort,
    SubscriptionRenewalMilliseconds = 10000,
    SubscriptionTtlSeconds = 30,
    CleanupTimeoutMilliseconds = 6000
};
static unsigned discovery_session = 1;
static bool discovery_reboot = true;

// Aggregate initialized by the owner: sockets start at -1, RPC session at 1,
// and remaining state at zero. start() may leave partial resources for close().
struct Control {
    int control, udp, guidance; // DisplaySink TCP, discovery UDP, Guidance TCP.
    unsigned port, rpc_session; // Advertised local TCP port; next Guidance session.
    uint32_t handle, renewed;   // Remote controller handle; last subscription time.

    // At most one Guidance RPC is outstanding. Preserve partial reads across
    // timeout/stop so close() can drain its reply before the cancellation graph.
    unsigned pending_method, pending_session, reply_used, reply_size;
    bool pending_reply, broken_stream;
    uint32_t cleanup_deadline;
    uint8_t reply_bytes[4096];

    bool stopped() const {
        return expired();
    }

    void refresh() {
        uint32_t now = millis();
        if (udp >= 0 && uint32_t(now - renewed) >= SubscriptionRenewalMilliseconds) {
            subscribe(udp, port, SubscriptionTtlSeconds);
            renewed = now;
        }
    }

    // Reuse the tested SDK framing, graph and cancellation RPC machinery.
    // The car-specific sink handshake is implemented below.
#define sink_ready reference_sink_ready
#include "native_methods.inc"
#undef sink_ready

    bool sink(bool initial) {
        uint8_t packet[64] = {};
        int message_size = message(control, packet, sizeof(packet), false, initial);
        if (!initial && message_size == SomeIpHeaderBytes && get32(packet) == 0xffff8000) {
            return true;
        }

        // DisplaySink.newDisplaySink notification: version 1/1, type 2,
        // return code zero, then scale(double), dimensions(u16/u16), selection.
        if (message_size != 29 || get32(packet) != 0xb0098001 || packet[12] != 1 ||
            packet[13] != 1 || packet[14] != 2 || packet[15]) {
            return false;
        }
        printf("SINK width=%u height=%u selection=%u\n", get16(packet + 24), get16(packet + 26),
               packet[28]);
        fflush(0);
        if (get16(packet + 24) != VideoProfile::ContentWidth ||
            get16(packet + 26) != VideoProfile::ContentHeight ||
            packet[28] != VideoProfile::SinkSelection) {
            printf("REFUSE_SINK requires_cluster_%ux%u_selection_%u\n",
                   unsigned(VideoProfile::ContentWidth), unsigned(VideoProfile::ContentHeight),
                   unsigned(VideoProfile::SinkSelection));
            return false;
        }
        uint8_t completion[18] = {};
        put32(completion, 0xb0090001);
        put32(completion + 4, 10);
        put16(completion + 8, 1);
        completion[12] = completion[13] = completion[14] = 1;
        completion[16] = 1;
        // Same no-response method and body as all captured cluster handshakes.
        bool ok = transfer(control, completion, sizeof(completion), true);
        printf("SINK_COMPLETION sent=%u\n", unsigned(ok));
        return ok;
    }

    bool start() {
        // Bind the TCP producer first, then advertise that connection's local
        // port through discovery. A valid sink handshake precedes Guidance.
        control = open_tcp(ClusterIP, DisplaySinkPort);
        if (control < 0) {
            printf("DISPLAY_SINK_CONNECT_FAILED errno=%d\n", *__get_errno_ptr());
            return false;
        }
        Address local = {};
        unsigned address_size = sizeof(local);
        if (getsockname(control, &local, &address_size) < 0) {
            return false;
        }
        port = (local.port >> 8) | ((local.port & 255) << 8);
        udp = socket(2, 2, 0);
        if (udp < 0) {
            return false;
        }
        Address sender = address(ProducerIP, 0);
        if (bind(udp, &sender, sizeof(sender)) < 0) {
            return false;
        }
        subscribe(udp, port, SubscriptionTtlSeconds);
        renewed = millis();
        if (!sink(true)) {
            return false;
        }
        guidance = open_tcp(ClusterIP, GuidancePort);
        if (guidance < 0) {
            printf("GUIDANCE_CONNECT_FAILED errno=%d\n", *__get_errno_ptr());
        }
        return guidance >= 0 &&
               guidance_start(guidance, handle, rpc_session, nbt::cluster::native_pip::resource());
    }

    bool pump() {
        if (stopped()) {
            return false;
        }
        refresh();
        if (!guidance_heartbeat(guidance)) {
            return false;
        }
        PollFD incoming = {control, PollRead, 0};
        int poll_result = poll(&incoming, 1, 0);
        return poll_result >= 0 && (poll_result == 0 || sink(false));
    }

    bool close() {
        bool ok = true;
        // A stopped/expired read may have consumed only part of a reply. Finish
        // that exact transaction before sending cancellation on the same TCP
        // stream. Never mistake its late acknowledgement for the Stop reply.
        // Six cancellation calls share this total deadline; each call also
        // has its own one-second budget. Late replies get one second to drain.
        cleanup_deadline = millis() + CleanupTimeoutMilliseconds;
        if (guidance >= 0 && pending_reply) {
            ok = finish_guidance(guidance, millis() + CleanupOperationTimeoutMilliseconds, true,
                                 pending_method == 1 ? &handle : 0);
        }
        ok = ok && !broken_stream;
        if (ok && guidance >= 0 && handle) {
            uint8_t body[512];
            for (unsigned i = 0; i < nbt::kombinav::tmpl::cancel_count; ++i) {
                const nbt::kombinav::tmpl::Frame& frame = nbt::kombinav::tmpl::cancel[i];
                if (frame.len > sizeof(body) || frame.len < 4) {
                    ok = false;
                    break;
                }
                for (unsigned j = 0; j < frame.len; ++j) {
                    body[j] = frame.body[j];
                }
                put32(body, handle);
                if (!guidance_call(guidance, frame.method, body, frame.len, rpc_session++, 0,
                                   true)) {
                    ok = false;
                    break;
                }
            }
        }

        // Release every local resource even when the peer cannot acknowledge.
        if (udp >= 0 && port) {
            subscribe(udp, port, 0);
        }
        if (udp >= 0) {
            ::close(udp);
        }
        if (control >= 0) {
            ::close(control);
        }
        if (guidance >= 0) {
            ::close(guidance);
        }
        udp = control = guidance = -1;
        printf("GUIDANCE_CLEANUP acknowledged=%u handle=%u\n", unsigned(ok), handle);
        fflush(0);
        return ok;
    }
};
