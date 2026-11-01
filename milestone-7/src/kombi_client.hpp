// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// kombi_nav: SOME/IP client for the KOMBI's route-guidance service (0xB006, TCP 160.48.199.96:30502 on the J29).
//
// - One persistent connection for the model updates. A connection error (EOF, reset, timeout) gets ONE reconnect and
//   a resend of that frame (model updates are idempotent); a reply that isn't a matching E_OK is never retried.
// - Queued KOMBI messages (SOME/IP magic cookies, about every 10 s) are drained before each request and skipped
//   while waiting for a reply.
// - The cancel always runs on a fresh connection, with the state-0 frame alone as a fallback.
// - Only whitelisted methods are sent (kombi_graph.hpp methodAllowed): never create / dispose.
// - emergencyCancel() is async-signal-safe (plain syscalls on a prebuilt buffer) for the fatal-signal handler.
#ifndef KOMBI_CLIENT_HPP
#define KOMBI_CLIENT_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

#include "kombi_graph.hpp"

namespace nbt {
namespace kombinav {

class KombiClient {
public:
    enum Result { kOk = 0, kLink = 1, kRejected = 2 };
    static const uint16_t kClientId = 0x7E01;   // our own client id; the factory HU uses its own

    KombiClient(const std::string& host, uint16_t port, int timeout_ms = 3000);
    ~KombiClient();

    // Sends the frames in order on the persistent connection. detail gets a short reason on failure.
    Result send(const std::vector<Frame>& frames, std::string* detail = NULL);

    // Factory cancel on a fresh connection (+ state-0 fallback). Retries every wait_s until retry_s has passed or
    // abort(user) returns true. True = the KOMBI acknowledged it.
    typedef bool (*AbortFn)(void* user);
    bool cancel(const Graph& g, double retry_s = 0.0, double wait_s = 5.0, AbortFn abort = NULL, void* user = NULL,
                std::string* detail = NULL);

    void close();
    void setTimeoutMs(int ms) { timeout_ms_ = ms; }   // connect and per-reply timeout (exit: shorter)

    // Prepares the buffer emergencyCancel() sends (call once the handle is known). Not thread-safe with itself.
    void prepareEmergency(const Graph& g);
    // Async-signal-safe one-shot cancel (1 s connect, 1 s per reply). Does nothing until prepareEmergency().
    static void emergencyCancel();

private:
    int connectFd(int timeout_ms) const;
    bool sendFrame(int fd, uint16_t method, const Bytes& body, uint16_t session, std::string* detail,
                   bool* rejected) const;
    bool drain(std::string* detail);
    bool runPhase(const std::vector<Frame>& frames, uint16_t session0, std::string* detail) const;
    uint16_t nextSession();

    std::string host_;
    uint16_t port_;
    int timeout_ms_;
    int fd_;
    uint16_t session_;
};

} // namespace kombinav
} // namespace nbt

#endif
