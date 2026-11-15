#ifndef NBT_HUD6WB_CALIBRATION_HPP
#define NBT_HUD6WB_CALIBRATION_HPP
namespace nbt { namespace hudterm {
// Source switch: no env/file dependency, no extra application or thread.
static const bool ENABLE_PALETTE_CALIBRATION = true;
static const double PALETTE_CALIBRATION_SECONDS = 10.0;
// Pure lifecycle clock. Caller supplies CLOCK_MONOTONIC seconds; no sleeps.
class PaletteCalibration {
public:
    enum Phase {Dormant, Pending, Dispatching, Holding, Clearing, Done};
    explicit PaletteCalibration(bool enabled=ENABLE_PALETTE_CALIBRATION)
        :enabled_(enabled),route_(false),phase_(Dormant),deadline_(0){}
    void observe(int status) {
        if(!enabled_)return;
        if(status==1 && !route_) {route_=true;phase_=Pending;deadline_=0;}
        else if(status==2) {
            route_=false;
            if(phase_==Dispatching||phase_==Holding)phase_=Clearing;
            else if(phase_==Pending)phase_=Dormant;
        }
        // REROUTING (3) and duplicate ACTIVE do not start a new calibration.
    }
    Phase phase()const{return phase_;}
    bool active()const{return phase_==Pending||phase_==Dispatching||phase_==Holding||phase_==Clearing;}
    void queued(){if(phase_==Pending)phase_=Dispatching;}
    void dispatched(double now){if(phase_==Dispatching){deadline_=now+PALETTE_CALIBRATION_SECONDS;phase_=Holding;}}
    bool due(double now)const{return phase_==Holding && now>=deadline_;}
    void clearQueued(){phase_=Clearing;}
    void finished(){phase_=route_?Done:Dormant;deadline_=0;}
    void cancel(){phase_=route_?Done:Dormant;deadline_=0;}
    void reset(){route_=false;phase_=Dormant;deadline_=0;}
    double nextIn(double now)const{
        if(phase_==Holding)return deadline_>now?deadline_-now:0.01;
        if(phase_==Dispatching||phase_==Clearing)return 0.01;
        return 0.25;
    }
private:
    bool enabled_,route_;Phase phase_;double deadline_;
};
} }
#endif
