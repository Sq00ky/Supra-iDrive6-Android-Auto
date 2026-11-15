// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Local Unix-socket client only: no MOST/DSI objects, registration or vendor linkage.
#include "ipc.hpp"
#include <stdio.h>
#include <signal.h>
using namespace nbt::test6wbtemp;
int main(int argc,char** argv) {
    if(argc<2 || (argc==2 && !strcmp(argv[1],"--help"))) {
        puts("test6wbctl list | status | stop | start COMMAND [ARGS] [--ready-timeout SECONDS] [--hold SECONDS] [--settle-ms N] [--pattern identity|bands]\n"
             "test6wbctl start --script FILE [OPTIONS]\nLocal temporary NBT-AAD diagnostics; navigation is managed MANUALLY by the user.");
        return argc<2?2:0;
    }
    signal(SIGPIPE,SIG_IGN);std::vector<std::string> args;
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--script")) {
            if(++i==argc){fputs("Missing script file\n",stderr);return 2;}
            FILE* f=fopen(argv[i],"r");if(!f){perror("script");return 2;}
            std::string text;char buffer[1024];size_t n;
            while((n=fread(buffer,1,sizeof(buffer),f))!=0) {text.append(buffer,n);if(text.size()>8192)break;}
            bool ok=!ferror(f) && text.size()<=8192;fclose(f);
            if(!ok){fputs("Script read failed or exceeds 8192 bytes\n",stderr);return 2;}
            args.push_back("--script-data");args.push_back(text);
        } else args.push_back(argv[i]);
    }
    std::string wire;if(!ipc::encode(args,wire)){fputs("Request too large\n",stderr);return 2;}
    int fd=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un a;
    if(fd<0 || fd>=FD_SETSIZE || !ipc::prepare(fd) || !ipc::address(a)) {
        if(fd>=0)close(fd);fputs("Cannot create local IPC client\n",stderr);return 3;
    }
    int result=connect(fd,(struct sockaddr*)&a,sizeof(a));
    if(result<0 && errno==EINPROGRESS) {
        int error=0;socklen_t length=sizeof(error);
        if(ipc::wait(fd,true,ipc::now(),2000) && getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)==0 && !error)result=0;
    }
    if(result<0) {close(fd);fputs("Local diagnostics unavailable: verify enabled build and running NBT-AAD\n",stderr);return 3;}
    if(!ipc::sendAll(fd,wire)){close(fd);fputs("IPC send failed\n",stderr);return 3;}
    shutdown(fd,SHUT_WR);std::string reply;
    bool ok=ipc::receive(fd,reply,ipc::MaxReply,10000);close(fd);
    if(!ok){fputs("IPC reply timeout/failure; use status before resubmitting\n",stderr);return 3;}
    fwrite(reply.data(),1,reply.size(),stdout);
    return reply.compare(0,2,"OK")==0?0:4;
}
