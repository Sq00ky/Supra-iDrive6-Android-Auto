// Native owner of the player's process, temporary producer address and lease.
// Commands use argv directly; no shell or installed supervisor script is used.
#include "supervisor.hpp"
#include "process.hpp"
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <poll.h>
#include <glob.h>
#include <time.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

namespace aa_cluster { namespace {
volatile sig_atomic_t stopping=0;
void stop_signal(int) {stopping=1;}
uint32_t now() {timespec t={};clock_gettime(CLOCK_MONOTONIC,&t);return uint32_t(t.tv_sec)*1000+t.tv_nsec/1000000;}
bool alive(pid_t p) {return p>1&&(kill(p,0)==0||errno==EPERM);}
int code(int status) {return WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);}
bool exists(const std::string& path) {struct stat s;return lstat(path.c_str(),&s)==0;}
bool read_file(const std::string& path,std::string& out) {
    out.clear();FILE* f=fopen(path.c_str(),"r");if(!f)return false;
    char b[1024];size_t n;bool ok=true;
    while((n=fread(b,1,sizeof(b),f))) {out.append(b,n);if(out.size()>65536) {ok=false;break;}}
    ok=ok&&!ferror(f);fclose(f);return ok;
}
bool line(const std::string& text,const std::string& wanted) {
    return ("\n"+text+"\n").find("\n"+wanted+"\n")!=std::string::npos;
}
std::string field(const std::string& text,const char* key) {
    std::string all="\n"+text+"\n",prefix="\n"+std::string(key);
    size_t start=all.find(prefix);if(start==std::string::npos)return "";
    start+=prefix.size();return all.substr(start,all.find('\n',start)-start);
}
bool digits(const std::string& s) {return !s.empty()&&s.find_first_not_of("0123456789")==std::string::npos;}
bool unique_file(const std::string& pattern,std::string& content) {
    glob_t g={};int rc=glob(pattern.c_str(),GLOB_NOSORT,0,&g);
    bool ok=!rc&&g.gl_pathc==1&&read_file(g.gl_pathv[0],content);globfree(&g);return ok;
}
bool inet_address(const std::string& text,const char* address) {
    std::string token="inet "+std::string(address);
    for(size_t p=text.find(token);p!=std::string::npos;p=text.find(token,p+1)) {
        size_t end=p+token.size();
        if((p==0||text[p-1]==' '||text[p-1]=='\t'||text[p-1]=='\n')&&
           (end==text.size()||text[end]==' '||text[end]=='\t'||text[end]=='\n'))return true;
    }return false;
}
bool mac_address(const std::string& text) {
    for(size_t p=0;p<text.size();++p) {
        unsigned a,b,c,d,e,f;
        if(sscanf(text.c_str()+p,"%2x:%2x:%2x:%2x:%2x:%2x",&a,&b,&c,&d,&e,&f)==6)return true;
    }return false;
}
int capture(const char* path,const char* const args[],std::string& output,unsigned timeout) {
    output.clear();int pipefd[2];if(pipe(pipefd))return -1;
    fcntl(pipefd[0],F_SETFD,FD_CLOEXEC);fcntl(pipefd[1],F_SETFD,FD_CLOEXEC);
    fcntl(pipefd[0],F_SETFL,O_NONBLOCK);
    int input=open("/dev/null",O_RDONLY);if(input<0) {close(pipefd[0]);close(pipefd[1]);return -1;}
    pid_t pid=spawn_mapped(path,const_cast<char* const*>(args),input,pipefd[1],pipefd[1]);
    close(input);close(pipefd[1]);if(pid<0) {close(pipefd[0]);return -1;}
    uint32_t began=now();int status=0;bool done=false,ok=true;
    while(!done&&uint32_t(now()-began)<timeout) {
        char b[2048];ssize_t n;
        while((n=read(pipefd[0],b,sizeof(b)))>0) {
            output.append(b,n);if(output.size()>65536) {ok=false;break;}
        }
        if(!ok)break;
        pid_t result=waitpid(pid,&status,WNOHANG);
        if(result==pid)done=true;
        else if(result<0&&errno!=EINTR) {ok=false;break;}
        else {pollfd p={pipefd[0],POLLIN,0};poll(&p,1,20);}
    }
    if(!done) {kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}ok=false;}
    if(done) {char b[2048];ssize_t n;while((n=read(pipefd[0],b,sizeof(b)))>0) {
        output.append(b,n);if(output.size()>65536) {ok=false;break;}
    }}
    close(pipefd[0]);return ok?code(status):-1;
}
class Supervisor {
    const SupervisorPaths& p;std::string self,dir,lease;pid_t owner,child;
    bool locked,alias;
    int command(const std::string& exe,const char* a,const char* b,std::string& text) {
        const char* args[]={exe.c_str(),a,b,0};return capture(exe.c_str(),args,text,p.command_ms);
    }
    int alias_command(bool add) {
        const char* create[]={p.ifconfig.c_str(),"vlan73","inet","160.48.199.120","netmask","255.255.255.255","alias",0};
        const char* remove[]={p.ifconfig.c_str(),"vlan73","inet","160.48.199.120","-alias",0};
        std::string text;int rc=capture(p.ifconfig.c_str(),add?create:remove,text,p.command_ms);
        if(rc)printf("AA_CLUSTER_ALIAS_FAILED add=%u status=%d %s\n",unsigned(add),rc,text.c_str());
        return rc;
    }
    bool managers() {
        for(unsigned i=1;i<=2;++i) {
            char name[32];snprintf(name,sizeof(name),"/*/dpy-%u",i);std::string text;
            if(!unique_file(p.screen+name,text))return false;
            std::string manager=field(text,"window manager = ");
            if(!digits(manager)||strtoul(manager.c_str(),0,10)==0)return false;
        }return true;
    }
    bool output_free() {
        std::string text;if(!unique_file(p.screen+"/*/dpy-2",text))return false;
        if(line(text,"scene = (empty)")&&line(text,"windows = (none)"))return true;
        std::string win=field(text,"windows = ");
        if(win.substr(0,4)!="win-"||!digits(win.substr(4))||!line(text,"scene = "+win))return false;
        if(!unique_file(p.screen+"/*/"+win+"/"+win,text))return false;
        if(!line(text,"id string = 1056")||!line(text,"display id = 2")||
           !line(text,"buffer size = 800x480")||!line(text,"format = SCREEN_FORMAT_RGBA8888")||
           !line(text,"destination rectangle = (0,0 800x480)"))return false;
        puts("FACTORY_WINDOW_RETAINED id=1056");return true;
    }
    bool lease_owned() {
        std::string current;return locked&&read_file(p.lock+"/owner",current)&&current==lease;
    }
    bool cleanup() {
        // The child has exited/reaped before its producer address is released.
        bool ok=true;
        if(alias) {
            if(alias_command(false))ok=false;
            std::string text;
            if(command(p.ifconfig,"vlan73",0,text)||inet_address(text,"160.48.199.120"))ok=false;
            if(ok)alias=false;
        }
        if(locked) {
            if(!lease_owned())ok=false;
            if(ok) {
                if(rmdir(dir.c_str())||unlink((p.lock+"/owner").c_str())||rmdir(p.lock.c_str()))ok=false;
            }
        }
        printf("AA_CLUSTER_SERVICE_CLEANUP alias_removed=%u\n",unsigned(ok));fflush(0);return ok;
    }
    int finish(int rc) {return cleanup()?rc:96;}
public:
    Supervisor(const SupervisorPaths& paths,const char* executable,pid_t parent):
        p(paths),self(executable),owner(parent),child(-1),locked(false),alias(false) {}
    int run() {
        struct utsname u;std::string text;
        if(uname(&u)||p.node!=u.nodename||!alive(owner)||access(self.c_str(),X_OK))return 90;
        // Version is a diagnostic, not an exact-firmware allowlist.
        if(read_file(p.version,text))printf("AA_CLUSTER_FIRMWARE %s\n",text.c_str());
        else puts("AA_CLUSTER_FIRMWARE unavailable");
        struct stat flags;
        if(stat(p.flags.c_str(),&flags)||!S_ISDIR(flags.st_mode)||access(p.flags.c_str(),R_OK|X_OK)||
           exists(p.flags+"/APIX_DRIVER_OFF")||exists(p.flags+"/APIX_DFE_1920"))return 90;
        if(command(p.ifconfig,"vlan73",0,text)||!inet_address(text,"160.48.199.119"))return 90;
        if(mkdir(p.lock.c_str(),0700))return 91;
        char suffix[64];snprintf(suffix,sizeof(suffix),"%ld-%ld",long(owner),long(getpid()));
        dir=p.temporary+suffix;lease=dir+"\n";
        if(mkdir(dir.c_str(),0700)) {rmdir(p.lock.c_str());return 91;}
        int fd=open((p.lock+"/owner").c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        bool saved=fd>=0&&write(fd,lease.data(),lease.size())==ssize_t(lease.size());
        if(fd>=0)close(fd);
        if(!saved) {unlink((p.lock+"/owner").c_str());rmdir(dir.c_str());rmdir(p.lock.c_str());return 91;}
        locked=true;
        if(command(p.ifconfig,"-a",0,text)||inet_address(text,"160.48.199.120")) {
            puts("REFUSE_EXISTING_ALIAS_OR_QUERY_FAILED");return finish(92);
        }
        // A missing ARP entry may return nonzero. Tool failure still fails closed.
        int arp=command(p.arp,"-n","160.48.199.120",text);
        if(arp<0||mac_address(text)) {puts("REFUSE_ALIAS_NEIGHBOR");return finish(93);}
        if(!managers()) {puts("REFUSE_MISSING_FACTORY_DISPLAY_MANAGER");return finish(98);}
        int inspected=command(self,"--inspect",0,text);printf("%s",text.c_str());
        if(inspected)return finish(94);
        if(!managers()||!output_free()) {puts("REFUSE_UNKNOWN_SECONDARY_WINDOW");return finish(97);}
        if(stopping||!alive(owner))return finish(130);
        // Recheck immediately before acquiring the address after slow inspection.
        if(command(p.ifconfig,"-a",0,text)||inet_address(text,"160.48.199.120"))return finish(92);
        if(alias_command(true))return finish(95);
        alias=true;
        int input=open("/dev/null",O_RDONLY);if(input<0)return finish(95);
        char* args[]={const_cast<char*>(self.c_str()),const_cast<char*>("--service"),0};
        // Both streams flow through the daemon's prefixed log relay. No second
        // file is created, and successful sessions are logged live as well.
        child=spawn_mapped(self.c_str(),args,input,STDOUT_FILENO,STDERR_FILENO);close(input);
        if(child<0)return finish(95);
        printf("AA_CLUSTER_SUPERVISOR_READY owner=%ld player=%ld\n",long(owner),long(child));fflush(0);
        bool terminating=false,forced=false;uint32_t since=0;int status=0;
        for(;;) {
            pid_t result=waitpid(child,&status,WNOHANG);
            if(result==child)break;
            if(result<0&&errno!=EINTR)return finish(96);
            if(!terminating&&(stopping||!alive(owner))) {
                kill(child,SIGTERM);terminating=true;since=now();
            }
            if(terminating&&!forced&&uint32_t(now()-since)>=p.stop_ms) {
                kill(child,SIGKILL);forced=true;
            }
            usleep(20000);
        }
        child=-1;int rc=code(status);
        if(rc) {
            printf("AA_CLUSTER_PLAYER_FAILED status=%d\n",rc);
        }
        return finish(rc);
    }
};
}
int supervise_player(const char* executable,const char* parent,const SupervisorPaths& paths) {
    if(!executable||executable[0]!='/'||!parent||!digits(parent))return 90;
    setvbuf(stdout,0,_IOLBF,0);
    errno=0;long owner=strtol(parent,0,10);if(errno||owner<=1||owner>INT_MAX)return 90;
    stopping=0;signal(SIGHUP,stop_signal);signal(SIGINT,stop_signal);signal(SIGTERM,stop_signal);
    signal(SIGPIPE,SIG_IGN);umask(077);
    // Same loader paths as the previous installed helper, inherited by children.
    setenv("LD_LIBRARY_PATH","/opt/mm/lib:/lib:/usr/lib:/opt/sys/lib:/opt/car/lib",1);
    return Supervisor(paths,executable,pid_t(owner)).run();
}
}
