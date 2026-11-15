#pragma once
#include <string>

namespace aa_cluster {
struct SupervisorPaths {
    std::string version,screen,flags,lock,temporary,ifconfig,arp,node;
    unsigned stop_ms,command_ms;
    SupervisorPaths():version("/opt/hmi/ID5/data/ro/version.txt"),screen("/dev/screen"),
        flags("/net/hu-jacinto/var/opt/sys"),lock("/tmp/idriver-kombi-demo.lock"),
        temporary("/tmp/idriver-kombi-demo-service-"),ifconfig("/bin/ifconfig"),
        arp("/bin/arp"),node("hu-omap"),stop_ms(8000),command_ms(5000) {}
};
// Paths are injectable for host tests, never selected from environment variables.
int supervise_player(const char* executable,const char* owner,const SupervisorPaths& paths=SupervisorPaths());
}
