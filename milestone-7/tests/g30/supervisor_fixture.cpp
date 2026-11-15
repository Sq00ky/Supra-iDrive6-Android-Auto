// Host-only filesystem/process fixture for the production native supervisor.
#include "cluster/g30/supervisor.hpp"
#include <sys/utsname.h>
#include <string>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    std::string root=argv[1];aa_cluster::SupervisorPaths p;struct utsname u;
    if(uname(&u))return 2;
    p.node=u.nodename;p.version=root+"/version";p.screen=root+"/screen";
    p.flags=root+"/flags";p.lock=root+"/lease.lock";p.temporary=root+"/lease-";
    p.ifconfig=root+"/bin/ifconfig";p.arp=root+"/bin/arp";
    p.stop_ms=200;p.command_ms=1500;
    return aa_cluster::supervise_player((root+"/kombi-player").c_str(),argv[2],p);
}
