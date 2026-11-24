// Build actual bt_audio.cpp with observable hardware ports. No car commands are executed.
#include "audio/bt_audio.hpp"
#include "audio/channels.hpp"
#include <assert.h>
#include <stdio.h>
namespace nbt { int btBuilds=0,btRemoves=0,btCancels=0,btPlays=0,btStops=0,btSpeechFrames=0,btSpeechEnds=0; }
using namespace nbt;
int main() {
    assert(btAutoSourceSetting(-1,false));
    assert(!btAutoSourceSetting(-1,true));
    assert(!btAutoSourceSetting(0,false));
    assert(!btAutoSourceSetting(0,true));
    assert(btAutoSourceSetting(1,false));
    assert(btAutoSourceSetting(1,true));
    const char* off[]={"0","off","no","false"};
    for(unsigned i=0;i<4;++i){setenv("NBT_BT_AUTO_SOURCE",off[i],1);assert(!readBtConfig().auto_source);}
    const char* on[]={"1","on","yes","true"};
    for(unsigned i=0;i<4;++i){setenv("NBT_BT_AUTO_SOURCE",on[i],1);assert(readBtConfig().auto_source);}
    unsetenv("NBT_BT_AUTO_SOURCE");
    for(int conn=0;conn<2;++conn) {
        BtConfig c; c.conn=conn?0xc7:0; c.auto_source=false;c.refocus=true;c.release=true;
        BtAudio audio;audio.init(c);
        int b=btBuilds,r=btRemoves,p=btPlays,s=btStops;
        audio.onSessionStarted();audio.onMicOpened();audio.onMicClosed();
        // Reconnects and repeated Assistant completions must also remain manual.
        audio.onSessionEnded();audio.onSessionStarted();audio.onMicClosed();
        audio.onFrame(AA_CH_GUIDANCE_AUDIO,NULL,0,0);audio.onSessionEnded();audio.stop();
        assert(btBuilds==b && btRemoves==r && btPlays==p && btStops==s);
        assert(btSpeechFrames>0 && btSpeechEnds>0 && btCancels>0);
    }
    for(int conn=0;conn<2;++conn) {
        BtConfig c;c.conn=conn?0xc7:0;c.release=true;
        BtAudio audio;audio.init(c);int b=btBuilds,p=btPlays,r=btRemoves;
        audio.onSessionStarted();audio.onMicClosed();
        assert(btBuilds==b+2 && btPlays==p+2);
        audio.onSessionEnded();assert(btRemoves==r+1);
    }
    { BtConfig c;c.refocus=false;BtAudio audio;audio.init(c);int b=btBuilds;
      audio.onSessionStarted();audio.onMicClosed();assert(btBuilds==b+1); }
    puts("PASS: default, env/flag precedence, both routing forms, reconnect, Assistant, release/PLAY isolation and speech forwarding.");
}
