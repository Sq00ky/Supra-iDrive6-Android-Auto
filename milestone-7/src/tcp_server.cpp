// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "nbt_env.hpp"
#include "tcp_server.hpp"
#include "alt_wifi.hpp"   // which Wi-Fi the phone is offered (NBT_ALT_WIFI): listener + accept filter
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>

namespace nbt {

// Low-latency + dead-peer detection for AA sockets. Small frames (media ACKs, input)
// must not wait for Nagle/delayed-ACK; a vanished wireless phone must eventually
// produce an error instead of an eternal recv().
static void tuneSocket(int fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
#ifdef TCP_KEEPIDLE
    int idle = 10;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#endif
#ifdef TCP_KEEPINTVL
    int intvl = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
#endif
#ifdef TCP_KEEPCNT
    int cnt = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif
}

TcpServer::TcpServer(uint16_t port)
    : port_(port),
      server_fd_(-1),
      accept_thread_(0),
      connector_thread_(0),
      is_running_(false),
      has_active_client_(false),
      client_callback_(NULL),
      client_user_data_(NULL),
      rebind_(false),
      rebind_why_(NULL),
      sel_errs_(0),
      last_err_(0),
      accepts_(0),
      aaNetCount_(0) {
}

TcpServer::~TcpServer() {
    stop();
}

void TcpServer::notifySessionEnded() {
    has_active_client_ = false;
}

int TcpServer::tryConnect(const char* ip, uint16_t port, int timeout_ms) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    // Set non-blocking for quick timeout probe
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    int res = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (res < 0) {
        if (errno == EINPROGRESS) {
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(sock, &write_fds);

            timeval tv;
            tv.tv_sec  = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            res = select(sock + 1, NULL, &write_fds, NULL, &tv);
            if (res > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err == 0) {
                    // Connected successfully! Restore blocking mode.
                    fcntl(sock, F_SETFL, flags);
                    tuneSocket(sock);
                    return sock;
                }
            }
        }
        close(sock);
        return -1;
    }

    fcntl(sock, F_SETFL, flags);
    tuneSocket(sock);
    return sock;
}

// The /24 of each Android Auto interface (uap0, wfd0) as far as it can be read now. Used by the accept filter when
// the listener is bound to every interface, so a client on the head unit's other networks is still refused.
void TcpServer::refreshAaNets() {
    aaNetCount_ = 0;
    const char* ifs[2] = { "uap0", "wfd0" };
    for (int i = 0; i < 2; ++i) {
        std::string ip;
        if (!nbt::interfaceIpv4(ifs[i], ip)) continue;
        struct in_addr a;
        if (!inet_aton(ip.c_str(), &a)) continue;
        unsigned net = ntohl(a.s_addr) & 0xffffff00u;
        bool dup = false;
        for (int x = 0; x < aaNetCount_; ++x) if (aaNets_[x] == net) dup = true;
        if (!dup && aaNetCount_ < (int)(sizeof(aaNets_) / sizeof(aaNets_[0]))) aaNets_[aaNetCount_++] = net;
    }
    printf("[TcpServer] Android Auto networks for the accept filter: %d read\n", aaNetCount_);
}

int TcpServer::openListener() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("[TcpServer] socket creation failed");
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port   = htons(port_);
    bool bound = false;
    // Prefer the CarPlay AP address so a client on another interface cannot open a session.
    // The AP is often not up yet at start, so fall back to every interface and filter accepts.
    if (nbt::wirelessEnabled()) {
        // The address has to be the one the phone was told about: NBT_ALT_WIFI=DIRECT offers 172.16.222.1
        // (wfd0), CARPLAY 172.16.224.1 (uap0). Binding the CarPlay address in DIRECT mode let the phone join
        // the group and connect to nothing (car log 2026-11-03).
        refreshAaNets();
        const char* ip = nbt::wirelessListenIp();
        if (!ip) {
            printf("[TcpServer] listening on every interface (NBT_ALT_WIFI=%s: both subnets), accepts filtered to "
                   "the Android Auto networks\n", nbt::altWifiName(nbt::altWifiMode()));
        } else if (inet_aton(ip, &address.sin_addr) &&
                   bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
            printf("[TcpServer] listening on %s:%u (NBT_ALT_WIFI=%s)\n", ip, (unsigned)port_,
                   nbt::altWifiName(nbt::altWifiMode()));
            bound = true;
        } else {
            // The configured address is only a default: a car may number its interface differently (or the
            // interface may not be up yet). Ask the interface itself before giving up on a specific address.
            int saved = errno;
            std::string ifip;
            const char* wif = nbt::altWifiMode() == nbt::ALT_WIFI_DIRECT
                                  ? (getenv("NBT_WFD_IF") && *getenv("NBT_WFD_IF") ? getenv("NBT_WFD_IF") : "wfd0")
                                  : (getenv("NBT_AP_IF") && *getenv("NBT_AP_IF") ? getenv("NBT_AP_IF") : "uap0");
            if (nbt::interfaceIpv4(wif, ifip) && inet_aton(ifip.c_str(), &address.sin_addr) &&
                bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                printf("[TcpServer] listening on %s:%u (NBT_ALT_WIFI=%s; %s, not the configured %s)\n",
                       ifip.c_str(), (unsigned)port_, nbt::altWifiName(nbt::altWifiMode()), wif, ip);
                bound = true;
            } else {
                printf("[TcpServer] could not bind %s:%u (%s) nor %s; listening on all interfaces\n", ip,
                       (unsigned)port_, strerror(saved), wif);
                memset(&address, 0, sizeof(address));
                address.sin_family = AF_INET;
                address.sin_port = htons(port_);
            }
        }
    }
    if (!bound) {
        address.sin_addr.s_addr = INADDR_ANY;
        if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            perror("[TcpServer] bind failed");
            close(fd);
            return -1;
        }
    }

    if (listen(fd, 5) < 0) {
        perror("[TcpServer] listen failed");
        close(fd);
        return -1;
    }
    return fd;
}

// Runs on the accept thread only (the only user of server_fd_ while running).
void TcpServer::rebindNow(const char* why) {
    int old = server_fd_;
    if (old >= 0) close(old);
    server_fd_ = -1;
    int fd = -1;
    for (int i = 0; i < 5 && is_running_ && fd < 0; ++i) {
        fd = openListener();
        if (fd < 0) sleep(1);
    }
    server_fd_ = fd;
    printf("[TcpServer] listener on port %u re-created (%s): fd %d -> %d, %u accepted so far, %u socket errors (last: %s)\n",
           port_, why ? why : "?", old, fd, accepts_, sel_errs_, last_err_ ? strerror(last_err_) : "none");
    sel_errs_ = 0;
    last_err_ = 0;
}

bool TcpServer::start() {
    if (is_running_) return true;

    server_fd_ = openListener();
    if (server_fd_ < 0) return false;

    is_running_ = true;
    has_active_client_ = false;

    pthread_create(&accept_thread_, NULL, &TcpServer::acceptThreadStub, this);
    pthread_create(&connector_thread_, NULL, &TcpServer::connectorThreadStub, this);

    printf("[TcpServer] Bi-directional Android Auto engine active on port %u (Listener + Auto-Connector)\n", port_);
    printf("[TcpServer] Notice: If phone does not connect, verify firewall is disabled via 'pfctl -d'.\n");
    return true;
}

void TcpServer::stop() {
    if (!is_running_) return;
    is_running_ = false;

    if (accept_thread_) {
        pthread_join(accept_thread_, NULL);   // accept loop polls is_running_ every second
        accept_thread_ = 0;
    }

    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }

    if (accept_thread_) {
        pthread_join(accept_thread_, NULL);
        accept_thread_ = 0;
    }

    if (connector_thread_) {
        pthread_join(connector_thread_, NULL);
        connector_thread_ = 0;
    }

    printf("[TcpServer] Listener and auto-connector stopped.\n");
}

void TcpServer::setConnectionCallback(ClientConnectedCallback callback, void* user_data) {
    client_callback_ = callback;
    client_user_data_ = user_data;
}

void* TcpServer::acceptThreadStub(void* arg) {
    reinterpret_cast<TcpServer*>(arg)->acceptLoop();
    return NULL;
}

void* TcpServer::connectorThreadStub(void* arg) {
    reinterpret_cast<TcpServer*>(arg)->connectorLoop();
    return NULL;
}

void TcpServer::acceptLoop() {
    while (is_running_) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        if (rebind_) {
            rebind_ = false;
            rebindNow(rebind_why_);
        }
        // Poll with a timeout instead of blocking in accept(): closing a listening
        // socket does not reliably wake a thread blocked in accept() on QNX, which
        // made stop() (and therefore Ctrl+C) hang in pthread_join.
        int lfd = server_fd_;
        if (lfd < 0) {
            // A failed re-create leaves no listener: keep trying instead of ending the thread.
            sleep(1);
            if (is_running_) rebindNow("no listening socket");
            continue;
        }
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(lfd, &rf);
        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int sr = select(lfd + 1, &rf, NULL, NULL, &tv);
        if (sr <= 0) {
            if (sr < 0 && errno != EINTR && is_running_) {
                last_err_ = errno;
                if (++sel_errs_ == 1) printf("[TcpServer] select() on the listener failed: %s\n", strerror(errno));
                // A listener that keeps failing (e.g. the network stack dropped it while the car's Wi-Fi was
                // switched off) can never accept the phone again: re-create it.
                if (sel_errs_ >= 3) rebindNow("listener socket keeps failing");
                else sleep(1);
            }
            continue;
        }

        int client_fd = accept(lfd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (!is_running_) break;
            if (errno != EINTR && errno != ECONNABORTED && errno != EAGAIN) {
                last_err_ = errno;
                perror("[TcpServer] accept failed");
                if (++sel_errs_ >= 3) rebindNow("accept() keeps failing");
                else sleep(1);   // do not spin at 100% CPU on persistent errors (EMFILE...)
            }
            continue;
        }
        sel_errs_ = 0;
        ++accepts_;
        tuneSocket(client_fd);

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("[TcpServer] Incoming Android Auto connection from %s:%u (fd: %d)!\n",
               client_ip, ntohs(client_addr.sin_port), client_fd);

        if (has_active_client_) {
            printf("[TcpServer] session already active, refusing %s\n", client_ip);
            close(client_fd);
            continue;
        }
        if (nbt::wirelessEnabled()) {
            // Which address did this connection arrive on? The phone has to be inside that network; that holds
            // whatever the car numbers wfd0/uap0, and still keeps Ethernet/USB/OBD clients out.
            uint32_t hip = ntohl(client_addr.sin_addr.s_addr);
            uint32_t lip = 0;
            sockaddr_in local;
            socklen_t llen = sizeof(local);
            memset(&local, 0, sizeof(local));
            if (getsockname(client_fd, reinterpret_cast<sockaddr*>(&local), &llen) == 0)
                lip = ntohl(local.sin_addr.s_addr);
            if (!nbt::wfdPeerInNets(hip, aaNets_, (size_t)aaNetCount_, lip)) {
                char lip_s[INET_ADDRSTRLEN] = "?";
                inet_ntop(AF_INET, &local.sin_addr, lip_s, sizeof(lip_s));
                printf("[TcpServer] refusing %s (not on %s: NBT_ALT_WIFI=%s)\n", client_ip, lip_s,
                       nbt::altWifiName(nbt::altWifiMode()));
                close(client_fd);
                continue;
            }
        }

        has_active_client_ = true;
        if (client_callback_) {
            client_callback_(client_fd, client_user_data_);
        } else {
            close(client_fd);
        }
    }
}

void TcpServer::connectorLoop() {
    // Known priority IPs where the phone has connected in past runs
    static const int priority_last_octets[] = {
        110, 111, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
        112, 113, 114, 115, 116, 117, 118, 119, 120, 125, 130, 140, 150,
        2, 3, 4, 5, 10, 20
    };
    static const size_t num_priorities = sizeof(priority_last_octets) / sizeof(priority_last_octets[0]);

    // Native wireless mode: the phone connects to us after the Bluetooth handshake,
    // so the outbound scanner (dev "head unit server" workflow) is disabled.
    const bool scanner_disabled = (nbt::wirelessEnabled());
    if (scanner_disabled) {
        printf("[TcpServer] Wireless mode (default): outbound phone scanner disabled, accept-only.\n");
    }

    while (is_running_) {
        if (has_active_client_ || scanner_disabled) {
            sleep(1);
            continue;
        }

        // 1. Probe priority targets on port 5277
        char ip_buf[32];
        for (size_t i = 0; i < num_priorities && is_running_ && !has_active_client_; ++i) {
            snprintf(ip_buf, sizeof(ip_buf), "172.16.224.%d", priority_last_octets[i]);
            int sock = tryConnect(ip_buf, port_, 80);
            if (sock >= 0) {
                printf("[TcpServer] >>> FOUND ANDROID AUTO PHONE at %s:%u (fd: %d)! Connected!\n",
                       ip_buf, port_, sock);
                has_active_client_ = true;
                if (client_callback_) {
                    client_callback_(sock, client_user_data_);
                } else {
                    close(sock);
                }
                while (is_running_ && has_active_client_) {
                    sleep(1);
                }
                break;
            }
        }

        // 2. Full sweep across entire 172.16.224.100 - 172.16.224.150 range
        for (int octet = 100; octet <= 150 && is_running_ && !has_active_client_; ++octet) {
            snprintf(ip_buf, sizeof(ip_buf), "172.16.224.%d", octet);
            int sock = tryConnect(ip_buf, port_, 60);
            if (sock >= 0) {
                printf("[TcpServer] >>> FOUND ANDROID AUTO PHONE at %s:%u (fd: %d)! Connected!\n",
                       ip_buf, port_, sock);
                has_active_client_ = true;
                if (client_callback_) {
                    client_callback_(sock, client_user_data_);
                } else {
                    close(sock);
                }
                while (is_running_ && has_active_client_) {
                    sleep(1);
                }
                break;
            }
        }

        // Rest before next scanning cycle
        for (int i = 0; i < 15 && is_running_ && !has_active_client_; ++i) {
            usleep(100000); // 100ms
        }
    }
}

} // namespace nbt
