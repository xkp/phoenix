#include "phoenix/scripting/geometry_bindings.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phoenix::scripting {
namespace {

bool matches(LabelId actual, const std::optional<LabelId>& expected)
{
    return !expected || actual == *expected;
}

Point3d source(const CanonicalGeometry& g, GeometryIndex h)
{
    return g.vertices().at(g.halfedges().at(h).origin_vertex).point;
}

Point3d target(const CanonicalGeometry& g, GeometryIndex h)
{
    return source(g, g.halfedges().at(h).next);
}

double length(const CanonicalGeometry& g, GeometryIndex h)
{
    const auto a=source(g,h),b=target(g,h);
    return std::hypot(std::hypot(b.x-a.x,b.y-a.y),b.z-a.z);
}

std::vector<GeometryIndex> face_edges(const CanonicalGeometry& g, GeometryIndex face)
{
    std::vector<GeometryIndex> result;auto h=g.faces().at(face).halfedge;const auto first=h;
    do{result.push_back(h);h=g.halfedges().at(h).next;}while(h!=first);
    return result;
}

double face_area(const CanonicalGeometry& g, GeometryIndex face)
{
    double x=0,y=0,z=0;const auto edges=face_edges(g,face);
    for(const auto h:edges){const auto a=source(g,h),b=target(g,h);x+=(a.y-b.y)*(a.z+b.z);y+=(a.z-b.z)*(a.x+b.x);z+=(a.x-b.x)*(a.y+b.y);}
    return 0.5*std::sqrt(x*x+y*y+z*z);
}

double choose_length(const CanonicalGeometry& g,const std::vector<GeometryIndex>& edges,
    const GeometryBindingSpec& spec,SeedValue seed)
{
    std::vector<double> values;for(const auto h:edges)if(matches(g.halfedges()[h].label,spec.label1))values.push_back(length(g,h));
    if(values.empty())return spec.choice==BindingChoice::shortest?9.0e64:0.0;
    if(spec.choice==BindingChoice::largest)return *std::max_element(values.begin(),values.end());
    if(spec.choice==BindingChoice::shortest)return *std::min_element(values.begin(),values.end());
    return values[static_cast<std::size_t>(seed%values.size())];
}

bool border(const CanonicalGeometry& g,GeometryIndex h){return g.halfedges()[h].opposite==invalid_geometry_index;}
double angle(const CanonicalGeometry& g,GeometryIndex h,const GeometryBindingSpec& spec)
{
    const auto& edge=g.halfedges()[h];if(!matches(edge.label,spec.label1))return 0;
    GeometryIndex other=edge.next;if(spec.relation==BindingRelation::previous)other=edge.previous;
    else if(spec.relation==BindingRelation::any&&spec.label2&& !matches(g.halfedges()[other].label,spec.label2))other=edge.previous;
    if(!matches(g.halfedges()[other].label,spec.label2))return 0;
    const auto pivot=spec.relation==BindingRelation::previous?source(g,h):target(g,h);
    const auto a=spec.relation==BindingRelation::previous?target(g,h):source(g,h);
    const auto b=spec.relation==BindingRelation::previous?source(g,other):target(g,other);
    const double ux=a.x-pivot.x,uy=a.y-pivot.y,uz=a.z-pivot.z,vx=b.x-pivot.x,vy=b.y-pivot.y,vz=b.z-pivot.z;
    const auto denominator=std::sqrt((ux*ux+uy*uy+uz*uz)*(vx*vx+vy*vy+vz*vz));if(denominator==0)return 0;
    constexpr double pi=3.14159265358979323846;return std::acos(std::clamp((ux*vx+uy*vy+uz*vz)/denominator,-1.0,1.0))*180.0/pi;
}

double point_distance(Point3d a,Point3d b){return std::hypot(std::hypot(b.x-a.x,b.y-a.y),b.z-a.z);}
double labeled_distance(const CanonicalGeometry& g,const std::vector<GeometryIndex>& edges,const GeometryBindingSpec& spec,SeedValue seed)
{
    std::vector<GeometryIndex> first,second;for(const auto h:edges){const auto label=g.halfedges()[h].label;if((spec.label1&&label==*spec.label1)||(!spec.label1&&(!spec.label2||label!=*spec.label2)))first.push_back(h);else if((spec.label2&&label==*spec.label2)||!spec.label2)second.push_back(h);}std::vector<double> distances;for(const auto a:first)for(const auto b:second){distances.push_back(point_distance(source(g,a),source(g,b)));distances.push_back(point_distance(source(g,a),target(g,b)));distances.push_back(point_distance(target(g,a),source(g,b)));distances.push_back(point_distance(target(g,a),target(g,b)));}if(distances.empty())return 0;if(spec.choice==BindingChoice::largest)return *std::max_element(distances.begin(),distances.end());if(spec.choice==BindingChoice::shortest)return *std::min_element(distances.begin(),distances.end());std::sort(distances.begin(),distances.end());distances.erase(std::unique(distances.begin(),distances.end()),distances.end());return distances[static_cast<std::size_t>(seed%distances.size())];
}

double evaluate(const CanonicalGeometry& g,BindingElementKind kind,GeometryIndex element,
    const GeometryBindingSpec& spec,SeedValue seed)
{
    const auto edges=kind==BindingElementKind::face?face_edges(g,element):std::vector<GeometryIndex>{element};
    switch(spec.kind){
    case GeometryBindingKind::length:return choose_length(g,edges,spec,seed);
    case GeometryBindingKind::area:return kind==BindingElementKind::face?face_area(g,element):0.0;
    case GeometryBindingKind::border_edge:for(const auto h:edges)if(matches(g.halfedges()[h].label,spec.label1)&&border(g,h))return 1.0;return 0.0;
    case GeometryBindingKind::count_edge_labels:{std::int64_t count=0;for(const auto h:edges)if(matches(g.halfedges()[h].label,spec.label1))++count;return static_cast<double>(count);}
    case GeometryBindingKind::count_face_labels:{std::int64_t count=0;for(const auto& f:g.faces())if(matches(f.label,spec.label1))++count;return static_cast<double>(count);}
    case GeometryBindingKind::opposite_edge:for(const auto h:edges){const auto o=g.halfedges()[h].opposite;if(matches(g.halfedges()[h].label,spec.label1)&&o!=invalid_geometry_index&&matches(g.halfedges()[o].label,spec.label2))return 1.0;}return 0.0;
    case GeometryBindingKind::adjacent:for(const auto h:edges)if(matches(g.halfedges()[h].label,spec.label1)){const auto& e=g.halfedges()[h];if(spec.relation!=BindingRelation::previous&&matches(g.halfedges()[e.next].label,spec.label2))return 1.0;if(spec.relation!=BindingRelation::next&&matches(g.halfedges()[e.previous].label,spec.label2))return 1.0;}return 0.0;
    case GeometryBindingKind::angle:for(const auto h:edges){const auto value=angle(g,h,spec);if(value!=0)return value;}return 0.0;
    case GeometryBindingKind::distance:return kind==BindingElementKind::face?labeled_distance(g,edges,spec,seed):0.0;
    case GeometryBindingKind::opposite_extended:if(kind!=BindingElementKind::face||!spec.label1||(!spec.label3&&!spec.label4))return 0.0;for(const auto h:edges){const auto& e=g.halfedges()[h];if(e.label!=*spec.label1||e.opposite==invalid_geometry_index)continue;const auto& opposite=g.halfedges()[e.opposite];if(matches(g.faces()[e.face].label,spec.label2)&&matches(opposite.label,spec.label3)&&matches(g.faces()[opposite.face].label,spec.label4))return 1.0;}return 0.0;
    }
    return 0.0;
}

} // namespace

std::optional<GeometryBindingInput> resolve_geometry_binding_input(
    const RuntimeValue& value,BindingElementKind kind)
{
    GeometryBindingInput result;if(const auto* geometry=value.as_geometry())result.source=*geometry;
    else if(const auto* selection=value.as_element_selection()){
        if(selection->kind!=selection_kind(kind))return {};result.source=selection->source;
        if(!result.source.geometry)return {};
        const auto count=kind==BindingElementKind::face?result.source.geometry->faces().size():result.source.geometry->halfedges().size();
        for(const auto id:selection->element_ids){bool found=false;for(GeometryIndex i=0;i<count;++i)if(stable_element_id(*result.source.geometry,kind,i)==id){result.elements.push_back(i);found=true;break;}if(!found)return {};}
        return result;
    }else return {};
    if(!result.source.geometry)return {};
    const auto count=kind==BindingElementKind::face?result.source.geometry->faces().size():result.source.geometry->halfedges().size();for(GeometryIndex i=0;i<count;++i)result.elements.push_back(i);return result;
}

std::vector<GeometryBindingInput> resolve_geometry_binding_inputs(const RuntimeValue& value,BindingElementKind kind)
{
    if(const auto single=resolve_geometry_binding_input(value,kind))return {*single};
    std::vector<GeometryBindingInput> result;if(const auto* collection=value.as_geometry_collection())for(const auto& source:collection->contributions){if(!source.geometry)continue;GeometryBindingInput input;input.source=source;const auto count=kind==BindingElementKind::face?source.geometry->faces().size():source.geometry->halfedges().size();for(GeometryIndex i=0;i<count;++i)input.elements.push_back(i);result.push_back(std::move(input));}return result;
}

Bindings evaluate_geometry_bindings(const CanonicalGeometry& geometry,BindingElementKind kind,
    GeometryIndex element,const std::vector<GeometryBindingSpec>& specs,SeedValue seed)
{
    Bindings result;for(const auto& spec:specs){if(spec.variable.empty())throw std::invalid_argument("Geometry binding variable cannot be empty.");if(!result.emplace(spec.variable,evaluate(geometry,kind,element,spec,seed^stable_element_id(geometry,kind,element))).second)throw std::invalid_argument("Duplicate geometry binding variable: "+spec.variable);}return result;
}

GeometryElementKind selection_kind(BindingElementKind kind)noexcept{return kind==BindingElementKind::face?GeometryElementKind::face:GeometryElementKind::halfedge;}
std::uint64_t stable_element_id(const CanonicalGeometry& geometry,BindingElementKind kind,GeometryIndex element){return kind==BindingElementKind::face?geometry.faces().at(element).id.value():geometry.halfedges().at(element).id.value();}

} // namespace phoenix::scripting
