// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_NAV_DIAG_HPP
#define NBT_NAV_DIAG_HPP
// Optional ch10 observer. No dependency on MOST, the renderer or NavMessages.
// Producer: one AASession RX thread; session boundaries serialized by lifecycle join.
// All parsing, formatting, file I/O and rotation belong to the exporter thread.
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include "nav_diag_trace.hpp"
namespace nbt { namespace navdiag {
enum { Capacity=32, MaxBody=8192, MaxFields=512, MaxDepth=8, MaxText=256 };
inline uint64_t stamp(){timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
inline bool flag(const char* name){const char* p=getenv(name);return p && !strcmp(p,"1");}
struct Config {
    bool enabled,text;const char* path;const char* source;size_t maxTotalBytes;
    Config():enabled(false),text(false),path("/opt/aa/logs/nbt-aad-nav-diag.jsonl"),source("unspecified"),maxTotalBytes(15u*1024u*1024u){}
};
inline bool sourceLabel(const char* s){return s && (!strcmp(s,"maps")||!strcmp(s,"waze")||!strcmp(s,"amigo")||!strcmp(s,"tomtom_amigo"));}
inline Config environment(){Config c;c.enabled=flag("NBT_NAV_DIAG");c.text=flag("NBT_NAV_DIAG_TEXT");
    const char* s=getenv("NBT_NAV_DIAG_SOURCE");if(sourceLabel(s))c.source=s;return c;}
struct Stats {uint64_t seen,captured,lost,truncated;Stats():seen(0),captured(0),lost(0),truncated(0){} };
enum Node { Unknown,Status,State,Step,Maneuver,Road,Lane,Direction,Cue,Destination,Position,StepDistance,Distance,DestinationDistance,LegacyTurn,LegacyDistance };
enum Value { Nested,Integer,Boolean,Text,Display,TimeText };
struct Field {const char* name;Value value;Node child;bool repeated;Field():name(NULL),value(Integer),child(Unknown),repeated(false){}
    Field(const char* n,Value v,Node c=Unknown,bool r=false):name(n),value(v),child(c),repeated(r){} };
inline Field describe(Node n,unsigned f){
    switch(n){
    case Status:if(f==1)return Field("status",Integer);break;
    case State:if(f==1)return Field("steps",Nested,Step,true);if(f==2)return Field("destinations",Nested,Destination,true);break;
    case Step:if(f==1)return Field("maneuver",Nested,Maneuver);if(f==2)return Field("road",Nested,Road);if(f==3)return Field("lanes",Nested,Lane,true);if(f==4)return Field("cue",Nested,Cue);break;
    case Maneuver:if(f==1)return Field("type",Integer);if(f==2)return Field("exit_number",Integer);if(f==3)return Field("exit_angle",Integer);break;
    case Road:if(f==1)return Field("name",Text);break;
    case Lane:if(f==1)return Field("direction",Nested,Direction,true);break;
    case Direction:if(f==1)return Field("shape",Integer);if(f==2)return Field("is_highlighted",Boolean);break;
    case Cue:if(f==1)return Field("text",Text,Unknown,true);break;
    case Destination:if(f==1)return Field("address",Text);break;
    case Position:if(f==1)return Field("step_distance",Nested,StepDistance);if(f==2)return Field("destination_distances",Nested,DestinationDistance,true);if(f==3)return Field("current_road",Nested,Road);break;
    case StepDistance:if(f==1)return Field("distance",Nested,Distance);if(f==2)return Field("seconds",Integer);break;
    case Distance:if(f==1)return Field("meters",Integer);if(f==2)return Field("display_value",Display);if(f==3)return Field("display_units",Integer);break;
    case DestinationDistance:if(f==1)return Field("distance",Nested,Distance);if(f==2)return Field("eta",TimeText);if(f==3)return Field("seconds",Integer);break;
    case LegacyTurn:if(f==1)return Field("road",Text);if(f==2)return Field("turn_side",Integer);if(f==3)return Field("event",Integer);if(f==5)return Field("exit_number",Integer);if(f==6)return Field("exit_angle",Integer);break;
    case LegacyDistance:if(f==1)return Field("distance_m",Integer);if(f==2)return Field("time_s",Integer);if(f==3)return Field("display_distance_e3",Integer);if(f==4)return Field("display_distance_unit",Integer);break;
    default:break;
    }return Field();
}
inline Node schema(uint16_t id){switch(id){case 0x8003:return Status;case 0x8006:return State;case 0x8007:return Position;case 0x8004:return LegacyTurn;case 0x8005:return LegacyDistance;default:return Unknown;}}
inline std::string quote(const std::string& value){std::string s="\"";const char* hex="0123456789abcdef";
    for(size_t i=0;i<value.size();++i){unsigned char c=(unsigned char)value[i];if(c=='"'||c=='\\'){s+='\\';s+=(char)c;}
        else if(c<32){s+="\\u00";s+=hex[c>>4];s+=hex[c&15];}else s+=(char)c;}s+='"';return s;}
inline bool utf8(const uint8_t* p,size_t n){
    for(size_t i=0;i<n;){unsigned c=p[i++],more=0,min=0,cp=c;
        if(c<128)continue;
        if(c>=0xc2 && c<=0xdf){more=1;min=0x80;cp=c&31;}
        else if(c>=0xe0 && c<=0xef){more=2;min=0x800;cp=c&15;}
        else if(c>=0xf0 && c<=0xf4){more=3;min=0x10000;cp=c&7;}else return false;
        if(more>n-i)return false;
        while(more--){unsigned b=p[i++];if((b&0xc0)!=0x80)return false;cp=(cp<<6)|(b&63);}
        if(cp<min || cp>0x10ffff || (cp>=0xd800&&cp<=0xdfff))return false;
    }return true;
}
// Default-safe display strings contain only numbers/punctuation. ETA additionally
// permits AM/PM. Other known text needs explicit NBT_NAV_DIAG_TEXT=1.
inline bool safeText(const uint8_t* p,size_t n,Value v){
    if(!n||n>32 || (v!=Display&&v!=TimeText))return false;
    bool digit=false;
    for(size_t i=0;i<n;++i){unsigned c=p[i];if(c>='0'&&c<='9'){digit=true;continue;}
        if(c==' '||c=='.'||c==','||c==':'||c=='+'||c=='-')continue;
        if(v==TimeText&&(c=='a'||c=='p'||c=='m'||c=='A'||c=='P'||c=='M'))continue;return false;}
    return digit;
}
class Decoder {
public:
    explicit Decoder(bool text):text_(text),count_(0),first_(true),bad_(false),limited_(false){}
    std::string fields(uint16_t id,const uint8_t* p,size_t n){if(!p && n)error("",0,"null_body");else walk(schema(id),p,n,"",0);return out_.str();}
    bool malformed()const{return bad_;}bool limited()const{return limited_;}
private:
    bool text_;unsigned count_;bool first_,bad_,limited_;std::ostringstream out_;
    static bool varint(const uint8_t*& p,const uint8_t* end,uint64_t& v){v=0;
        for(unsigned i=0;i<10 && p<end;++i){uint8_t b=*p++;if(i==9 && b>1)return false;v|=(uint64_t)(b&127)<<(7*i);if(!(b&128))return true;}return false;}
    void error(const std::string& path,size_t offset,const char* why,unsigned field=0,unsigned wire=0){bad_=true;if(!first_)out_<<',';first_=false;
        out_<<"{\"path\":"<<quote(path)<<",\"parse_error\":"<<quote(why)<<",\"offset\":"<<offset;
        if(field)out_<<",\"field\":"<<field<<",\"wire\":"<<wire<<",\"size_unknown\":true";
        out_<<'}';}
    void walk(Node node,const uint8_t* data,size_t n,const std::string& parent,unsigned depth){
        if(!n)return;
        const uint8_t* p=data;const uint8_t* end=data+n;std::map<unsigned,unsigned> occurrences;
        while(p<end){if(count_>=MaxFields){limited_=true;return;}size_t offset=(size_t)(p-data);uint64_t tag=0,v=0,len=0;
            if(!varint(p,end,tag)||!(tag>>3)||(tag>>3)>0x1fffffffu){error(parent,offset,"invalid_tag");return;}
            unsigned number=(unsigned)(tag>>3),wire=(unsigned)(tag&7);const uint8_t* payload=p;size_t size=0;
            if(wire==0){if(!varint(p,end,v)){error(parent,offset,"invalid_varint");return;}size=(size_t)(p-payload);}
            else if(wire==1 || wire==5){size=wire==1?8:4;if((size_t)(end-p)<size){error(parent,offset,"short_fixed");return;}p+=size;}
            else if(wire==2){if(!varint(p,end,len)||len>(uint64_t)(end-p)){error(parent,offset,"short_length");return;}payload=p;size=(size_t)len;p+=size;}
            else {error(parent,offset,"unsupported_wire_type",number,wire);return;}
            ++count_;Field f=describe(node,number);unsigned index=occurrences[number]++;std::ostringstream path;
            path<<parent;if(!parent.empty())path<<'.';if(f.name)path<<f.name;else path<<"unknown_"<<number;
            if(f.repeated||index)path<<'['<<index<<']';
            std::string name=path.str();
            if(!first_)out_<<',';
            first_=false;out_<<"{\"path\":"<<quote(name)<<",\"field\":"<<number<<",\"wire\":"<<wire<<",\"size\":"<<size<<",\"known\":"<<(f.name?"true":"false");
            bool length=f.value==Nested||f.value==Text||f.value==Display||f.value==TimeText;
            bool match=f.name && wire==(length?2u:0u);
            if(f.name&&!match)out_<<",\"type_mismatch\":true";
            if(match && (f.value==Integer||f.value==Boolean)){
                if(f.value==Boolean)out_<<",\"value\":"<<(v?"true":"false");
                else out_<<",\"value\":"<<(int32_t)(uint32_t)v;
            }else if(match && f.value!=Nested){
                if((text_||safeText(payload,size,f.value))&&utf8(payload,size)){
                    size_t take=size<MaxText?size:MaxText;
                    while(take<size && take && (payload[take]&0xc0)==0x80)--take;
                    out_<<",\"value\":"<<quote(std::string((const char*)payload,take));
                    if(take<size)out_<<",\"text_truncated\":true";
                }else out_<<",\"redacted\":true";
            }else if(!f.name)out_<<",\"value_omitted\":true";
            out_<<'}';
            if(match && f.value==Nested){if(depth>=MaxDepth)limited_=true;else walk(f.child,payload,size,name,depth+1);}
            if(limited_)return;
        }
    }
};
// Export the captured wire bytes without interpreting them as a different app.
// Keep scalar wire data exactly; mask text/opaque length-delimited values in
// place. Offsets and lengths remain those of the original protobuf message.
class RawWire {
public:
    RawWire(const uint8_t* p,size_t n,bool text):text_(text),complete_(true),fields_(0) {
        if(p&&n)bytes_.assign(p,p+n);
    }
    std::string json(uint16_t id) {
        walk(schema(id),0,bytes_.size(),0);std::ostringstream o;
        o<<"\"raw_body_hex\":\"";const char* hex="0123456789abcdef";
        for(size_t i=0;i<bytes_.size();++i)o<<hex[bytes_[i]>>4]<<hex[bytes_[i]&15];
        o<<"\",\"raw_exact\":"<<(masks_.empty()?"true":"false")
         <<",\"wire_valid\":"<<(complete_?"true":"false")<<",\"redacted_ranges\":["<<masks_<<']';return o.str();
    }
    static bool varint(const std::vector<uint8_t>& b,size_t& i,size_t end,uint64_t& v) {
        v=0;for(unsigned j=0;j<10&&i<end;++j){unsigned c=b[i++];if(j==9&&c>1)return false;
            v|=(uint64_t)(c&127)<<(7*j);if(!(c&128))return true;}return false;
    }
    static int status(const uint8_t* p,size_t n) {
        if(!p||!n||n>MaxBody)return -1;std::vector<uint8_t> b(p,p+n);size_t i=0;int result=-1;
        while(i<n){uint64_t tag=0,v=0;if(!varint(b,i,n,tag)||!(tag>>3))return -1;
            unsigned wire=(unsigned)(tag&7);
            if(wire==0){if(!varint(b,i,n,v))return -1;if((tag>>3)==1)result=v<=3?(int)v:-1;}
            else if(wire==2){if(!varint(b,i,n,v)||v>n-i)return -1;i+=(size_t)v;}
            else if(wire==1||wire==5){size_t k=wire==1?8:4;if(k>n-i)return -1;i+=k;}
            else return -1;
        }return result;
    }
private:
    std::vector<uint8_t> bytes_;bool text_,complete_;unsigned fields_;std::string masks_;
    void mask(size_t i,size_t n) {if(!n)return;std::ostringstream o;if(!masks_.empty())o<<',';
        o<<"{\"offset\":"<<i<<",\"bytes\":"<<n<<'}';masks_+=o.str();
        for(size_t j=0;j<n;++j)bytes_[i+j]=0;}
    void walk(Node node,size_t start,size_t end,unsigned depth) {
        size_t i=start;
        while(i<end){size_t begin=i;uint64_t tag=0,n=0;
            if(++fields_>MaxFields||!varint(bytes_,i,end,tag)||!(tag>>3)||(tag>>3)>0x1fffffffu){complete_=false;mask(begin,end-begin);return;}
            unsigned wire=(unsigned)(tag&7);Field f=describe(node,(unsigned)(tag>>3));
            if(wire==0){uint64_t value;if(!varint(bytes_,i,end,value)){complete_=false;mask(begin,end-begin);return;}}
            else if(wire==1||wire==5){size_t k=wire==1?8:4;if(k>end-i){complete_=false;mask(begin,end-begin);return;}i+=k;}
            else if(wire==2){
                if(!varint(bytes_,i,end,n)||n>end-i){complete_=false;mask(begin,end-begin);return;}
                size_t size=(size_t)n;
                if(f.name&&f.value==Nested&&depth<MaxDepth)walk(f.child,i,i+size,depth+1);
                else if(!f.name||f.value==Nested||f.value==Integer||f.value==Boolean||
                        !(text_||safeText(size?&bytes_[i]:NULL,size,f.value)))mask(i,size);
                i+=size;
            }else {complete_=false;mask(begin,end-begin);return;}
        }
    }
};
class FileLog {
public:
    // maximum is the TOTAL budget: current + .1 + .2, including newlines.
    FileLog(const char* path,size_t maximum):path_(path),maximum_(maximum),segment_(maximum/3),file_(NULL),bytes_(0),rotations_(0){}
    ~FileLog(){if(file_)fclose(file_);}
    bool write(const std::string& line){
        if(line.size()>=segment_)return false; // whole record, including newline, must fit one file
        const size_t incoming=line.size()+1;
        if(!file_&&!open())return false;
        // Rotate between whole JSON records. Three 5 MiB files stay within
        // the 15 MiB total budget; never split or truncate message content.
        if(bytes_ && (bytes_>segment_ || incoming>segment_-bytes_) && !rotate())return false;
        if(!trim(incoming))return false;
        if(fwrite(line.data(),1,line.size(),file_)!=line.size()||fputc('\n',file_)==EOF||fflush(file_))return false;
        bytes_+=incoming;return true;
    }
    unsigned rotations()const{return rotations_;}
private:
    std::string path_;size_t maximum_,segment_;FILE* file_;uint64_t bytes_;unsigned rotations_;
    bool ensureParent(){
        const size_t slash=path_.find_last_of('/');if(slash==std::string::npos)return true;
        const std::string parent=slash?path_.substr(0,slash):std::string("/");
        // /opt/aa is the existing installation parent. Create only its missing
        // logs directory, not an installation tree or any unrelated directory.
        if(mkdir(parent.c_str(),0700) && errno!=EEXIST)return false;
        struct stat st;return stat(parent.c_str(),&st)==0 && S_ISDIR(st.st_mode);
    }
    static bool sizeOf(const std::string& path,uint64_t& bytes){
        struct stat st;bytes=0;
        if(lstat(path.c_str(),&st))return errno==ENOENT;
        if(!S_ISREG(st.st_mode)||st.st_size<0)return false;
        bytes=(uint64_t)st.st_size;return true;
    }
    bool trim(size_t incoming){
        if(bytes_>maximum_ || incoming>maximum_-bytes_)return false;
        const uint64_t available=maximum_-bytes_-incoming;
        std::string one=path_+".1",two=path_+".2";uint64_t previous=0,oldest=0;
        if(!sizeOf(one,previous)||!sizeOf(two,oldest))return false;
        // Subtraction, rather than summing arbitrary preexisting file sizes,
        // avoids overflow even for an unexpectedly large file at startup.
        if(oldest>segment_ || previous>segment_ || previous>available || oldest>available-previous){
            if(unlink(two.c_str()) && errno!=ENOENT)return false;
        }
        if(previous>segment_ || previous>available){if(unlink(one.c_str()) && errno!=ENOENT)return false;}
        return true;
    }
    bool open(){
        if(!ensureParent())return false;
        int flags=O_WRONLY|O_CREAT|O_APPEND;
#ifdef O_NOFOLLOW
        flags|=O_NOFOLLOW;
#endif
        int fd=::open(path_.c_str(),flags,0600);if(fd<0)return false;
        struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||fchmod(fd,0600)){close(fd);return false;}
        int old=fcntl(fd,F_GETFD);if(old<0||fcntl(fd,F_SETFD,old|FD_CLOEXEC)<0){close(fd);return false;}
        bytes_=(uint64_t)st.st_size;file_=fdopen(fd,"a");if(!file_){close(fd);return false;}return true;
    }
    bool rotate(){if(file_){if(fclose(file_)){file_=NULL;return false;}file_=NULL;}
        std::string one=path_+".1",two=path_+".2";
        if(unlink(two.c_str())&&errno!=ENOENT)return false;
        if(rename(one.c_str(),two.c_str())&&errno!=ENOENT)return false;
        if(rename(path_.c_str(),one.c_str())&&errno!=ENOENT)return false;
        ++rotations_;return open();
    }
};
class Recorder {
public:
    explicit Recorder(const Config& c):config_(c),records_(NULL),read_(0),write_(0),active_(false),threaded_(false),stop_(false),failed_(false),lockReady_(false),session_(0),sequence_(0),run_(stamp()),navigation_(false),navigationSerial_(0){
        lockReady_=pthread_mutex_init(&lock_,NULL)==0;path_[0]=source_[0]=0;
        if(c.path&&strlen(c.path)<sizeof(path_))strcpy(path_,c.path);
        const char* s=sourceLabel(c.source)?c.source:"unspecified";strcpy(source_,s);
    }
    ~Recorder(){stop();if(lockReady_)pthread_mutex_destroy(&lock_);}
    bool start(){return begin(true);}
    uint32_t newSession(){if(!active_)return 0;++session_;enqueue(1,0,NULL,0);return session_;}
    void endSession(){if(active_)enqueue(2,0,NULL,0);}
    void capture(uint16_t id,const uint8_t* p,size_t n,uint16_t channel=10){if(!active_)return;
        const uint64_t ns=stamp();navtrace::Token token;token.sequence=sequence_+1;token.rx_ns=ns;
        token.connection=session_;token.channel=channel;token.id=id;navtrace::event(navtrace::Receive,token);
        enqueue(0,id,p,n,channel,ns);}
    // Producer-only snapshot; the writer never reads these mutable counters.
    Stats stats()const{return stats_;}
    void stop(){
        if(!active_)return;
        // Owner calls only after the AASession producer has joined.
        pthread_mutex_lock(&lock_);final_=stats_;stop_=true;pthread_mutex_unlock(&lock_);
        if(threaded_)pthread_join(thread_,NULL);else run();
        active_=false;navtrace::enable(false);delete[] records_;records_=NULL;
    }
#ifdef NBT_HOST_TEST
    bool startPausedForTest(){return begin(false);}
    void lockForTest(){pthread_mutex_lock(&lock_);}void unlockForTest(){pthread_mutex_unlock(&lock_);}
#endif
private:
    struct Record {uint64_t ns,sequence;uint32_t session;uint16_t id,channel;unsigned kind;size_t original,size;time_t realSeconds;long realNanos;bool realValid;Stats stats;uint8_t body[MaxBody];};
    Config config_;char path_[512],source_[16];Record* records_;unsigned read_,write_;bool active_,threaded_,stop_,failed_,lockReady_;
    uint32_t session_;uint64_t sequence_,run_;bool navigation_;uint64_t navigationSerial_;std::string navigationId_;Stats stats_,final_;pthread_mutex_t lock_;pthread_t thread_;
    bool begin(bool threaded){
        if(active_||!lockReady_||!config_.enabled||!path_[0]||config_.maxTotalBytes<1024)return false;
        // Record contains Stats, so use proper construction rather than calloc.
        records_=new(std::nothrow) Record[Capacity];if(!records_)return false;
        active_=true;stop_=failed_=false;read_=write_=0;
        if(threaded && pthread_create(&thread_,NULL,&Recorder::threadMain,this)){
            active_=false;delete[] records_;records_=NULL;return false;
        }
        threaded_=threaded;navtrace::enable(true);return true;
    }
    void enqueue(unsigned kind,uint16_t id,const uint8_t* p,size_t n,uint16_t channel=10,uint64_t ns=0){
        if(!kind){++stats_.seen;++sequence_;if(n>MaxBody || (n && !p))++stats_.truncated;}
        if(pthread_mutex_trylock(&lock_)) {if(!kind)++stats_.lost;return;}
        if(failed_||write_-read_==Capacity||(!kind&&id!=0x8003&&write_-read_>=Capacity-4)){pthread_mutex_unlock(&lock_);if(!kind)++stats_.lost;return;}
        Record& r=records_[write_%Capacity];r.ns=ns?ns:stamp();r.kind=kind;r.id=id;r.channel=channel;
        timespec wall;r.realValid=clock_gettime(CLOCK_REALTIME,&wall)==0;
        r.realSeconds=r.realValid?wall.tv_sec:0;r.realNanos=r.realValid?wall.tv_nsec:0;r.session=session_;r.sequence=sequence_;
        r.original=n;r.size=n<MaxBody?n:MaxBody;
        if(r.size && !p)r.size=0;
        if(r.size)memcpy(r.body,p,r.size);
        if(!kind)++stats_.captured;
        r.stats=stats_;++write_;pthread_mutex_unlock(&lock_);
    }
    std::string envelope(const Record& r,const char* type,bool navigationContext=true){std::ostringstream o;
        o<<"{\"version\":1,\"record\":"<<quote(type)<<",\"pid\":"<<getpid()<<",\"run_mono_ns\":"<<run_
         <<",\"session\":"<<r.session<<",\"sequence\":"<<r.sequence<<",\"mono_ns\":"<<r.ns
         <<",\"navigation_session_id\":"<<(navigationContext&&!navigationId_.empty()?quote(navigationId_):"null")
         <<",\"application_id\":null,\"source_kind\":\"manual_label\",\"source\":"<<quote(source_)<<",\"seen\":"<<r.stats.seen<<",\"captured\":"<<r.stats.captured
         <<",\"lost\":"<<r.stats.lost<<",\"truncated\":"<<r.stats.truncated;return o.str();}
    std::string render(const Record& r){
        if(r.kind)return envelope(r,r.kind==1?"session_start":"session_end")+"}";
        Decoder d(config_.text);std::string fields=d.fields(r.id,r.body,r.size);std::ostringstream o,hex;hex<<"0x"<<std::hex<<r.id;
        o<<envelope(r,"navigation")<<",\"channel\":"<<r.channel<<",\"message_id\":"<<r.id<<",\"message_id_hex\":"<<quote(hex.str())<<",\"known_message\":"<<(schema(r.id)!=Unknown?"true":"false")
         <<",\"original_bytes\":"<<r.original<<",\"captured_bytes\":"<<r.size<<",\"body_truncated\":"<<(r.size<r.original?"true":"false")
         <<",\"malformed\":"<<(d.malformed()?"true":"false")<<",\"field_limit\":"<<(d.limited()?"true":"false")<<",\"fields\":["<<fields<<"]}";
        return o.str();
    }
    std::string wallFields(const Record& r) {
        struct tm utc;char date[32];
        // Plausibility, not an assertion of NTP/GPS synchronization. QNX time_t is 32 bit.
        const bool valid=r.realValid&&(int64_t)r.realSeconds>=1577836800ll&&
            (int64_t)r.realSeconds<4102444800ll&&gmtime_r(&r.realSeconds,&utc)&&
            strftime(date,sizeof(date),"%Y-%m-%dT%H:%M:%SZ",&utc);
        std::ostringstream o;o<<",\"wall_time_valid\":"<<(valid?"true":"false")
            <<",\"wall_time\":"<<(valid?quote(date):"null")
            <<",\"wall_time_quality\":"<<quote(valid?"system_clock_plausible_unverified":"unavailable_or_implausible")
            <<",\"wall_time_nsec\":"<<(valid?r.realNanos:0);return o.str();
    }
    void refreshManualLabel() {
        strcpy(source_,sourceLabel(config_.source)?config_.source:"unspecified");
        // Optional operator label, read only by the writer at navigation start.
        // This is never an application identifier obtained from Android Auto.
        std::string path(path_);size_t slash=path.find_last_of('/');
        path=slash==std::string::npos?"nav-diag-source":path.substr(0,slash+1)+"nav-diag-source";
        int fd=open(path.c_str(),O_RDONLY|O_NONBLOCK);if(fd<0)return;
        struct stat st;char label[32];ssize_t n=0;
        if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<(off_t)sizeof(label))
            n=read(fd,label,sizeof(label)-1);
        close(fd);if(n<=0)return;
        label[n]=0;while(n>0&&(label[n-1]=='\n'||label[n-1]=='\r'))label[--n]=0;
        if(sourceLabel(label))strcpy(source_,label);
    }
    bool process(FileLog& file,const Record& r) {
        if(r.kind==1){navigation_=false;navigationId_.clear();}
        const int status=!r.kind&&r.id==0x8003&&r.size==r.original?RawWire::status(r.body,r.size):-1;
        if(status==1&&!navigation_) {
            refreshManualLabel();
            navigation_=true;std::ostringstream id;
            id<<getpid()<<'-'<<run_<<'-'<<r.realSeconds<<'-'<<r.realNanos<<'-'<<r.session<<'-'<<++navigationSerial_;
            navigationId_=id.str();
            if(!file.write(envelope(r,"navigation_session_start")+wallFields(r)+",\"trigger\":\"navigation_status_active\"}"))return false;
        }
        if(!r.kind) {
            RawWire wire(r.body,r.size,config_.text);std::ostringstream raw;
            raw<<envelope(r,"navigation_raw")<<",\"channel\":"<<r.channel<<",\"message_id\":"<<r.id
               <<",\"known_message\":"<<(schema(r.id)!=Unknown?"true":"false")
               <<",\"original_bytes\":"<<r.original<<",\"captured_bytes\":"<<r.size
               <<",\"body_truncated\":"<<(r.size<r.original?"true":"false")<<','<<wire.json(r.id)<<'}';
            if(!file.write(raw.str()))return false;
        }
        if(!file.write(render(r)))return false;
        if(navigation_&&(r.kind==2||status==0||status==2)) {
            if(!file.write(envelope(r,"navigation_session_end")+"}"))return false;
            navigation_=false;navigationId_.clear();
        }
        return true;
    }
    bool drainTrace(FileLog& file) {
        navtrace::Event e;unsigned dropped=0;
        for(unsigned i=0;i<16&&navtrace::take(e,dropped);++i) {
            Record r;r.ns=e.ns;r.sequence=e.serial;r.session=e.token.connection;r.stats=Stats();
            std::ostringstream o;o<<envelope(r,"navigation_timing",false)
                <<",\"stage\":"<<quote(navtrace::name(e.stage))<<",\"rx_sequence\":"<<e.token.sequence
                <<",\"rx_mono_ns\":"<<e.token.rx_ns<<",\"channel\":"<<e.token.channel
                <<",\"message_id\":"<<e.token.id<<",\"result\":"<<e.result
                <<",\"maneuver_type\":"<<(e.stage==navtrace::MostSend||e.stage==navtrace::MostBegin?-1:e.type)
                <<",\"most_function\":"<<(e.stage==navtrace::MostSend||e.stage==navtrace::MostBegin?e.type:0)<<",\"exit_number\":"<<e.exit<<",\"exit_angle\":"<<e.angle
                <<",\"distance_m\":"<<(e.metres==e.metres&&e.metres>-1e100&&e.metres<1e100?e.metres:-1)<<",\"handle\":"<<e.handle
                <<",\"trace_queue_full_drops\":"<<dropped
                <<",\"correlation\":"<<quote(e.token.sequence?"rx_sequence":"timeline_only")<<'}';
            if(!file.write(o.str()))return false;
        }return true;
    }
    static void* threadMain(void* p){static_cast<Recorder*>(p)->run();return NULL;}
    void run(){
        try {
            FileLog file(path_,config_.maxTotalBytes);
            for(;;){Record r;bool have=false,finish=false;
                pthread_mutex_lock(&lock_);
                if(read_!=write_){r=records_[read_%Capacity];++read_;have=true;}
                else if(stop_){r.stats=final_;r.ns=stamp();r.session=session_;r.sequence=sequence_;finish=true;}
                pthread_mutex_unlock(&lock_);
                if(have&&!process(file,r))break;
                if(!drainTrace(file))break;
                if(finish){
                    // Bounded final drain; producers never wait for the exporter.
                    for(unsigned i=0;i<8;++i)if(!drainTrace(file))return;
                    std::ostringstream o;o<<envelope(r,"shutdown")<<",\"rotations\":"<<file.rotations()<<'}';
                    if(!file.write(o.str()))break;return;
                }
                if(!have)usleep(100000);
            }
        }catch(...){ /* diagnostic failure must not terminate Android Auto */ }
        pthread_mutex_lock(&lock_);failed_=true;pthread_mutex_unlock(&lock_);
        fprintf(stderr,"[NavDiag] export stopped (open/write/rotation/format failure): %s; Android Auto continues\n",path_);
    }
    Recorder(const Recorder&);Recorder& operator=(const Recorder&);
};
} }
#endif
