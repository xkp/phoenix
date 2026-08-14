#include "phoenix/scripting/intersections3.hpp"
#include <cmath>
#include <iostream>
namespace {bool close(double a,double b){return std::abs(a-b)<1e-9;}}
int main(){using namespace phoenix;using namespace phoenix::scripting;
const auto crossing=intersection(Segment3d{{0,0,0},{2,0,0}},Segment3d{{1,-1,0},{1,1,0}});const auto* cp=std::get_if<Point3d>(&crossing);const bool point=cp&&close(cp->x,1)&&close(cp->y,0);
const auto overlap=intersection(Segment3d{{0,0,0},{3,0,0}},Segment3d{{1,0,0},{2,0,0}});const auto* os=std::get_if<Segment3d>(&overlap);const bool segment=os&&close(os->source.x,1)&&close(os->target.x,2);
const auto parallel=intersection(Line3d{{0,0,0},{1,0,0}},Line3d{{0,1,0},{1,0,0}});const bool none=std::holds_alternative<std::monostate>(parallel);
const auto coincident=intersection(Plane3d{0,0,1,0},Plane3d{0,0,2,0});const bool plane=std::holds_alternative<Plane3d>(coincident);
const auto planes=intersection(Plane3d{1,0,0,-1},Plane3d{0,1,0,-2});const auto* line=std::get_if<Line3d>(&planes);const bool line_result=line&&has_on(*line,{1,2,5});
std::cout<<"script intersection point: "<<point<<'\n'<<"script intersection overlap: "<<segment<<'\n'<<"script intersection none: "<<none<<'\n'<<"script intersection plane: "<<plane<<'\n'<<"script intersection line: "<<line_result<<'\n';return point&&segment&&none&&plane&&line_result?0:1;}
