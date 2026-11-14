// Prepared for the user's host/SDK environment. NOT compiled or executed in Codex.
#include "nav_diag.hpp"
#include "kombi_nav_test_util.hpp"
#include <assert.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
using namespace nbt::navdiag;
using kntest::Bytes;
std::string readFile(const std::string& path){std::ifstream f(path.c_str());std::ostringstream s;s<<f.rdbuf();return s.str();}
std::string decode(uint16_t id,const Bytes& b,bool text=false){Decoder d(text);std::string s=d.fields(id,b.empty()?NULL:&b[0],b.size());assert(!d.malformed());return s;}
void testModern(){
    Bytes man,road,step,state; kntest::fInt(man,1,8);kntest::fBytes(step,1,man);
    kntest::fStr(road,1,"Sensitive Road");kntest::fBytes(step,2,road);
    kntest::fBytes(step,4,kntest::msgStr(1,"Sensitive instruction"));
    kntest::fBytes(state,1,step);kntest::fBytes(state,1,step);kntest::fBytes(state,1,step);
    kntest::fBytes(state,2,kntest::msgStr(1,"Sensitive Destination"));
    std::string s=decode(0x8006,state);assert(s.find("steps[2].road.name")!=std::string::npos);
    assert(s.find("Sensitive")==std::string::npos&&s.find("redacted")!=std::string::npos);
    assert(decode(0x8006,state,true).find("Sensitive Road")!=std::string::npos);
    Bytes dist,dd,position; kntest::fInt(dist,1,44306);kntest::fStr(dist,2,"28");kntest::fInt(dist,3,4);
    kntest::fBytes(dd,1,dist);kntest::fStr(dd,2,"12:17 AM");kntest::fInt(dd,3,1844);
    kntest::fBytes(position,2,dd);kntest::fBytes(position,2,dd);kntest::fBytes(position,3,road);
    s=decode(0x8007,position);assert(s.find("destination_distances[1].eta")!=std::string::npos);
    assert(s.find("12:17 AM")!=std::string::npos&&s.find("1844")!=std::string::npos&&s.find("44306")!=std::string::npos);
    assert(s.find("Sensitive")==std::string::npos);
    s=decode(0x8006,kntest::navState(8,"Fixture Street"));assert(s.find("maneuver.type")!=std::string::npos);
    s=decode(0x8007,kntest::curPos(250,"250",1));assert(s.find("step_distance.distance.meters")!=std::string::npos);
}
void testLegacyUnknownMalformed(){
    Bytes turn;kntest::fStr(turn,1,"Private road");kntest::fInt(turn,2,1);kntest::fInt(turn,3,4);
    std::string s=decode(0x8004,turn);assert(s.find("turn_side")!=std::string::npos&&s.find("Private road")==std::string::npos);
    Bytes distance;kntest::fInt(distance,1,90);kntest::fInt(distance,2,20);kntest::fInt(distance,3,90000);kntest::fInt(distance,4,1);
    assert(decode(0x8005,distance).find("display_distance_e3")!=std::string::npos);
    Bytes unknown;kntest::fInt(unknown,99,12345);kntest::fStr(unknown,100,"Opaque private payload");
    // fixed32/64: known size, uninterpreted contents.
    kntest::varint(unknown,(101u<<3)|5);unknown.insert(unknown.end(),4,0x41);
    kntest::varint(unknown,(102u<<3)|1);unknown.insert(unknown.end(),8,0x42);
    s=decode(0x8fff,unknown,true);assert(s.find("unknown_102")!=std::string::npos);
    assert(s.find("Opaque")==std::string::npos&&s.find("12345")==std::string::npos);
    uint8_t bad[]={0x0a,0x7f,0x01};Decoder d(false);d.fields(0x8006,bad,sizeof(bad));assert(d.malformed());
    uint8_t overflow[]={0x08,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x02};
    Decoder v(false);v.fields(0x8003,overflow,sizeof(overflow));assert(v.malformed());
    Bytes many;for(int i=0;i<MaxFields+1;++i)kntest::fInt(many,99,1);
    Decoder limit(false);limit.fields(0x8003,&many[0],many.size());assert(limit.limited());
    Bytes longText=kntest::msgStr(1,std::string(MaxText+20,'x'));
    s=decode(0x8004,longText,true);assert(s.find("text_truncated")!=std::string::npos);
}
void testRecorderAndRotation(){
    char dir[]="/tmp/nav-diag-XXXXXX";assert(mkdtemp(dir));std::string path=std::string(dir)+"/nav.jsonl";
    Config disabled;disabled.path=path.c_str();Recorder off(disabled);assert(!off.start());
    off.capture(0x8003,NULL,0);assert(off.stats().seen==0&&access(path.c_str(),F_OK));
    Config c;c.enabled=true;c.path=path.c_str();
    {
        Recorder r(c);assert(r.startPausedForTest());r.newSession();
        Bytes body=kntest::status(1);
        r.lockForTest();r.capture(0x8003,&body[0],body.size());r.unlockForTest();assert(r.stats().lost==1);
        for(int i=0;i<40;++i)r.capture(0x8003,&body[0],body.size());
        assert(r.stats().captured==Capacity-1&&r.stats().seen==41&&r.stats().lost==41-(Capacity-1));
        r.stop();
    }
    std::string s=readFile(path);assert(s.find("\"record\":\"shutdown\"")!=std::string::npos);
    struct stat st;assert(!stat(path.c_str(),&st)&&(st.st_mode&0777)==0600);
    unlink(path.c_str());
    {
        Recorder r(c);assert(r.startPausedForTest());r.newSession();Bytes big(MaxBody+1,0);
        r.capture(0x8006,&big[0],big.size());assert(r.stats().truncated==1);r.stop();
        assert(readFile(path).find("\"body_truncated\":true")!=std::string::npos);
    }
    unlink(path.c_str());
    {
        FileLog f(path.c_str(),1024);std::string line="{\"padding\":\""+std::string(700,'x')+"\"}";
        for(int i=0;i<7;++i)assert(f.write(line));
        assert(f.rotations()==6);
        assert(!f.write(std::string(1025,'x')));
    }
    for(int i=0;i<3;++i){std::string file=path+(i==0?"":i==1?".1":".2");assert(!stat(file.c_str(),&st));
        assert(st.st_size<=1024&&(st.st_mode&0777)==0600);std::string contents=readFile(file);assert(!contents.empty()&&contents[contents.size()-1]=='\n');unlink(file.c_str());}
    FileLog missing((std::string(dir)+"/absent/nav").c_str(),1024);assert(!missing.write("{}"));rmdir(dir);
}
int main(){testModern();testLegacyUnknownMalformed();testRecorderAndRotation();puts("test_nav_diag OK");}
