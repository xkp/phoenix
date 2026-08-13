#include "phoenix/instance/placement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace phoenix::instance {
namespace {

struct V { double x, y, z; };
V operator+(V a, V b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
V operator-(V a, V b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
V operator*(V a, double s) { return {a.x*s,a.y*s,a.z*s}; }
double dot(V a,V b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
V cross(V a,V b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
double length(V a) { return std::sqrt(dot(a,a)); }
std::optional<V> normalized(V a) { const auto l=length(a); if(l<=1e-16)return std::nullopt; return a*(1.0/l); }
V vector(Point3d p) { return {p.x,p.y,p.z}; }
Point3d point(V p) { return {p.x,p.y,p.z}; }

Quaternion normalized(Quaternion q)
{
    const auto l=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
    return l<=1e-16?Quaternion{}:Quaternion{q.w/l,q.x/l,q.y/l,q.z/l};
}
Quaternion multiply(Quaternion a,Quaternion b)
{
    return normalized({a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
        a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w});
}
Quaternion axis_angle(V axis,double radians)
{
    const auto n=normalized(axis); if(!n)return {};
    const auto h=radians*0.5,s=std::sin(h); return normalized({std::cos(h),n->x*s,n->y*s,n->z*s});
}
Quaternion between(V from,V to)
{
    const auto a=normalized(from),b=normalized(to); if(!a||!b)return {};
    const auto d=std::clamp(dot(*a,*b),-1.0,1.0);
    if(d>1.0-1e-12)return {};
    if(d<-1.0+1e-12) {
        auto axis=normalized(cross(*a,{1,0,0})); if(!axis)axis=normalized(cross(*a,{0,0,1}));
        return axis_angle(*axis,3.14159265358979323846);
    }
    const auto c=cross(*a,*b); return normalized({1.0+d,c.x,c.y,c.z});
}
V rotate(Quaternion q,V v)
{
    const V u{q.x,q.y,q.z}; return u*(2.0*dot(u,v))+v*(q.w*q.w-dot(u,u))+cross(u,v)*(2.0*q.w);
}

bool face_loop(const CanonicalGeometry& source, GeometryIndex face_index,
    std::vector<GeometryIndex>& halfedges, std::vector<Point3d>& points)
{
    const auto first=source.faces()[face_index].halfedge; if(first>=source.halfedges().size())return false;
    auto h=first;
    do { if(h>=source.halfedges().size())return false; const auto& edge=source.halfedges()[h];
        if(edge.face!=face_index||edge.origin_vertex>=source.vertices().size())return false;
        halfedges.push_back(h); points.push_back(source.vertices()[edge.origin_vertex].point);
        h=edge.next; if(halfedges.size()>source.halfedges().size())return false;
    } while(h!=first);
    return points.size()>=3;
}

std::optional<V> normal(const std::vector<Point3d>& points)
{
    V n{}; for(std::size_t i=0;i<points.size();++i){const auto a=vector(points[i]);const auto b=vector(points[(i+1)%points.size()]);
        n.x+=(a.y-b.y)*(a.z+b.z);n.y+=(a.z-b.z)*(a.x+b.x);n.z+=(a.x-b.x)*(a.y+b.y);}
    return normalized(n);
}

V center(const std::vector<Point3d>& points,FacePosition mode)
{
    if(mode==FacePosition::centroid){V c{};for(auto p:points)c=c+vector(p);return c*(1.0/points.size());}
    V lo{std::numeric_limits<double>::max(),std::numeric_limits<double>::max(),std::numeric_limits<double>::max()};
    V hi{-lo.x,-lo.y,-lo.z};for(auto p:points){auto v=vector(p);lo.x=std::min(lo.x,v.x);lo.y=std::min(lo.y,v.y);lo.z=std::min(lo.z,v.z);hi.x=std::max(hi.x,v.x);hi.y=std::max(hi.y,v.y);hi.z=std::max(hi.z,v.z);}return (lo+hi)*0.5;
}

} // namespace

PlacementResult build_placements(const CanonicalGeometry& source,const PlacementOptions& options)
{
    PlacementResult result;
    if(options.orientation!=OrientationMethod::axis_aligned){result.diagnostics.push_back("Production byFace instance orientation is unsupported.");return result;}
    constexpr double pi=3.14159265358979323846;
    for(GeometryIndex fi=0;fi<source.faces().size();++fi){
        std::vector<GeometryIndex> edges;std::vector<Point3d> points;
        if(!face_loop(source,fi,edges,points)){result.diagnostics.push_back("Invalid instance placement face loop.");return result;}
        const auto face_normal=normal(points);if(!face_normal){result.diagnostics.push_back("Degenerate instance placement face.");return result;}
        V edge_axis{1,0,0};bool found=false;
        if(options.orientation_label){for(std::size_t i=0;i<edges.size();++i)if(source.halfedges()[edges[i]].label==*options.orientation_label){
            const auto direction=normalized(vector(points[(i+1)%points.size()])-vector(points[i]));if(direction){edge_axis=*direction;found=true;}break;}
            if(!found){result.diagnostics.push_back("Instance orientation label was not found on the face.");return result;}}
        auto rotation=between({0,1,0},*face_normal);
        if(found){const auto local_edge=rotate({rotation.w,-rotation.x,-rotation.y,-rotation.z},edge_axis);rotation=multiply(rotation,between({1,0,0},local_edge));}
        if(found||!options.preserve_production_rotation_gate){rotation=multiply(rotation,axis_angle({1,0,0},options.rotation_degrees.x*pi/180.0));rotation=multiply(rotation,axis_angle({0,1,0},options.rotation_degrees.y*pi/180.0));rotation=multiply(rotation,axis_angle({0,0,1},options.rotation_degrees.z*pi/180.0));}
        const auto origin=center(points,options.position);const auto axis_z=cross(edge_axis,*face_normal);
        const auto translated=origin+edge_axis*options.translation.x+*face_normal*options.translation.y+axis_z*options.translation.z;
        result.placements.push_back({source.faces()[fi].id,point(translated),rotation,options.scale});
    }
    return result;
}

Point3d quaternion_to_euler_xyz(const Quaternion& q0)
{
    const auto q = normalized(q0);
    const auto sin_x = 2.0 * (q.w * q.x + q.y * q.z);
    const auto cos_x = 1.0 - 2.0 * (q.x * q.x + q.y * q.y);
    const auto sin_y = std::clamp(2.0 * (q.w * q.y - q.z * q.x), -1.0, 1.0);
    const auto sin_z = 2.0 * (q.w * q.z + q.x * q.y);
    const auto cos_z = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    return {std::atan2(sin_x, cos_x), std::asin(sin_y), std::atan2(sin_z, cos_z)};
}

} // namespace phoenix::instance
