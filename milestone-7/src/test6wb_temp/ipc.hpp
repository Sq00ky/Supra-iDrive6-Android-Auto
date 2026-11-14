// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef NBT_TEST6WB_TEMP_IPC_HPP
#define NBT_TEST6WB_TEMP_IPC_HPP
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>
namespace nbt { namespace test6wbtemp { namespace ipc {
static const char* const Directory="/dev/shmem/nbt-aad-test6wb-temp";
static const char* const Socket="/dev/shmem/nbt-aad-test6wb-temp/control.sock";
static const char* const Lock="/dev/shmem/nbt-aad-test6wb-temp/owner.lock";
enum { MaxRequest=16384,MaxArguments=128,MaxReply=8192 };
inline uint32_t now() {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)t.tv_sec*1000u+t.tv_nsec/1000000;}
inline bool prepare(int fd) {
    int flags=fcntl(fd,F_GETFL);return flags>=0 && fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0 && fcntl(fd,F_SETFD,FD_CLOEXEC)==0;
}
inline bool wait(int fd,bool writing,uint32_t begin,unsigned budget) {
    for(;;) {
        uint32_t elapsed=now()-begin;if(elapsed>=budget)return false;
        fd_set set;FD_ZERO(&set);FD_SET(fd,&set);unsigned remain=budget-elapsed;
        struct timeval timeout;timeout.tv_sec=remain/1000;timeout.tv_usec=(remain%1000)*1000;
        int n=select(fd+1,writing?NULL:&set,writing?&set:NULL,NULL,&timeout);
        if(n>0)return true;if(n==0 || errno!=EINTR)return false;
    }
}
inline bool sendAll(int fd,const std::string& data,unsigned budget=2000) {
    size_t offset=0;uint32_t begin=now();
    while(offset<data.size()) {
        if(!wait(fd,true,begin,budget))return false;
        ssize_t n=send(fd,data.data()+offset,data.size()-offset,0);
        if(n>0)offset+=(size_t)n;else if(n==0 || (errno!=EINTR && errno!=EAGAIN && errno!=EWOULDBLOCK))return false;
    }
    return true;
}
inline bool receive(int fd,std::string& data,size_t cap,unsigned budget=2000) {
    data.clear();uint32_t begin=now();char buffer[1024];
    for(;;) {
        if(!wait(fd,false,begin,budget))return false;
        ssize_t n=recv(fd,buffer,sizeof(buffer),0);
        if(n==0)return true;
        if(n<0){if(errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK)continue;return false;}
        if(data.size()+(size_t)n>cap)return false;data.append(buffer,(size_t)n);
    }
}
inline void put(std::string& s,unsigned n) {for(unsigned i=0;i<4;++i)s+=(char)(n>>(i*8));}
inline bool get(const std::string& s,size_t& offset,unsigned& n) {
    if(offset+4>s.size())return false;n=0;
    for(unsigned i=0;i<4;++i)n|=(unsigned)(unsigned char)s[offset++]<<(i*8);return true;
}
inline bool encode(const std::vector<std::string>& args,std::string& wire) {
    if(args.empty() || args.size()>MaxArguments)return false;wire.clear();put(wire,args.size());
    for(size_t i=0;i<args.size();++i) {
        if(args[i].size()>MaxRequest || wire.size()+4+args[i].size()>MaxRequest)return false;
        put(wire,args[i].size());wire+=args[i];
    }
    return true;
}
inline bool decode(const std::string& wire,std::vector<std::string>& args) {
    args.clear();size_t offset=0;unsigned count=0;
    if(wire.size()>MaxRequest || !get(wire,offset,count) || !count || count>MaxArguments)return false;
    for(unsigned i=0;i<count;++i) {
        unsigned length=0;if(!get(wire,offset,length) || length>MaxRequest || offset+length>wire.size())return false;
        std::string arg=wire.substr(offset,length);if(arg.find('\0')!=std::string::npos)return false;
        args.push_back(arg);offset+=length;
    }
    return offset==wire.size();
}
inline bool address(struct sockaddr_un& a) {
    if(strlen(Socket)>=sizeof(a.sun_path))return false;
    memset(&a,0,sizeof(a));a.sun_family=AF_UNIX;memcpy(a.sun_path,Socket,strlen(Socket)+1);return true;
}
} } }
#endif
