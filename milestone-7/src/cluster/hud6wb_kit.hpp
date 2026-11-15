#ifndef NBT_HUD6WB_KIT_HPP
#define NBT_HUD6WB_KIT_HPP
#include "cluster/grafite_oro/hudterminal_palette.hpp"
#include "cluster/hudterm.hpp"
#include "cluster/hud_terminal.hpp"
#include "cluster/nav_msg.hpp"
#include "cluster/hud6wb_profile.hpp"
namespace nbt { namespace hudterm {
// No new handle discovery or surface allocation. Calibration is loaded once.
class GrafiteKit {
public:
    GrafiteKit();
    GrafiteKit(const sixwbkit::HudTerminalPalette& main,const sixwbkit::HudTerminalPalette& flat,
               const sixwbkit::HudTerminalPalette& lanes):main_(main),flat_(flat),lanes_(lanes){}
    bool central(const kombinav::NavView& v,uint16_t handle,const Rect& useful,bool perspective,Batch& out)const;
    bool lanes(const kombinav::NavView& v,uint16_t handle,unsigned width,unsigned height,Batch& out)const;
    bool swatches(uint16_t handle,const Rect& useful,unsigned columns,Batch& out)const;
    static sixwbkit::Maneuver::Value maneuver(int aaType);
    static bool laneEvent(const kombinav::NavView& v,sixwbkit::NavigationEvent& event);
    static bool encode(const sixwbkit::Scene& scene,const sixwbkit::HudTerminalPalette& palette,
                       uint16_t handle,const Rect& bounds,Batch& out);
    static bool hidden(int aaType){return sixwbkit::describe(maneuver(aaType)).policy==sixwbkit::PlanePolicy::Hidden;}
private:
    sixwbkit::HudTerminalPalette main_,flat_,lanes_;
    void readProfile();
};
struct KitBarState {bool visible;int value;KitBarState():visible(false),value(0){} };
KitBarState kitBarState(const kombinav::NavView& v,unsigned rangeMetres);
enum KitPlane {KitFlat,KitPerspective};
KitPlane kitPlane(sixwbkit::PlanePolicy::Value policy,bool barVisible);
void configureKitPlanes(Bmw6wbProfile& p);
} }
#endif
