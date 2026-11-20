#ifndef NBT_GRAFITE_ORO_ARTWORK_HPP
#define NBT_GRAFITE_ORO_ARTWORK_HPP
#include <vector>
namespace nbt { namespace sixwbkit {
// Scoped enum spelling without C++11 enum classes (QNX GCC 4.4.2).
struct Maneuver { enum Value {
#include "maneuvers.inc"
}; };
struct PlanePolicy { enum Value { Hidden, DistanceBased, FlatOnly }; };
struct Color { enum Value { None, Graphite, Gold, White, Gray, Black, Red, ExitWhite }; };
struct Point { double x,y; };
struct Rect { double x,y,width,height; Rect(double a=0,double b=0,double w=0,double h=0):x(a),y(b),width(w),height(h){} };
struct Contour { bool closed; std::vector<Point> points; Contour():closed(false){} };
struct Shape { Color::Value fill,stroke;double strokeWidth;std::vector<Contour> contours;
    Shape():fill(Color::None),stroke(Color::None),strokeWidth(0){} };
typedef std::vector<Shape> Scene;
struct ManeuverInfo {const char* key;const char* label;PlanePolicy::Value policy;};
enum LaneDirection { LaneLeft=1,LaneStraight=2,LaneRight=4 };
struct Direction {int shape;bool highlighted;Direction(int s=0,bool h=false):shape(s),highlighted(h){} };
struct Lane { std::vector<Direction> directions; };
struct NavigationEvent {Maneuver::Value maneuver;bool hasExitAngle,hasExitNumber;double exitAngle;unsigned exitNumber;std::vector<Lane> lanes;
    NavigationEvent():maneuver(Maneuver::Unknown),hasExitAngle(false),hasExitNumber(false),exitAngle(0),exitNumber(0){} };
const ManeuverInfo& describe(Maneuver::Value maneuver);
const char* rgb(Color::Value color);
Scene renderMiniature(const NavigationEvent& event,Rect area);
Scene renderManeuver(const NavigationEvent& event,Rect area,double margin=2.0);
Scene renderLanes(const std::vector<Lane>& lanes,Rect area=Rect(0,0,283,40));
bool finite(double value);
} }
#endif
