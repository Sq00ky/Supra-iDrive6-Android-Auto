// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Temporary diagnostics: one local IPC server, one worker, borrowed production transport.
#include "test6wb_temp/engine.hpp"
#include "test6wb_temp/ipc.hpp"
#include "commands/plan.hpp"
#include "logging/log.hpp"
#include <sys/stat.h>
#include <stdio.h>
#include <sstream>

namespace nbt { namespace test6wbtemp {
namespace {
using namespace hudterm;
using namespace test6wb;
const char* const LogPath="/dev/shmem/nbt-aad-test6wb.log";
struct Job {
    std::vector<Trial> trials;std::vector<std::string> requests;
    unsigned holdMs,settleMs;
    Job():holdMs(20000),settleMs(2000) {}
};
bool parse(const std::vector<std::string>& args,Job& job,std::string& error) {
    std::string variant="identity",script;bool hasScript=false,hasHold=false;
    std::vector<std::string> words;
    for(size_t i=1;i<args.size();++i) {
        const std::string& opt=args[i];
        if(opt=="--hold" || opt=="--hold-ms" || opt=="--settle-ms" || opt=="--pattern" || opt=="--script-data") {
            if(++i==args.size()){error="missing option value";return false;}
            const std::string& value=args[i];
            if(opt=="--pattern") {variant=value;if(value!="identity" && value!="bands"){error="pattern identity|bands";return false;}}
            else if(opt=="--script-data") {if(hasScript){error="one script only";return false;}script=value;hasScript=true;}
            else {
                unsigned n=0;
                if(!number(value,opt=="--hold"?120:opt=="--settle-ms"?30000:120000,n)){error="invalid time option";return false;}
                if(opt=="--settle-ms")job.settleMs=n;
                else {if(hasHold){error="one photographic duration only";return false;}hasHold=true;job.holdMs=opt=="--hold"?n*1000:n;}
            }
        } else if(opt=="--verbose") { /* Prepared payloads are always logged in this integration. */ }
        else if(opt.size()>1 && opt[0]=='-' && opt[1]=='-'){error="unsupported option";return false;}
        else words.push_back(opt);
    }
    if(hasScript) {
        if(!words.empty() || script.size()>8192){error="script cannot be combined with command, max 8192 bytes";return false;}
        std::istringstream input(script);std::string line;unsigned count=0;
        while(std::getline(input,line)) {
            if(line.size()>255){error="script line exceeds 255 bytes";return false;}
            std::istringstream tokens(line);std::string word;std::vector<std::string> command;
            while(tokens>>word){if(word[0]=='#')break;command.push_back(word);}
            if(command.empty())continue;
            if(++count>32){error="script exceeds 32 commands";return false;}
            if(!plan(command,variant,job.trials,error))return false;
            job.requests.push_back(line);
        }
        if(!count){error="empty script";return false;}
    } else {
        if(!plan(words,variant,job.trials,error))return false;
        std::string request;for(size_t i=1;i<args.size();++i){if(i>1)request+=' ';request+=args[i];}
        job.requests.push_back(request);
    }
    return true;
}

class Engine {
public:
    Engine(HudTerminal* port,most::Client& client):port_(port),client_(client),quit_(false),cancel_(false),
        busy_(false),pending_(false),active_(false),ipcStarted_(false),workerStarted_(false),
        listener_(-1),lockFd_(-1),epoch_(0),verified_(false),dirty_(false),submitted_(false),current_(0),currentTest_(0),currentHandle_(0),lastResult_(0) {
        strcpy(phase_,"idle");
        pthread_mutex_init(&mutex_,NULL);pthread_cond_init(&condition_,NULL);
    }
    bool start();void stop();
private:
    static void* ipcEntry(void* self){static_cast<Engine*>(self)->serve();return NULL;}
    static void* workerEntry(void* self){static_cast<Engine*>(self)->work();return NULL;}
    static void trace(const most::Msg* tx,const most::View* rx,HudTerminal::Test6wbPhase phase,void* user);
    void serve();void work();std::string command(const std::vector<std::string>& args);
    bool canceled();bool quitting();bool stable();void phase(const char* value,unsigned current=0,unsigned test=0,unsigned handle=0);
    bool waitDispatch(unsigned budget,bool cancelable);
    bool hold(unsigned duration);bool submit(const Batch& batch,bool cleanup=false);
    bool resources();bool execute(const Batch& batch);bool cleanup();int run(Job& job);
    void prepared(const Trial& trial);
    HudTerminal* port_;most::Client& client_;Log log_;
    pthread_mutex_t mutex_;pthread_cond_t condition_;pthread_t ipcThread_,workerThread_;
    bool quit_,cancel_,busy_,pending_,active_,ipcStarted_,workerStarted_;
    int listener_,lockFd_;uint32_t epoch_;bool verified_,dirty_,submitted_;
    unsigned current_,currentTest_,currentHandle_;int lastResult_;char phase_[32];Job job_;
};

bool Engine::start() {
    if(mkdir(ipc::Directory,0700)<0 && errno!=EEXIST)return false;
    struct stat directory;
    if(lstat(ipc::Directory,&directory)<0 || !S_ISDIR(directory.st_mode) || directory.st_uid!=geteuid() || (directory.st_mode&077))return false;
    struct stat oldLock;bool existed=lstat(ipc::Lock,&oldLock)==0;
    if(existed && (!S_ISREG(oldLock.st_mode) || oldLock.st_uid!=geteuid()))return false;
    lockFd_=open(ipc::Lock,O_CREAT|O_RDWR,0600);if(lockFd_<0)return false;
    struct stat actual;
    if(fstat(lockFd_,&actual)<0 || !S_ISREG(actual.st_mode) || actual.st_uid!=geteuid() ||
       (existed && (actual.st_dev!=oldLock.st_dev || actual.st_ino!=oldLock.st_ino))) {close(lockFd_);lockFd_=-1;return false;}
    fcntl(lockFd_,F_SETFD,FD_CLOEXEC);
    struct flock owner;memset(&owner,0,sizeof(owner));owner.l_type=F_WRLCK;owner.l_whence=SEEK_SET;
    if(fcntl(lockFd_,F_SETLK,&owner)<0){close(lockFd_);lockFd_=-1;return false;}
    struct stat endpoint;
    if(lstat(ipc::Socket,&endpoint)==0) {
        if(!S_ISSOCK(endpoint.st_mode) || endpoint.st_uid!=geteuid())return false;
        if(unlink(ipc::Socket)<0)return false;
    }
    listener_=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un address;
    if(listener_<0 || listener_>=FD_SETSIZE || !ipc::prepare(listener_) || !ipc::address(address) ||
       bind(listener_,(struct sockaddr*)&address,sizeof(address))<0 || chmod(ipc::Socket,0600)<0 || listen(listener_,4)<0)return false;
    if(pthread_create(&workerThread_,NULL,&Engine::workerEntry,this)!=0)return false;
    workerStarted_=true;
    if(port_)port_->setTest6wbTrace(&Engine::trace,this);
    if(pthread_create(&ipcThread_,NULL,&Engine::ipcEntry,this)!=0)return false;
    ipcStarted_=true;
    printf("[TEST6WB_TEMP] local IPC ready; IDLE until explicit command; existing 6WB transport %s\n",port_?"available":"unavailable (select LIVE 6wb backend)");
    return true;
}
void Engine::stop() {
    pthread_mutex_lock(&mutex_);quit_=true;cancel_=true;pthread_cond_signal(&condition_);pthread_mutex_unlock(&mutex_);
    if(ipcStarted_){pthread_join(ipcThread_,NULL);ipcStarted_=false;}
    if(workerStarted_){pthread_join(workerThread_,NULL);workerStarted_=false;}
    if(port_)port_->setTest6wbTrace(NULL,NULL);
    if(listener_>=0){close(listener_);listener_=-1;unlink(ipc::Socket);}
    if(lockFd_>=0){unlink(ipc::Lock);close(lockFd_);lockFd_=-1;rmdir(ipc::Directory);}
}
void Engine::trace(const most::Msg* tx,const most::View* rx,HudTerminal::Test6wbPhase phase,void* user) {
    Engine* e=static_cast<Engine*>(user);
    // Callback paths never write files or wait for the diagnostic worker.
    if(pthread_mutex_trylock(&e->mutex_)!=0)return;
    if(e->active_) {
        if(rx)e->log_.response(*rx);
        if(tx)e->log_.message(phase==HudTerminal::kTest6wbAttempt?"TX_ATTEMPT":phase==HudTerminal::kTest6wbLocalOk?"TX_LOCAL_OK":"TX_LOCAL_FAIL",
                             *tx,e->client_.selfAddress(),e->client_.registeredHeaderTag());
    }
    pthread_mutex_unlock(&e->mutex_);
}
void Engine::serve() {
    while(!quitting()) {
        if(!ipc::wait(listener_,false,ipc::now(),100))continue;
        int fd=accept(listener_,NULL,NULL);if(fd<0)continue;
        if(fd>=FD_SETSIZE || !ipc::prepare(fd)){close(fd);continue;}
        try {
            std::string wire,reply;std::vector<std::string> args;
            if(!ipc::receive(fd,wire,ipc::MaxRequest) || !ipc::decode(wire,args))reply="ERROR malformed/timed-out local request\n";
            else reply=command(args);
            ipc::sendAll(fd,reply);
        } catch(...) {
            const char error[]="ERROR temporary diagnostic request failed; check status\n";
            send(fd,error,sizeof(error)-1,0);
        }
        shutdown(fd,SHUT_WR);close(fd);
    }
}
std::string Engine::command(const std::vector<std::string>& args) {
    if(args.size()==1 && args[0]=="list") {
        size_t count=0;const TestDefinition* tests=catalog(count);std::string result="OK stable catalog\n";char line[180];
        for(size_t i=0;i<count;++i){snprintf(line,sizeof(line),"%03u 0x%02X %s\n",tests[i].id,tests[i].handle,tests[i].description);result+=line;}
        return result;
    }
    pthread_mutex_lock(&mutex_);
    if(args.size()==1 && args[0]=="status") {
        char result[300];snprintf(result,sizeof(result),"OK available=%u transport=%s busy=%u stop_requested=%u trial=R%03u test=%03u handle=0x%02X phase=%s last_exit=%d log=%s\n",
            port_?1:0,port_?statusName(port_->status()):"unavailable",busy_?1:0,cancel_?1:0,current_,currentTest_,currentHandle_,phase_,lastResult_,LogPath);
        pthread_mutex_unlock(&mutex_);return result;
    }
    if(args.size()==1 && args[0]=="stop") {
        if(busy_)cancel_=true;
        pthread_mutex_unlock(&mutex_);return "OK stop requested; queued work may finish before cleanup; poll status\n";
    }
    if(args.empty() || args[0]!="start"){pthread_mutex_unlock(&mutex_);return "ERROR use list, status, stop or start COMMAND\n";}
    if(quit_ || busy_){pthread_mutex_unlock(&mutex_);return "ERROR another diagnostic job is active or shutting down\n";}
    if(!port_){pthread_mutex_unlock(&mutex_);return "ERROR existing LIVE 6WB backend unavailable; no new MOST connection will be opened\n";}
    pthread_mutex_unlock(&mutex_);
    Job next;std::string error;if(!parse(args,next,error))return "ERROR "+error+"\n";
    pthread_mutex_lock(&mutex_);
    if(quit_ || busy_){pthread_mutex_unlock(&mutex_);return "ERROR diagnostic worker busy/shutting down\n";}
    job_.trials.swap(next.trials);job_.requests.swap(next.requests);job_.holdMs=next.holdMs;job_.settleMs=next.settleMs;
    busy_=true;pending_=true;cancel_=false;strcpy(phase_,"queued");
    pthread_cond_signal(&condition_);pthread_mutex_unlock(&mutex_);
    return "OK accepted; use status/stop; dispatch does not prove physical display\n";
}
bool Engine::canceled() {
    pthread_mutex_lock(&mutex_);bool value=quit_ || cancel_;pthread_mutex_unlock(&mutex_);return value;
}
bool Engine::quitting() {
    pthread_mutex_lock(&mutex_);bool value=quit_;pthread_mutex_unlock(&mutex_);return value;
}
void Engine::phase(const char* value,unsigned current,unsigned test,unsigned handle) {
    pthread_mutex_lock(&mutex_);strncpy(phase_,value,sizeof(phase_)-1);phase_[sizeof(phase_)-1]=0;
    if(current){current_=current;currentTest_=test;currentHandle_=handle;}pthread_mutex_unlock(&mutex_);
}
bool Engine::stable() {return port_ && port_->status()==kReady && port_->epoch()==epoch_;}
bool Engine::waitDispatch(unsigned budget,bool cancelable) {
    uint32_t begin=monotonicMs();
    do {
        log_.drain();if(!stable() || (cancelable && (canceled() || !log_.healthy())))return false;
        if(port_->waitIdle(0))return true;
        struct timespec pause={0,10*1000000};nanosleep(&pause,NULL);
    } while(monotonicMs()-begin<budget);
    log_.write("TIMEOUT","local dispatch/mandatory C04 result budget=%u",budget);return false;
}
bool Engine::hold(unsigned duration) {
    uint32_t begin=monotonicMs();
    while(monotonicMs()-begin<duration) {
        log_.drain();if(canceled() || !log_.healthy() || !stable())return false;
        struct timespec pause={0,10*1000000};nanosleep(&pause,NULL);
    }
    return !canceled() && log_.healthy() && stable();
}
bool Engine::submit(const Batch& batch,bool cleanupMode) {
    std::string error;
    if(!stable() || (!cleanupMode && (canceled() || !log_.healthy())) || !validate(batch,error)) {
        log_.write("ERROR","batch withheld %s: %s",batch.label.c_str(),error.c_str());return false;
    }
    SubmitResult result=port_->submit(batch);
    log_.write("QUEUE","label=%s calls=%u result=%s",batch.label.c_str(),(unsigned)batch.calls.size(),submitResultName(result));
    if(result==kQueued || result==kReplaced || result==kMerged)submitted_=true;
    return result==kQueued;
}
bool Engine::resources() {
    if(!port_ || port_->status()!=kReady){log_.write("ERROR","existing terminal not transport-ready");return false;}
    epoch_=port_->epoch();if(!waitDispatch(5000,true))return false;
    Batch missing("test6wb-temp-size-check");
    for(size_t i=0;i<surfaceCount;++i)if(surfaces[i].kind!=Text) {
        uint16_t w=0,h=0;const Surface& s=surfaces[i];
        if(port_->imageSize(s.handle,w,h)) {
            if(w!=s.width || h!=s.height){log_.write("ERROR","cached size mismatch handle=%02X %ux%u",s.handle,w,h);return false;}
        } else {std::vector<uint8_t> p=getSize(s.handle);p.push_back(1);missing.add(kGetSize,p);}
    }
    if(!missing.calls.empty()) {
        log_.write("RESOURCE_QUERY","missing=%u; reuse existing registration/cache, no retry",(unsigned)missing.calls.size());
        if(!submit(missing) || !waitDispatch(42000,true))return false;
    }
    for(size_t i=0;i<surfaceCount;++i)if(surfaces[i].kind!=Text) {
        uint16_t w=0,h=0;const Surface& s=surfaces[i];
        if(!port_->imageSize(s.handle,w,h) || w!=s.width || h!=s.height){log_.write("ERROR","resource mismatch handle=%02X received=%ux%u",s.handle,w,h);return false;}
        log_.write("RESOURCE_VERIFIED","handle=%02X nominal=%ux%u",s.handle,w,h);
    }
    verified_=stable();return verified_;
}
bool Engine::execute(const Batch& batch) {
    if(!verified_ || !stable() || canceled())return false;dirty_=true;
    if(!submit(batch) || !waitDispatch(5000,true))return false;
    if(batch.label=="clear")dirty_=false;
    log_.write("DISPATCH_COMPLETE","%s; graphic ACKs asynchronous; physical_display=UNVERIFIED",batch.label.c_str());return true;
}
bool Engine::cleanup() {
    phase("cleanup");log_.beginCleanup();
    if(!submitted_){log_.write("CLEANUP","no diagnostic batch submitted");return true;}
    // Drain already submitted queries/drawing without canceling the shared production Core.
    if(!waitDispatch(42000,false)) {log_.write("CLEANUP_UNCONFIRMED","shared transport invalidated or still busy; no reset");return false;}
    if(!dirty_)return true;
    if(!verified_ || !stable() || !submit(clearBatch(),true) || !waitDispatch(5000,false)) {
        log_.write("CLEANUP_UNCONFIRMED","no forced writes to unverified resources");return false;
    }
    dirty_=false;log_.write("CLEANUP_DISPATCHED","no OEM restore, physical cleanup unverified");
    uint32_t begin=monotonicMs();
    while(monotonicMs()-begin<1000 && stable()) {log_.drain();struct timespec pause={0,10*1000000};nanosleep(&pause,NULL);}
    return stable();
}
void Engine::prepared(const Trial& t) {
    log_.setTrial(t.id,t.testId);const Surface* s=surface(t.handle);
    log_.write("TRIAL_PLAN","handle=%02X nominal=%ux%u mode=%s params=%s",t.handle,s?s->width:0,s?s->height:0,t.description.c_str(),t.parameters.c_str());
    for(size_t i=0;i<t.batch.calls.size() && log_.healthy();++i) {
        const Call& c=t.batch.calls[i];log_.write("GENERATED","call=%u fkt=%03X h=%02X bytes=%u tx=%u",(unsigned)i,c.fkt,c.handle(),(unsigned)c.payload.size(),c.txType<0?4:(unsigned)c.txType);
        for(size_t offset=0;offset<c.payload.size() && log_.healthy();offset+=32) {
            char hex[97];size_t count=c.payload.size()-offset;if(count>32)count=32;
            for(size_t k=0;k<count;++k)snprintf(hex+3*k,4,"%02x ",c.payload[offset+k]);hex[3*count]=0;
            log_.write("PAYLOAD","call=%u offset=%u hex=%s",(unsigned)i,(unsigned)offset,hex);
        }
    }
}
int Engine::run(Job& job) {
    verified_=false;dirty_=false;submitted_=false;
    if(!log_.open(LogPath)){fprintf(stderr,"[TEST6WB_TEMP] cannot open diagnostic log\n");return 2;}
    pthread_mutex_lock(&mutex_);active_=true;pthread_mutex_unlock(&mutex_);
    log_.write("MODE","TEMP integrated; existing DSI/MOST/HudTerminal only; manual navigation responsibility; physical display UNVERIFIED");
    log_.write("JOB_OPTIONS","hold_ms=%u settle_ms=%u trials=%u",job.holdMs,job.settleMs,(unsigned)job.trials.size());
    for(size_t i=0;i<job.requests.size();++i)log_.write("REQUEST","%s",job.requests[i].c_str());
    for(size_t i=0;i<job.trials.size() && log_.healthy() && !canceled();++i)prepared(job.trials[i]);
    log_.setTrial(0);phase("resource validation");
    TerminalStats before=port_->stats();bool ok=log_.healthy() && !canceled() && resources();
    for(size_t i=0;ok && i<job.trials.size();++i) {
        const Trial& t=job.trials[i];phase("test",t.id,t.testId,t.handle);log_.setTrial(t.id,t.testId);
        log_.write("TRIAL_BEGIN","handle=%02X test=%03u description=%s params=%s",t.handle,t.testId,t.description.c_str(),t.parameters.c_str());
        if(t.isolate){ok=execute(clearBatch());if(ok)ok=hold(job.settleMs);if(!ok)break;}
        ok=execute(t.batch);if(ok)ok=hold(job.settleMs);
        if(ok){log_.write("PHOTO_BEGIN","test=%03u h=%02X duration_ms=%u",t.testId,t.handle,job.holdMs);uint32_t begin=monotonicMs();
            ok=hold(t.clear?1000:job.holdMs);log_.write("PHOTO_END","elapsed_ms=%u complete=%u",monotonicMs()-begin,ok?1:0);}
    }
    if(canceled())log_.write("MANUAL_STOP","stop/shutdown requested; no new patterns");
    bool clean=cleanup();TerminalStats after=port_->stats();
    log_.write("SHARED_STATS","observed delta errors=%u timeouts=%u graphics_sent=%u ack=%u unconfirmed=%u ambiguous=%u; not exclusive attribution",
        after.errors-before.errors,after.timeouts-before.timeouts,after.graphicsSent-before.graphicsSent,
        after.graphicsAnswered-before.graphicsAnswered,after.graphicsUnconfirmed-before.graphicsUnconfirmed,after.graphicsAmbiguous-before.graphicsAmbiguous);
    int code=canceled()?130:!ok?3:(!clean || after.errors!=before.errors || after.timeouts!=before.timeouts)?4:0;
    log_.write("SESSION_END","job exit=%d; shared DSI and sender remain owned by NBT-AAD",code);
    pthread_mutex_lock(&mutex_);active_=false;pthread_mutex_unlock(&mutex_);
    if(!log_.finish(code) && !code)code=4;return code;
}
void Engine::work() {
    for(;;) {
        pthread_mutex_lock(&mutex_);
        while(!pending_ && !quit_)pthread_cond_wait(&condition_,&mutex_);
        if(quit_ && !pending_){pthread_mutex_unlock(&mutex_);break;}
        Job current;current.trials.swap(job_.trials);current.requests.swap(job_.requests);
        current.holdMs=job_.holdMs;current.settleMs=job_.settleMs;pending_=false;pthread_mutex_unlock(&mutex_);
        int result=3;
        try {result=run(current);} catch(...) {
            log_.beginCleanup();log_.write("FATAL","temporary diagnostic exception; stop job");
            try {cleanup();} catch(...) {log_.write("CLEANUP_UNCONFIRMED","exception during cleanup");}
            pthread_mutex_lock(&mutex_);active_=false;pthread_mutex_unlock(&mutex_);log_.finish(3);
        }
        pthread_mutex_lock(&mutex_);lastResult_=result;busy_=false;cancel_=false;strcpy(phase_,"idle");current_=currentTest_=currentHandle_=0;bool quitting=quit_;pthread_mutex_unlock(&mutex_);
        if(quitting)break;
    }
}
Engine* instance=NULL;
} // namespace
bool start(hudterm::HudTerminal* terminal,most::Client& client) {
    if(instance)return false;
    try {
        instance=new Engine(terminal,client);
        if(instance->start())return true;
    } catch(...) {fprintf(stderr,"[TEST6WB_TEMP] startup exception; normal daemon continues\n");}
    if(instance)instance->stop();
    fprintf(stderr,"[TEST6WB_TEMP] local IPC disabled: startup failed\n");return false;
}
void stop() {if(instance)instance->stop();}
} }
