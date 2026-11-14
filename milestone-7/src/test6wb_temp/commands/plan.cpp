// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "commands/plan.hpp"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
namespace test6wb {
using namespace nbt::hudterm;
const TestDefinition* catalog(size_t& count) {
    // Public IDs are permanent. Append future tests; never renumber these entries.
    static const TestDefinition tests[]={
        {1,0x18,"Bitmap A: red identity"},{2,0x19,"Bitmap B: blue identity"},
        {3,0x1f,"Main A: vector identity"},{4,0x20,"Main B: vector identity"},
        {5,0x23,"Lanes A: vector identity"},{6,0x24,"Lanes B: vector identity"},
        {7,0x1a,"Street: automatic text label"},{8,0x1b,"Distance: automatic text label"},
        {9,0x1c,"Unit: numeric test label"},{10,0x35,"Approach bar: value 128"}
    };
    count=sizeof(tests)/sizeof(tests[0]);return tests;
}
bool number(const std::string& text,unsigned max,unsigned& value) {
    if(text.empty() || text[0]=='-' || text[0]=='+')return false;
    int base=text.size()>2 && text[0]=='0' && (text[1]=='x' || text[1]=='X')?16:10;
    char* end=NULL;errno=0;unsigned long n=strtoul(text.c_str(),&end,base);
    if(errno || end==text.c_str() || !end || *end || n>max)return false;
    value=(unsigned)n;return true;
}
namespace {
bool addTrial(unsigned handle,bool isolate,const std::string& variant,
              const std::string& description,std::vector<Trial>& trials,std::string& error,unsigned testId=0) {
    const Surface* s=surface(handle);
    if(!s || s->kind==Container){error="select a diagnostic surface; 0x17 supports frame on/off only";return false;}
    if(trials.size()>=320){error="plan limited to 320 trials";return false;}
    Trial t;t.id=trials.size()+1;t.testId=testId;t.handle=handle;t.isolate=isolate;t.clear=false;t.description=description;
    unsigned labelId=testId?testId:t.id;
    if(s->kind==Bitmap || s->kind==Vector) {
        std::string colour=s->kind==Vector?"theme":handle==0x18?"red":"blue";
        if(!identifiedPattern(*s,labelId,variant,colour,t.batch,error))return false;
        t.parameters=variant+" colour="+colour+(variant=="identity"?" grid=40":" handle_specific_bands")+
                     " label=0xHH_Tnnn two_copies";
    } else if(s->kind==Text) {
        char text[32];
        if(handle==0x1a)snprintf(text,sizeof(text),"0x1A T%03u",labelId);
        else if(handle==0x1b)snprintf(text,sizeof(text),"1BT%03u",labelId);
        else snprintf(text,sizeof(text),"%03u",labelId);
        if(!textPattern(handle,text,t.batch,error))return false;
        t.parameters=std::string("Latin-15 text=")+text;
    } else {
        if(!barPattern(128,t.batch,error))return false;
        t.parameters="value=128 range=0..255 no_embedded_label";
    }
    trials.push_back(t);return true;
}
}
bool plan(const std::vector<std::string>& args,const std::string& variant,
          std::vector<Trial>& trials,std::string& error) {
    if(args.empty()){error="missing command; use --help";return false;}
    const std::string& cmd=args[0];unsigned handle=0;
    if(cmd=="test" || cmd=="range" || cmd=="all") {
        size_t count=0;const TestDefinition* tests=catalog(count);unsigned first=1,last=(unsigned)count;
        if(variant!="identity"){error="catalog tests have fixed identity patterns; use advanced commands for bands";return false;}
        if(cmd=="test") {
            if(args.size()!=2 || !number(args[1],count,first) || !first){error="test ID: valid IDs 001..010";return false;}
            last=first;
        } else if(cmd=="range") {
            if(args.size()!=3 || !number(args[1],count,first) || !number(args[2],count,last) || !first || first>last) {
                error="range FIRST LAST: ascending, inclusive IDs 001..010";return false;
            }
        } else if(args.size()!=1){error="all takes no positional arguments";return false;}
        for(unsigned id=first;id<=last;++id) {
            const TestDefinition& t=tests[id-1];
            if(!addTrial(t.handle,true,"identity",t.description,trials,error,t.id))return false;
        }
        return true;
    }
    if(cmd=="sequence") {
        if(args.size()>2 || (args.size()==2 && args[1]!="graphics")){error="sequence [graphics]";return false;}
        for(size_t i=0;i<surfaceCount;++i) {
            const Surface& s=surfaces[i];
            if(s.kind==Container || (args.size()==2 && s.kind!=Bitmap && s.kind!=Vector))continue;
            if(!addTrial(s.handle,true,variant,"sequence",trials,error))return false;
        }
        return true;
    }
    if(cmd=="compose") {
        if(args.size()<3){error="compose requires at least two handles from different resource groups";return false;}
        bool used[256]={false};
        // Validate the entire selection before generating trials or connecting DSI.
        for(size_t i=1;i<args.size();++i) {
            if(!number(args[i],255,handle) || !surface(handle) || surface(handle)->kind==Container) {
                error="invalid composition handle";return false;
            }
            const Surface& s=*surface(handle);
            if(used[handle] || (s.partner && used[s.partner])) {
                error="composition cannot include duplicate handles or both A/B buffers";return false;
            }
            used[handle]=true;
        }
        for(size_t i=1;i<args.size();++i) {
            number(args[i],255,handle);
            if(!addTrial(handle,i==1,variant,"compose step",trials,error))return false;
        }
        return true;
    }
    if(cmd=="isolated") {
        if(args.size()!=2 || !number(args[1],255,handle)){error="isolated HANDLE";return false;}
        return addTrial(handle,true,variant,"isolated",trials,error);
    }
    if(cmd=="ab") {
        if(args.size()!=2 || !number(args[1],255,handle) || !surface(handle) || !surface(handle)->partner) {
            error="ab HANDLE requires one documented A/B image handle";return false;
        }
        unsigned pair=surface(handle)->partner;
        return addTrial(handle,true,variant,"A/B start",trials,error) &&
               addTrial(pair,false,variant,"A/B partner",trials,error) &&
               addTrial(handle,false,variant,"A/B return",trials,error);
    }
    if(trials.size()>=320){error="plan limited to 320 trials";return false;}
    Trial t;t.id=trials.size()+1;t.testId=0;t.handle=0;t.clear=cmd=="clear";t.isolate=false;t.description=cmd;
    if(cmd=="clear") {
        if(args.size()!=1){error="clear takes no arguments";return false;}
        t.batch=clearBatch();t.parameters="all_known_resources";
    } else if(cmd=="frame") {
        if(args.size()!=2 || (args[1]!="on" && args[1]!="off")){error="frame on|off";return false;}
        t.handle=0x17;t.batch=frameBatch(args[1]=="on");t.parameters="visibility="+args[1];
    } else if(cmd=="bar") {
        unsigned value;
        if(args.size()!=2 || !number(args[1],255,value)){error="bar VALUE (0..255)";return false;}
        t.handle=0x35;t.isolate=true;
        if(!barPattern(value,t.batch,error))return false;
        t.parameters="value="+args[1]+" no_embedded_label";
    } else if(cmd=="text") {
        if(args.size()<3 || !number(args[1],255,handle)){error="text HANDLE STRING";return false;}
        std::string text=args[2];for(size_t i=3;i<args.size();++i)text+=' '+args[i];
        t.handle=handle;t.isolate=true;
        if(!textPattern(handle,text,t.batch,error))return false;
        t.parameters="user_text="+text+" (ID on CLI; use isolated for automatic visual ID)";
    } else if(cmd=="fill" || cmd=="border") {
        if(args.size()<2 || args.size()>3 || (cmd=="fill" && args.size()!=3) || !number(args[1],255,handle) || !surface(handle)) {
            error="fill HANDLE COLOUR or border HANDLE [COLOUR]";return false;
        }
        const Surface& s=*surface(handle);std::string colour=args.size()==3?args[2]:s.kind==Vector?"theme":"white";
        t.handle=handle;t.isolate=true;
        if(cmd=="fill" && s.kind==Vector) {
            if(colour!="theme"){error="vector fill requires theme index 2";return false;}
            if(!vectorFill(s,t.batch,error))return false;
            t.parameters="solid theme_index=2 ID_on_CLI_only; use isolated/border for vector label";
        } else {
            if(!identifiedPattern(s,t.id,variant,colour,t.batch,error,cmd=="border"))return false;
            t.parameters="identified "+cmd+" colour="+colour+" variant="+variant;
        }
    } else {error="unknown command; use --help";return false;}
    if(!validate(t.batch,error))return false;
    trials.push_back(t);return true;
}
}
