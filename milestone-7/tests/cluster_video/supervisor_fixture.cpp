// Host-only filesystem/process fixture for the production native supervisor.
#include "cluster/video/supervisor.hpp"
#include <sys/utsname.h>
#include <string>
int main(int argc, char **argv) {
    if (argc != 3) {
        return 2;
    }
    std::string root = argv[1];
    aa_cluster::SupervisorPaths paths;
    struct utsname system_info;
    if (uname(&system_info)) {
        return 2;
    }
    paths.node = system_info.nodename;
    paths.version = root + "/version";
    paths.screen = root + "/screen";
    paths.flags = root + "/flags";
    paths.lock = root + "/lease.lock";
    paths.temporary = root + "/lease-";
    paths.ifconfig = root + "/bin/ifconfig";
    paths.arp = root + "/bin/arp";
    paths.stop_ms = 200;
    paths.command_ms = 1500;
    return aa_cluster::supervise_player((root + "/kombi-player").c_str(), argv[2], paths);
}
