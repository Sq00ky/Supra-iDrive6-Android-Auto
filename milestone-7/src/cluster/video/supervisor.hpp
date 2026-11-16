#pragma once
#include <string>
#include "cluster/profiles/video.hpp"

namespace aa_cluster {
struct SupervisorPaths {
    std::string version;
    std::string screen;
    std::string flags;
    std::string lock;
    std::string temporary; // Prefix for a per-owner/per-supervisor work directory.
    std::string ifconfig;
    std::string arp;
    std::string node;
    unsigned stop_ms;    // Grace period before killing a non-responsive child.
    unsigned command_ms; // Deadline for each inspection or network command.
    SupervisorPaths()
        : version("/opt/hmi/ID5/data/ro/version.txt"), screen("/dev/screen"),
          flags("/net/hu-jacinto/var/opt/sys"), lock("/tmp/idriver-kombi-demo.lock"),
          temporary("/tmp/idriver-kombi-demo-service-"), ifconfig("/bin/ifconfig"), arp("/bin/arp"),
          node(nbt::cluster::VideoProfile::nodeName()), stop_ms(8000), command_ms(5000) {}
};
// Paths are injectable for host tests, never selected from environment variables.
int supervise_player(const char* executable, const char* owner,
                     const SupervisorPaths& paths = SupervisorPaths());
} // namespace aa_cluster
