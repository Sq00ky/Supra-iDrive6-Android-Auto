#include "cluster/g30/service_log.hpp"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <string>
static void check(bool ok,const char* text) {if(!ok){fprintf(stderr,"FAIL %s\n",text);exit(1);}}
static std::string contents(FILE* file) {
    fflush(file);fseek(file,0,SEEK_SET);char data[8192];size_t n=fread(data,1,sizeof(data),file);
    clearerr(file);fseek(file,0,SEEK_END);return std::string(data,n);
}
int main() {
    FILE* first=tmpfile();FILE* second=tmpfile();check(first&&second,"temporary log sinks");
    int saved=dup(STDOUT_FILENO),fds[2];check(saved>=0&&!pipe(fds),"log pipe");
    fflush(stdout);check(dup2(fileno(first),STDOUT_FILENO)>=0,"first daemon log destination");
    aa_cluster::ServiceLog logs;check(logs.start(fds[0]),"start independent log reader");
    const char begin[]="ready\nsplit ";check(write(fds[1],begin,sizeof(begin)-1)==sizeof(begin)-1,"first chunk");
    bool live=false;
    for(unsigned i=0;i<100;++i) {struct stat s;if(!fstat(fileno(first),&s)&&s.st_size) {live=true;break;}usleep(10000);}
    check(live,"logs arrive before process exit");
    check(contents(first)=="[kombi-player] ready\n","one prefix per complete line");
    // Match the daemon's RAM/USB/off redirect behavior while the child lives.
    fflush(stdout);check(dup2(fileno(second),STDOUT_FILENO)>=0,"switch daemon log destination");
    const char end[]="line\n";check(write(fds[1],end,sizeof(end)-1)==sizeof(end)-1,"fragment continuation");
    char long_line[6001];memset(long_line,'x',6000);long_line[6000]='\n';
    check(write(fds[1],long_line,sizeof(long_line))==sizeof(long_line),"long diagnostic");
    check(write(fds[1],"last partial",12)==12,"trailing partial line");close(fds[1]);logs.finish();
    std::string text=contents(second);
    check(text.find("[kombi-player] split line\n")==0,"partial line follows current destination");
    check(text.find(" [line truncated]\n")!=std::string::npos&&text.size()<2300,"bounded long line");
    check(text.find("[kombi-player] last partial\n")!=std::string::npos,"final diagnostics flushed");
    check(contents(first)=="[kombi-player] ready\n","no stale writes to old RAM log");
    // A descendant holding the write end cannot prevent bounded shutdown.
    check(!pipe(fds)&&logs.start(fds[0]),"restart reader");logs.finish();close(fds[1]);
    fflush(stdout);dup2(saved,STDOUT_FILENO);close(saved);fclose(first);fclose(second);
    puts("AA_SERVICE_LOG_OK live prefix, fragments, log switching, bounded lines, EOF and held-open shutdown");
}
