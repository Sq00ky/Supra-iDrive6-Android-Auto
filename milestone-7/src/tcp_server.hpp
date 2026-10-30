// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

#include <stdint.h>
#include <pthread.h>
#include <signal.h>

namespace nbt {

typedef void (*ClientConnectedCallback)(int client_socket_fd, void* user_data);

class TcpServer {
public:
    TcpServer(uint16_t port = 5277);
    ~TcpServer();

    bool start();
    void stop();

    void setConnectionCallback(ClientConnectedCallback callback, void* user_data = NULL);
    void notifySessionEnded();
    void markSessionActive() { has_active_client_ = true; }   // re-assert after AASession::start() returned
    // Close and re-create the listening socket (done by the accept thread within ~1 s). Used when a phone took the
    // Wi-Fi details but never connected, or when the car's access point was restarted (ignition off/on while the
    // head unit stayed up). `why` must be a string literal.
    void requestRebind(const char* why) { rebind_why_ = why; rebind_ = true; }

private:
    static void* acceptThreadStub(void* arg);
    void acceptLoop();

    static void* connectorThreadStub(void* arg);
    void connectorLoop();

    int tryConnect(const char* ip, uint16_t port, int timeout_ms);
    int openListener();                 // socket + SO_REUSEADDR + bind(INADDR_ANY:port_) + listen, -1 on failure
    void rebindNow(const char* why);


    uint16_t port_;
    int server_fd_;
    pthread_t accept_thread_;
    pthread_t connector_thread_;
    volatile bool is_running_;
    volatile bool has_active_client_;

    ClientConnectedCallback client_callback_;
    void* client_user_data_;
    volatile bool rebind_;
    const char* volatile rebind_why_;
    unsigned sel_errs_;                 // consecutive select()/accept() errors on the listener
    int last_err_;
    unsigned accepts_;
};

} // namespace nbt

#endif // TCP_SERVER_HPP
