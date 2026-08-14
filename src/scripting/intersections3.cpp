#include "phoenix/scripting/intersections3.hpp"

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/intersections.h>

namespace phoenix::scripting {
namespace {
using K=CGAL::Exact_predicates_exact_constructions_kernel;
using P=K::Point_3;using V=K::Vector_3;using L=K::Line_3;using S=K::Segment_3;using Plane=K::Plane_3;
P p(Point3d v){return {v.x,v.y,v.z};}
L l(Line3d v){return {p(v.point),V(v.direction.x,v.direction.y,v.direction.z)};}
S s(Segment3d v){return {p(v.source),p(v.target)};}
Plane plane(Plane3d v){return {v.a,v.b,v.c,v.d};}
Point3d out(const P&v){return {CGAL::to_double(v.x()),CGAL::to_double(v.y()),CGAL::to_double(v.z())};}
Line3d out(const L&v){const auto q=v.point();const auto d=v.direction();return {out(q),{CGAL::to_double(d.dx()),CGAL::to_double(d.dy()),CGAL::to_double(d.dz())}};}
Segment3d out(const S&v){return {out(v.source()),out(v.target())};}
Plane3d out(const Plane&v){return {CGAL::to_double(v.a()),CGAL::to_double(v.b()),CGAL::to_double(v.c()),CGAL::to_double(v.d())};}
template<class Result> Intersection3 point_line_or_none(const Result&r){if(!r)return {};if(const auto*v=std::get_if<P>(&*r))return out(*v);if(const auto*v=std::get_if<L>(&*r))return out(*v);return {};}
template<class Result> Intersection3 point_segment_or_none(const Result&r){if(!r)return {};if(const auto*v=std::get_if<P>(&*r))return out(*v);if(const auto*v=std::get_if<S>(&*r))return out(*v);return {};}
template<class Result> Intersection3 line_plane_or_none(const Result&r){if(!r)return {};if(const auto*v=std::get_if<L>(&*r))return out(*v);if(const auto*v=std::get_if<Plane>(&*r))return out(*v);return {};}
}
Intersection3 intersection(Line3d a,Line3d b){return point_line_or_none(CGAL::intersection(l(a),l(b)));}
Intersection3 intersection(Line3d a,Plane3d b){return point_line_or_none(CGAL::intersection(l(a),plane(b)));}
Intersection3 intersection(Plane3d a,Line3d b){return intersection(b,a);}
Intersection3 intersection(Line3d a,Segment3d b){return point_segment_or_none(CGAL::intersection(l(a),s(b)));}
Intersection3 intersection(Segment3d a,Line3d b){return intersection(b,a);}
Intersection3 intersection(Plane3d a,Plane3d b){return line_plane_or_none(CGAL::intersection(plane(a),plane(b)));}
Intersection3 intersection(Plane3d a,Segment3d b){return point_segment_or_none(CGAL::intersection(plane(a),s(b)));}
Intersection3 intersection(Segment3d a,Plane3d b){return intersection(b,a);}
Intersection3 intersection(Segment3d a,Segment3d b){return point_segment_or_none(CGAL::intersection(s(a),s(b)));}
} // namespace phoenix::scripting
