// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "hudprobe_protocol.hpp"
#include "hudprobe_text.hpp"
#include "hudprobe_visual.hpp"
#include "most/client.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

using namespace nbt;
using namespace nbt::hudprobe;

namespace {

const uint8_t kInstances[] = {0, 2};
const uint16_t kCreateInstance = 0x200;
const uint16_t kFrameAddItem = 0x22B;
const unsigned kMaxFactoryEntries = 96;
const unsigned kMaxGuidanceEntries = 64;
const unsigned kMaxVisualLayouts = 16;
volatile sig_atomic_t interrupted = 0;

enum ProbeReply { kAccepted, kRefused, kUnconfirmed, kNotSent };

void interrupt(int) { interrupted = 1; }

uint32_t milliseconds() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)now.tv_sec * 1000u + (uint32_t)(now.tv_nsec / 1000000);
}

// A TTY gives us complete lines. A bounded poll lets cancellation clear the picture even if nobody answers.
bool inputLine(std::string& line, uint32_t start, uint32_t timeout) {
    line.clear();
    while (!interrupted && (uint32_t)(milliseconds() - start) < timeout) {
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        const int ready = poll(&input, 1, 200);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return false;
        if (ready == 0) continue;
        char c;
        if (!(input.revents & POLLIN) || read(STDIN_FILENO, &c, 1) != 1) return false;
        if (c == '\n') return true;
        if (c != '\r' && line.size() < 80) line += c;
    }
    return false;
}

std::string screenAnswer(const char* screen, uint32_t start, uint32_t timeout) {
    std::string answer;
    for (;;) {
        printf("%s [full, none, clipped edges l/r/t/b (e.g. rt), ?, q to stop]: ", screen);
        fflush(stdout);
        if (!inputLine(answer, start, timeout)) return "unanswered";
        if (visualAnswer(answer)) return answer;
        puts("Enter full, none, ?, q, or the letters of each clipped edge.");
    }
}

bool loadLayouts(const char* path, std::vector<VisualLayout>& layouts) {
    std::ifstream file(path);
    if (!file) {
        fprintf(stderr, "Cannot open visual layout file %s\n", path);
        return false;
    }
    std::string line;
    unsigned lineNo = 0;
    while (std::getline(file, line)) {
        ++lineNo;
        const size_t comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        VisualLayout layout;
        if (layouts.size() == kMaxVisualLayouts || !parseVisualLayout(line, layout)) {
            fprintf(stderr, "%s:%u: invalid layout or more than %u tests\n", path, lineNo, kMaxVisualLayouts);
            return false;
        }
        layouts.push_back(layout);
    }
    if (!file.eof() || layouts.empty()) {
        fprintf(stderr, "%s: no usable visual test plan\n", path);
        return false;
    }
    return true;
}

struct FactoryEntry {
    uint16_t function, first, second, kind;

    bool operator==(const FactoryEntry& o) const {
        return function == o.function && first == o.first && second == o.second && kind == o.kind;
    }
};

struct Terminal {
    uint16_t host;
    Size sizes[kForms][kLastHandle + 1];
    bool requested[kForms][kLastHandle + 1];
    unsigned refused, sendFailures;
    std::vector<FactoryEntry> setup;
    std::vector<FactoryEntry> guidance;

    Terminal() : host(0), refused(0), sendFailures(0) {
        memset(requested, 0, sizeof(requested));
        setup.reserve(kMaxFactoryEntries);
        guidance.reserve(kMaxGuidanceEntries);
    }

    bool surface(unsigned handle) const {
        return sizes[kShort][handle].present() || sizes[kWithSelector][handle].present();
    }
};

class Probe {
public:
    Probe() : haveRegistry_(false), tag_(0), activeHost_(0), activeFunction_(0), activeHandle_(0),
              activeTerminal_(0), replyOp_(0) { pthread_mutex_init(&mu_, NULL); }

    bool start() {
        for (unsigned i = 0; i < 2; ++i) {
            if (!client_.allowSend(hudterm::kFBlock, kInstances[i]) ||
                !client_.addRoute(hudterm::kFBlock, kInstances[i], most::kAny, most::kAny, &Probe::message, this))
                return false;
        }
        return client_.addRegistryListener(&Probe::registry, this) && client_.start("NBTAAD_HUDPROBE");
    }

    bool ready() {
        pthread_mutex_lock(&mu_);
        const bool registrySeen = haveRegistry_;
        pthread_mutex_unlock(&mu_);
        return registrySeen && client_.state() == most::kUp;
    }

    void scan() {
        for (unsigned i = 0; i < 2; ++i) {
            pthread_mutex_lock(&mu_);
            const uint16_t host = terminals_[i].host;
            pthread_mutex_unlock(&mu_);
            if (!host) continue;
            printf("Querying HUDTerminal 82/%02X at %04X (handles 01-40)\n", kInstances[i], host);
            fflush(stdout);
            for (unsigned form = 0; form < kForms; ++form) {
                for (unsigned handle = 1; handle <= kLastHandle; ++handle) {
                    pthread_mutex_lock(&mu_);
                    const bool sameHost = terminals_[i].host == host;
                    if (sameHost) terminals_[i].requested[form][handle] = true;
                    pthread_mutex_unlock(&mu_);
                    if (!sameHost) break;

                    const most::Msg request = sizeQuery(host, kInstances[i], handle, (SizeForm)form, ++tag_);
                    if (!client_.send(request)) {
                        pthread_mutex_lock(&mu_);
                        ++terminals_[i].sendFailures;
                        pthread_mutex_unlock(&mu_);
                        break;
                    }
                    usleep(20000);
                }
            }
        }
        usleep(750000);
    }

    bool visual(const std::vector<VisualLayout>& layouts, uint32_t answerMs) {
        puts("\nVisual test requires the car parked, all navigation routes ended and other HUD apps stopped.");
        printf("Type YES to start: ");
        fflush(stdout);
        std::string confirm;
        if (!inputLine(confirm, milliseconds(), 45000) || confirm != "YES") return false;

        bool tested = false;
        for (size_t i = 0; i < layouts.size() && !interrupted; ++i) {
            const VisualLayout& l = layouts[i];
            pthread_mutex_lock(&mu_);
            const uint16_t host = terminals_[l.terminal == 2 ? 1 : 0].host;
            pthread_mutex_unlock(&mu_);
            printf("\nTest %s 82/%02X panel=0x%02X buffer=0x%02X bounds=(%d,%d)-(%d,%d) colour=%u\n",
                   l.name.c_str(), l.terminal, l.panel, l.buffer, l.left, l.top, l.right, l.bottom, l.colour);
            if (!host) {
                puts("Result: skipped (terminal missing from MOST registry)");
                continue;
            }
            tested = true;
            const std::vector<hudterm::Call> pattern = visualPattern(l);
            // If the initial clear is refused or times out, no test graphic was sent. Try the next layout.
            if (!visualCall(l, host, pattern.front(), false)) {
                puts("Result: skipped (picture buffer did not acknowledge the initial clear)");
                continue;
            }
            bool drawn = true, panelAttempted = false;
            for (size_t j = 1; j < pattern.size(); ++j) {
                panelAttempted = j + 1 == pattern.size();
                if (!visualCall(l, host, pattern[j], false)) {
                    drawn = false;
                    break;
                }
            }
            std::string cluster = "unanswered", hud = "unanswered";
            if (drawn) {
                puts("Look for a rectangle with a centre cross and a dot near the top-left corner.");
                const uint32_t start = milliseconds();
                cluster = screenAnswer("Instrument cluster", start, answerMs);
                if (cluster != "q" && cluster != "unanswered") hud = screenAnswer("Windshield HUD", start, answerMs);
            }
            const std::vector<hudterm::Call> clear = clearVisualPattern(l, panelAttempted);
            bool cleared = true;
            for (size_t j = 0; j < clear.size(); ++j) {
                if (!visualCall(l, host, clear[j], true)) cleared = false;
            }
            printf("Result: draw=%s; cluster=%s; windshield=%s; cleanup=%s\n",
                   drawn ? "acknowledged" : "incomplete", cluster.c_str(), hud.c_str(),
                   cleared ? "acknowledged" : "unconfirmed");
            fflush(stdout);
            if (!cleared) {
                puts("Visual test stopped because the terminal did not confirm cleanup.");
                return false;
            }
            if (interrupted || cluster == "q" || hud == "q") return true;
            if (drawn && (cluster == "unanswered" || hud == "unanswered")) return false;
        }
        return tested;
    }

    bool textTest(const char* const labels[], uint32_t answerMs) {
        const uint16_t panel = 1;
        const uint16_t handles[] = {0x0C, 0x0D, 0x10};
        pthread_mutex_lock(&mu_);
        const uint16_t host = terminals_[0].host;
        pthread_mutex_unlock(&mu_);
        if (!host) {
            puts("Text test skipped: HUDTerminal 82/00 missing from MOST registry.");
            return false;
        }
        puts("Text test requires the car parked, all navigation routes ended and other HUD apps stopped.");
        printf("Type YES to start: ");
        fflush(stdout);
        std::string input;
        if (!inputLine(input, milliseconds(), 45000) || input != "YES") return false;

        const TextEncoding encodings[] = {kLatin9, kUtf16, kUtf8};
        TextEncoding working[] = {kTextEncodings, kTextEncodings, kTextEncodings};
        for (unsigned step = 0; step < kTextEncodings && !interrupted; ++step) {
            const TextEncoding encoding = encodings[step];
            const unsigned e = (unsigned)encoding;
            printf("\nText test %02X %s, 82/00: 0x0C=%s, 0x0D=%s, 0x10=%s\n",
                   e, encodingName(e), labels[0], labels[1], labels[2]);
            ProbeReply writes[] = {kNotSent, kNotSent, kNotSent};
            bool visibilityAttempted[] = {false, false, false};
            const bool panelVisible = setProbeVisibility(host, panel, true, false);
            bool anyAccepted = false;
            for (unsigned i = 0; panelVisible && i < 3 && !interrupted; ++i) {
                visibilityAttempted[i] = true;
                if (!setProbeVisibility(host, handles[i], true, false)) continue;
                writes[i] = sendText(host, handles[i], encoding, labels[i], false);
                if (writes[i] != kAccepted) continue;
                anyAccepted = true;
                working[i] = encoding;
                printf("  0x%02X accepted %s (marker %s)\n", handles[i], encodingName(e), labels[i]);
            }
            const bool showing = panelVisible && anyAccepted;
            std::string observation = showing ? "unanswered" : "no marker confirmed on panel";
            if (showing) {
                printf("Which markers are visible, on which screen and in what positions? (none or q): ");
                fflush(stdout);
                if (inputLine(input, milliseconds(), answerMs) && !input.empty()) observation = input;
            }
            bool cleared = true;
            for (unsigned i = 0; i < 3; ++i) {
                if (writes[i] == kAccepted || writes[i] == kUnconfirmed) {
                    const TextEncoding clearEncoding = working[i] == kTextEncodings ? encoding : working[i];
                    if (sendText(host, handles[i], clearEncoding, "", true) != kAccepted) cleared = false;
                }
                if (visibilityAttempted[i] && !setProbeVisibility(host, handles[i], false, true)) cleared = false;
            }
            if (!setProbeVisibility(host, panel, false, true)) cleared = false;
            printf("Text result %s: %s (cleanup %s)\n", encodingName(e), observation.c_str(),
                   cleared ? "acknowledged" : "unconfirmed");
            if (!cleared || !panelVisible) return false;
            if (observation == "q" || interrupted) return true;
            if (observation == "unanswered") return false;
        }
        return true;
    }

    void report(bool ethernetReached) {
        pthread_mutex_lock(&mu_);
        const bool registrySeen = haveRegistry_;
        const std::vector<most::RegistryEntry> registryEntries = registryEntries_;
        const Terminal cluster = terminals_[0];
        const Terminal secondary = terminals_[1];
        pthread_mutex_unlock(&mu_);

        printf("\nMOST link: %s; registry: %s\n", most::linkStateName(client_.state()),
               registrySeen ? "received" : "unavailable");
        for (size_t i = 0; i < registryEntries.size(); ++i) {
            printf("  HUDTerminal %02X/%02X at %04X\n", registryEntries[i].key >> 8,
                   registryEntries[i].key & 0xFF, registryEntries[i].address);
        }
        const char* transport = "undetermined";
        if (ethernetReached && !registryEntries.empty()) transport = "MOST HUDTerminal and reachable Ethernet endpoint";
        else if (ethernetReached) transport = "reachable Ethernet KOMBI candidate";
        else if (!registryEntries.empty()) transport = "MOST HUDTerminal";
        printf("Cluster transport evidence: %s\n", transport);
        puts("82/00 and 82/02 are reported separately; the physical display behind each can vary by car.");
        printTerminal(0, cluster, registrySeen);
        printTerminal(2, secondary, registrySeen);
    }

private:
    bool visualCall(const VisualLayout& layout, uint16_t host, const hudterm::Call& call, bool clearing) {
        if (interrupted && !clearing) return false;
        hudterm::HandleSet handles;
        handles.allow(layout.panel);
        handles.allow(layout.buffer);
        if (layout.swapFrom) handles.allow(layout.swapFrom);
        most::Msg request;
        if (!hudterm::buildCall(host, call, ++tag_, handles, request)) return false;
        request.inst = layout.terminal;
        return probeCall(request, call.handle(), clearing) == kAccepted;
    }

    bool setProbeVisibility(uint16_t host, uint16_t handle, bool visible, bool clearing) {
        hudterm::HandleSet handles;
        handles.allow(handle);
        const hudterm::Call call(hudterm::kSetVisibility, hudterm::setVisibility(handle, visible));
        most::Msg request;
        return hudterm::buildCall(host, call, ++tag_, handles, request) &&
               probeCall(request, handle, clearing) == kAccepted;
    }

    ProbeReply sendText(uint16_t host, uint16_t handle, TextEncoding encoding, const char* text, bool clearing) {
        most::Msg request;
        if (!textProbeMessage(host, handle, encoding, text, ++tag_, request)) return kNotSent;
        return probeCall(request, handle, clearing);
    }

    ProbeReply probeCall(const most::Msg& request, uint16_t handle, bool clearing) {
        if (interrupted && !clearing) return kNotSent;
        pthread_mutex_lock(&mu_);
        const Terminal& terminal = terminals_[request.inst == 2 ? 1 : 0];
        if (terminal.host != request.dst) {
            pthread_mutex_unlock(&mu_);
            return kNotSent;
        }
        activeHost_ = request.dst;
        activeTerminal_ = request.inst;
        activeFunction_ = request.fkt;
        activeHandle_ = handle;
        replyOp_ = 0;
        pthread_mutex_unlock(&mu_);

        const bool sent = client_.send(request);
        uint8_t reply = 0;
        for (unsigned i = 0; sent && i < 75 && (clearing || !interrupted); ++i) {
            pthread_mutex_lock(&mu_);
            reply = replyOp_;
            pthread_mutex_unlock(&mu_);
            if (reply) break;
            usleep(10000);
        }
        pthread_mutex_lock(&mu_);
        activeHost_ = 0;
        pthread_mutex_unlock(&mu_);
        if (reply == most::kOpResultAck || reply == most::kOpStatus) return kAccepted;
        printf("  %s handle 0x%02X: %s (reply op 0x%X)\n", hudterm::fktName(request.fkt), handle,
               !sent ? "send failed" : reply ? "refused" : "no acknowledgement", reply);
        if (!sent) return kNotSent;
        return reply ? kRefused : kUnconfirmed;
    }

    static void registry(const std::vector<most::RegistryEntry>& entries, void* user) {
        Probe& p = *static_cast<Probe*>(user);
        pthread_mutex_lock(&p.mu_);
        p.haveRegistry_ = true;
        p.registryEntries_.clear();
        for (size_t i = 0; i < entries.size(); ++i) {
            if ((entries[i].key >> 8) == hudterm::kFBlock) p.registryEntries_.push_back(entries[i]);
        }
        for (unsigned i = 0; i < 2; ++i) {
            const most::RegistryEntry* e =
                most::findRegistryEntry(entries, most::registryKey(hudterm::kFBlock, kInstances[i]));
            const uint16_t host = e && hudterm::isRingNode(e->address) ? e->address : 0;
            if (p.terminals_[i].host != host) {
                p.terminals_[i] = Terminal();
                p.terminals_[i].host = host;
            }
        }
        pthread_mutex_unlock(&p.mu_);
    }

    static void message(const most::View& m, void* user) {
        Probe& p = *static_cast<Probe*>(user);
        const uint16_t self = p.client_.selfAddress();
        if (!self || m.src == self) return;
        pthread_mutex_lock(&p.mu_);
        Terminal& terminal = p.terminals_[m.inst == 2 ? 1 : 0];
        if (terminal.host && m.src == terminal.host) {
            if (m.dst == self && m.fkt == hudterm::kGetSize) {
                unsigned handle;
                SizeForm form;
                Size size;
                if (sizeReply(m, handle, form, size) && terminal.requested[form][handle])
                    terminal.sizes[form][handle] = size;
                else if (m.op == most::kOpErrorAck || m.op == most::kOpError)
                    ++terminal.refused;
            } else if (m.dst == self && m.src == p.activeHost_ && m.inst == p.activeTerminal_ &&
                       m.fkt == p.activeFunction_ &&
                       (m.op == most::kOpError ||
                        (m.len >= 2 && hudterm::rdBe16(m.data) == p.activeHandle_ &&
                         (m.op == most::kOpResultAck || m.op == most::kOpStatus || m.op == most::kOpErrorAck)))) {
                p.replyOp_ = m.op;
            }
        }
        if (terminal.host && m.dst != self) {
            if (m.src == terminal.host && m.op == most::kOpResultAck) recordSetup(terminal, m);
            const bool reply = m.src == terminal.host && (m.op == most::kOpResultAck || m.op == most::kOpStatus);
            const bool command = m.dst == terminal.host && m.op <= most::kOpStartAck;
            if (reply || command) recordGuidance(terminal, m);
        }
        pthread_mutex_unlock(&p.mu_);
    }

    static void recordSetup(Terminal& terminal, const most::View& m) {
        if (terminal.setup.size() >= kMaxFactoryEntries) return;
        if (!((m.fkt == kCreateInstance && m.len >= 4) || (m.fkt == kFrameAddItem && m.len >= 7))) return;
        FactoryEntry entry = {m.fkt, hudterm::rdBe16(m.data), hudterm::rdBe16(m.data + 2), 0};
        if (m.fkt == kFrameAddItem) entry.kind = hudterm::rdBe16(m.data + 5);
        if (!entry.first || !entry.second) return;
        for (size_t i = 0; i < terminal.setup.size(); ++i) {
            if (terminal.setup[i] == entry) return;
        }
        terminal.setup.push_back(entry);
    }

    static void recordGuidance(Terminal& terminal, const most::View& m) {
        if (m.len < 2 || terminal.guidance.size() >= kMaxGuidanceEntries) return;
        const uint16_t handle = hudterm::rdBe16(m.data);
        if (!handle) return;
        unsigned second = 0;
        TextKind textKind = kUnknownText;
        switch (m.fkt) {
            case hudterm::kSetVisibility:
                if (m.len >= 3 && m.data[2] <= 1) second = 1 + m.data[2];
                break;
            case hudterm::kSwitchVisibility:
                if (m.len >= 4) second = hudterm::rdBe16(m.data + 2);
                break;
            case hudterm::kTextareaSetText:
                textKind = factoryTextKind(m.data, m.len);
                if (textKind != kUnknownText) second = 1 + m.data[2];
                break;
            case hudterm::kAddPolygon:
            case hudterm::kAddCircle:
            case hudterm::kAddCircleDiameter:
            case hudterm::kRemoveAllItems:
            case 0xC05: // ImageAddBitmap
            case 0x227: // PotentiometerSetValue
                break;
            default:
                return;
        }
        const FactoryEntry entry = {m.fkt, handle, (uint16_t)second, (uint16_t)textKind};
        for (size_t i = 0; i < terminal.guidance.size(); ++i) {
            if (terminal.guidance[i] == entry) return;
        }
        terminal.guidance.push_back(entry);
    }

    static void printTerminal(unsigned instance, const Terminal& t, bool registrySeen) {
        printf("\nHUDTerminal 82/%02X\n", instance);
        if (!t.host) {
            puts(registrySeen ? "  No usable ring-node address in the registry." : "  Registry unavailable.");
            return;
        }
        bool low = t.surface(5) || t.surface(6);
        bool high = t.surface(0x1F) || t.surface(0x20);
        for (size_t i = 0; i < t.setup.size(); ++i) {
            const FactoryEntry& e = t.setup[i];
            if (e.kind == 4) {
                low = low || e.second == 5 || e.second == 6;
                high = high || e.second == 0x1F || e.second == 0x20;
            }
        }
        printf("  Layout hint: %s\n", layoutHint(low, high));
        unsigned replies = 0, positive = 0;
        for (unsigned h = 1; h <= kLastHandle; ++h) {
            for (unsigned f = 0; f < kForms; ++f) {
                const Size& s = t.sizes[f][h];
                replies += s.answered ? 1 : 0;
                if (!s.present()) continue;
                ++positive;
                printf("  Surface 0x%02X: %u x %u (%s)\n", h, s.width, s.height,
                       f == kShort ? "handle only" : "handle + selector 01");
            }
        }
        printf("  Size replies: %u (%u positive); errors: %u; sends failed: %u\n",
               replies, positive, t.refused, t.sendFailures);
        if (!t.setup.empty()) puts("  Observed factory setup:");
        for (size_t i = 0; i < t.setup.size(); ++i) {
            const FactoryEntry& e = t.setup[i];
            if (e.function == kCreateInstance)
                printf("    instance index 0x%04X -> handle 0x%04X\n", e.first, e.second);
            else
                printf("    frame 0x%04X item 0x%04X: kind %u (%s)\n",
                       e.first, e.second, e.kind, itemRole(e.kind));
        }
        if (t.setup.empty()) puts("  No factory layout setup observed in this run.");
        if (t.guidance.empty()) puts("  No factory guidance drawing traffic observed.");
        else puts("  Observed factory guidance handles:");
        for (size_t i = 0; i < t.guidance.size(); ++i) {
            const FactoryEntry& e = t.guidance[i];
            printf("    %03X %s handle 0x%04X", e.function, hudterm::fktName(e.function), e.first);
            if (e.function == hudterm::kSwitchVisibility && e.second) printf(" -> 0x%04X", e.second);
            if (e.function == hudterm::kSetVisibility && e.second) printf(" %s", e.second == 2 ? "visible" : "hidden");
            if (e.function == hudterm::kTextareaSetText && e.second)
                printf(" %s, format %s", textKindName(e.kind), encodingName(e.second - 1));
            putchar('\n');
        }
    }

    most::Client client_;
    pthread_mutex_t mu_;
    bool haveRegistry_;
    std::vector<most::RegistryEntry> registryEntries_;
    Terminal terminals_[2];
    uint32_t tag_;
    uint16_t activeHost_, activeFunction_, activeHandle_;
    uint8_t activeTerminal_, replyOp_;
};

bool ethernetProbe(const char* host, unsigned port) {
    sockaddr_in peer;
    memset(&peer, 0, sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &peer.sin_addr) != 1) {
        printf("Ethernet KOMBI: skipped, invalid IPv4 address %s\n", host);
        return false;
    }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("Ethernet KOMBI: socket unavailable (%s)\n", strerror(errno));
        return false;
    }
    int error = 0;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) error = errno;
    else if (connect(fd, (sockaddr*)&peer, sizeof(peer)) < 0) {
        error = errno;
        if (error == EINPROGRESS) {
            struct pollfd event = {fd, POLLOUT, 0};
            const int result = poll(&event, 1, 1200);
            if (result > 0) {
                socklen_t len = sizeof(error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) error = errno;
            } else error = result == 0 ? ETIMEDOUT : errno;
        }
    }
    close(fd);
    printf("Ethernet KOMBI %s:%u: %s (TCP only)\n", host, port, error ? strerror(error) : "reachable");
    return error == 0;
}

long number(const char* arg, long min, long max) {
    char* end = NULL;
    const long value = strtol(arg, &end, 10);
    return *arg && !*end && value >= min && value <= max ? value : -1;
}

void usage() {
    puts("Usage: hudprobe [--observe SECONDS | --visual LAYOUT_FILE | --text]\n"
         "Query HUDTerminal 82/00 and 82/02 and check Ethernet KOMBI reachability.\n"
         "--observe 1..120  Listen for factory navigation setup and drawing before querying sizes.\n"
         "--visual FILE [--answer-seconds 15..120]   Draw temporary patterns and record visible/clipped edges on each screen.\n"
         "--text [--answer-seconds 15..120] [--markers A B C]   Test ASCII on 82/00 handles 0x0C, 0x0D and 0x10.\n"
         "NBT_KOMBI_NAV_HOST and NBT_KOMBI_NAV_PORT override 160.48.199.96:30502.");
}

} // namespace

int main(int argc, char** argv) {
    long observe = 0;
    const char* visualPath = NULL;
    long answerSeconds = 45;
    bool textMode = false;
    const char* markers[] = {"11", "22", "33"};
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        usage();
        return 0;
    }
    if (argc != 1) {
        if (argc == 3 && !strcmp(argv[1], "--observe")) observe = number(argv[2], 1, 120);
        else if (argc >= 3 && !strcmp(argv[1], "--visual")) {
            visualPath = argv[2];
            if (argc == 5 && !strcmp(argv[3], "--answer-seconds")) answerSeconds = number(argv[4], 15, 120);
            else if (argc != 3) answerSeconds = -1;
        }
        else if (!strcmp(argv[1], "--text")) {
            textMode = true;
            int i = 2;
            if (i + 1 < argc && !strcmp(argv[i], "--answer-seconds")) {
                answerSeconds = number(argv[i + 1], 15, 120);
                i += 2;
            }
            if (i + 3 < argc && !strcmp(argv[i], "--markers")) {
                for (unsigned n = 0; n < 3; ++n) {
                    markers[n] = argv[i + n + 1];
                    most::Msg checked;
                    if (!*markers[n] || !textProbeMessage(0x0101, 1, kUtf16, markers[n], 0, checked)) answerSeconds = -1;
                }
                i += 4;
            }
            if (i != argc) answerSeconds = -1;
        } else observe = -1;
        if (observe < 0 || answerSeconds < 0) {
            usage();
            return 2;
        }
    }

    std::vector<VisualLayout> layouts;
    if (visualPath || textMode) {
        if (!isatty(STDIN_FILENO)) {
            fputs("Visual mode needs an interactive terminal. Use ssh -t.\n", stderr);
            return 2;
        }
        if (visualPath && !loadLayouts(visualPath, layouts)) return 2;
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_handler = &interrupt;
        sigemptyset(&action.sa_mask);
        sigaction(SIGINT, &action, NULL);
        sigaction(SIGTERM, &action, NULL);
        sigaction(SIGHUP, &action, NULL);
        signal(SIGPIPE, SIG_IGN);
        setvbuf(stdout, NULL, _IOLBF, 0);
    }
    puts("NBT-AAD HUD probe. Reports surface queries and factory layout observations.");
    const char* host = getenv("NBT_KOMBI_NAV_HOST");
    const char* portText = getenv("NBT_KOMBI_NAV_PORT");
    const long port = portText ? number(portText, 1, 65535) : -1;
    const bool ethernetReached = ethernetProbe(host && *host ? host : "160.48.199.96", port > 0 ? (unsigned)port : 30502);

    // DSI registers callbacks for the process lifetime. Keep their state alive until process exit.
    Probe* probe = new Probe();
    const bool started = probe->start();
    if (started) {
        for (unsigned i = 0; i < 50 && !probe->ready(); ++i) usleep(100000);
        if (observe) {
            printf("Listening for factory navigation setup for %ld s. Start a parked factory route now if needed.\n", observe);
            fflush(stdout);
            for (long i = 0; i < observe; ++i) sleep(1);
        }
        probe->scan();
    }
    probe->report(ethernetReached);
    if (visualPath) return started && probe->visual(layouts, (uint32_t)answerSeconds * 1000u) ? 0 : 1;
    if (textMode) return started && probe->textTest(markers, (uint32_t)answerSeconds * 1000u) ? 0 : 1;
    return started || ethernetReached ? 0 : 1;
}
