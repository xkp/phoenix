#include "phoenix/scripting/primitives3.hpp"
#include <cmath>
#include <iostream>
namespace {bool close(double a,double b){return std::abs(a-b)<1e-9;}}
int main(){using namespace phoenix;using namespace phoenix::scripting;
const auto d=direction(Vector3d{0,3,4});const bool vectors=d&&close(d->y,.6)&&close(d->z,.8)&&close(dot({1,2,3},{4,5,6}),32);
const auto t=compose(Transform3d::translation({3,4,5}),Transform3d::scaling(2,2,2));const auto p=transform(Point3d{1,2,3},t);const auto ti=inverse(t);const auto back=ti?transform(p,*ti):Point3d{};const bool transforms=close(p.x,5)&&close(p.y,8)&&close(p.z,11)&&close(back.x,1)&&close(back.y,2)&&close(back.z,3)&&is_even(t);
const bool predicates=has_on(Segment3d{{0,0,0},{2,0,0}},{1,0,0})&&!has_on(Segment3d{{0,0,0},{2,0,0}},{3,0,0})&&has_on(Triangle3d{{Point3d{0,0,0},Point3d{2,0,0},Point3d{0,2,0}}},{.5,.5,0})&&close(squared_area({{Point3d{0,0,0},Point3d{2,0,0},Point3d{0,2,0}}}),4);
const Plane3d plane{0,0,1,-2};const auto projected=projection(plane,{1,3,9});const bool planes=has_on(plane,{5,6,2})&&oriented_side(plane,{0,0,3})>0&&close(projected.z,2);
std::cout<<"script 3D vectors: "<<vectors<<'\n'<<"script 3D transforms: "<<transforms<<'\n'<<"script exact predicates: "<<predicates<<'\n'<<"script 3D planes: "<<planes<<'\n';return vectors&&transforms&&predicates&&planes?0:1;}
