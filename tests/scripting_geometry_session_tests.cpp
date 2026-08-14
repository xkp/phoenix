#include "phoenix/scripting/geometry_session.hpp"

#include <array>
#include <iostream>

namespace {
phoenix::CanonicalGeometryRef triangle()
{
    using namespace phoenix;
    return CanonicalGeometry::create(
        {{{0,0,0},VertexId{1},0},{{1,0,0},VertexId{2},1},{{0,1,0},VertexId{3},2}},
        {{0,0,1,2,invalid_geometry_index,invalid_geometry_index,HalfedgeId{10},EdgeId{20},LabelId{31}},
         {1,0,2,0,invalid_geometry_index,invalid_geometry_index,HalfedgeId{11},EdgeId{21},LabelId{32}},
         {2,0,0,1,invalid_geometry_index,invalid_geometry_index,HalfedgeId{12},EdgeId{22},LabelId{33}}},
        {{0,FaceId{40},LabelId{70}}});
}

bool commits_private_edits()
{
    using namespace phoenix; using namespace phoenix::scripting;
    RunElementIdAllocator ids{100}; GeometryEditSession session{9,&ids};
    const auto geometry=std::get<GeometryHandle>(session.clone(triangle()));
    const auto vertex=std::get<VertexHandle>(session.vertex(geometry,0));
    const auto face=std::get<FaceHandle>(session.face(geometry,0));
    if(session.set_point(vertex,{2,3,4})||session.set_label(face,LabelId{88}))return false;
    const auto committed=session.commit({geometry});
    return committed.success()&&committed.outputs.size()==1
        &&committed.outputs[0]->vertices()[0].point.x==2
        &&committed.outputs[0]->faces()[0].label==LabelId{88}
        &&triangle()->vertices()[0].point.x==0;
}

bool topology_changes_stale_handles()
{
    using namespace phoenix; using namespace phoenix::scripting;
    RunElementIdAllocator ids{100}; GeometryEditSession session{10,&ids};
    const auto geometry=std::get<GeometryHandle>(session.create_empty());
    const auto a=std::get<VertexHandle>(session.add_vertex(geometry,{0,0,0}));
    const auto b=std::get<VertexHandle>(session.add_vertex(geometry,{1,0,0}));
    const auto c=std::get<VertexHandle>(session.add_vertex(geometry,{0,1,0}));
    const auto old=std::get<VertexHandle>(session.vertex(geometry,0));
    const auto added=session.add_face(geometry,{a,b,c});
    const auto stale=session.point(old);
    return std::holds_alternative<FaceHandle>(added)&&std::holds_alternative<SessionError>(stale)
        &&std::get<SessionError>(stale).code==SessionErrorCode::stale_handle;
}

bool invalid_output_rolls_back_atomically()
{
    using namespace phoenix; using namespace phoenix::scripting;
    GeometryEditSession session{11};
    const auto good=std::get<GeometryHandle>(session.clone(triangle()));
    const GeometryHandle bad{11,999};
    const auto committed=session.commit({good,bad});
    return !committed.success()&&committed.outputs.empty()&&session.closed();
}

bool untouched_clone_reuses_canonical_source()
{
    using namespace phoenix; using namespace phoenix::scripting;
    const auto source=triangle(); GeometryEditSession session{12};
    const auto geometry=std::get<GeometryHandle>(session.clone(source));
    const auto committed=session.commit({geometry});
    return committed.success()&&committed.outputs.size()==1
        &&committed.outputs[0].get()==source.get();
}

bool builds_labeled_cgal_face()
{
    using namespace phoenix; using namespace phoenix::scripting;
    RunElementIdAllocator ids{200}; GeometryEditSession session{13,&ids};
    const auto geometry=std::get<GeometryHandle>(session.create_empty());
    const auto a=std::get<VertexHandle>(session.add_vertex(geometry,{0,0,0}));
    const auto b=std::get<VertexHandle>(session.add_vertex(geometry,{1,0,0}));
    const auto c=std::get<VertexHandle>(session.add_vertex(geometry,{0,1,0}));
    const auto face=session.add_face(geometry,{a,b,c},LabelId{70},
        {LabelId{31},LabelId{32},LabelId{33}});
    if(!std::holds_alternative<FaceHandle>(face))return false;
    const auto committed=session.commit({geometry});
    if(!committed.success()||committed.outputs.size()!=1)return false;
    const auto& output=*committed.outputs[0];
    if(output.vertices().size()!=3||output.faces().size()!=1
        ||output.faces()[0].label!=LabelId{70})return false;
    for(const auto& edge:output.halfedges()){
        const auto& point=output.vertices()[edge.origin_vertex].point;
        const auto expected=point.x==0&&point.y==0?LabelId{31}
            :point.x==1?LabelId{32}:LabelId{33};
        if(edge.label!=expected)return false;
    }
    return true;
}

bool splits_and_rejoins_facet()
{
    using namespace phoenix; using namespace phoenix::scripting;
    RunElementIdAllocator ids{300}; GeometryEditSession session{14,&ids};
    const auto geometry=std::get<GeometryHandle>(session.create_empty());
    const auto a=std::get<VertexHandle>(session.add_vertex(geometry,{0,0,0}));
    const auto b=std::get<VertexHandle>(session.add_vertex(geometry,{1,0,0}));
    const auto c=std::get<VertexHandle>(session.add_vertex(geometry,{1,1,0}));
    const auto d=std::get<VertexHandle>(session.add_vertex(geometry,{0,1,0}));
    const auto face=std::get<FaceHandle>(session.add_face(geometry,{a,b,c,d},LabelId{77}));
    const auto first=std::get<HalfedgeHandle>(session.halfedge(face));
    const auto second=std::get<HalfedgeHandle>(session.next(
        std::get<HalfedgeHandle>(session.next(first))));
    const auto diagonal=session.split_facet(first,second);
    if(!std::holds_alternative<HalfedgeHandle>(diagonal))return false;
    const auto joined=session.join_facet(std::get<HalfedgeHandle>(diagonal));
    if(!std::holds_alternative<HalfedgeHandle>(joined))return false;
    const auto committed=session.commit({geometry});
    return committed.success()&&committed.outputs.size()==1
        &&committed.outputs[0]->vertices().size()==4
        &&committed.outputs[0]->faces().size()==1
        &&committed.outputs[0]->faces()[0].label==LabelId{77};
}

bool splits_edge_with_cgal()
{
    using namespace phoenix; using namespace phoenix::scripting;
    RunElementIdAllocator ids{400}; GeometryEditSession session{15,&ids};
    const auto geometry=std::get<GeometryHandle>(session.clone(triangle()));
    const auto face=std::get<FaceHandle>(session.face(geometry,0));
    const auto edge=std::get<HalfedgeHandle>(session.halfedge(face));
    const auto split=session.split_edge(edge);
    if(!std::holds_alternative<HalfedgeHandle>(split))return false;
    const auto committed=session.commit({geometry});
    return committed.success()&&committed.outputs.size()==1
        &&committed.outputs[0]->vertices().size()==4
        &&committed.outputs[0]->faces().size()==1
        &&committed.outputs[0]->halfedges().size()==4;
}

bool inspects_open_mesh_with_production_counts()
{
    using namespace phoenix; using namespace phoenix::scripting;
    GeometryEditSession session{16};
    const auto geometry=std::get<GeometryHandle>(session.clone(triangle()));
    if(std::get<bool>(session.empty(geometry))
        ||std::get<std::size_t>(session.vertex_count(geometry))!=3
        ||std::get<std::size_t>(session.halfedge_count(geometry))!=6
        ||std::get<std::size_t>(session.face_count(geometry))!=1
        ||std::get<std::size_t>(session.border_halfedge_count(geometry))!=3
        ||std::get<std::size_t>(session.border_edge_count(geometry))!=3
        ||std::get<bool>(session.is_closed(geometry))
        ||!std::get<bool>(session.is_pure_bivalent(geometry))
        ||!std::get<bool>(session.is_pure_triangle(geometry))
        ||std::get<bool>(session.is_pure_quad(geometry)))return false;
    const auto face=std::get<std::vector<FaceHandle>>(session.faces(geometry)).front();
    const auto ring=std::get<std::vector<HalfedgeHandle>>(session.incident_halfedges(face));
    if(ring.size()!=3||std::get<std::size_t>(session.degree(face))!=3
        ||std::get<std::size_t>(session.facet_degree(ring.front()))!=3
        ||std::get<bool>(session.is_border(ring.front()))
        ||!std::get<bool>(session.is_border_edge(ring.front())))return false;
    const auto border=std::get<HalfedgeHandle>(session.opposite(ring.front()));
    if(!std::get<bool>(session.is_border(border))
        ||std::get<std::optional<FaceHandle>>(session.face(border)).has_value())return false;
    const auto target=std::get<VertexHandle>(session.target(ring.front()));
    const auto incident=std::get<std::vector<HalfedgeHandle>>(session.incident_halfedges(target));
    if(incident.size()!=2||std::get<std::size_t>(session.degree(target))!=2
        ||std::get<std::size_t>(session.vertex_degree(ring.front()))!=2)return false;
    const auto around=std::get<HalfedgeHandle>(session.next_on_vertex(ring.front()));
    return std::get<HalfedgeHandle>(session.prev_on_vertex(around)).index==ring.front().index;
}

bool flips_interior_triangle_edge()
{
    using namespace phoenix;using namespace phoenix::scripting;
    RunElementIdAllocator ids{500};GeometryEditSession session{17,&ids};
    const auto geometry=std::get<GeometryHandle>(session.create_empty());
    session.add_vertex(geometry,{0,0,0});session.add_vertex(geometry,{1,0,0});
    session.add_vertex(geometry,{1,1,0});session.add_vertex(geometry,{0,1,0});
    auto v=std::get<std::vector<VertexHandle>>(session.vertices(geometry));
    if(!std::holds_alternative<FaceHandle>(session.add_face(geometry,{v[0],v[1],v[2]},LabelId{81})))return false;
    v=std::get<std::vector<VertexHandle>>(session.vertices(geometry));
    if(!std::holds_alternative<FaceHandle>(session.add_face(geometry,{v[0],v[2],v[3]},LabelId{82})))return false;
    const auto edges=std::get<std::vector<HalfedgeHandle>>(session.halfedges(geometry));
    std::optional<HalfedgeHandle> interior;
    for(const auto h:edges)if(!std::get<bool>(session.is_border_edge(h))){interior=h;break;}
    if(!interior||!std::holds_alternative<HalfedgeHandle>(session.flip_edge(*interior)))return false;
    const auto committed=session.commit({geometry});
    return committed.success()&&committed.outputs[0]->vertices().size()==4
        &&committed.outputs[0]->faces().size()==2;
}

bool removes_and_fills_closed_face()
{
    using namespace phoenix;using namespace phoenix::scripting;
    RunElementIdAllocator ids{600};GeometryEditSession session{18,&ids};
    const auto geometry=std::get<GeometryHandle>(session.create_empty());
    session.add_vertex(geometry,{0,0,0});session.add_vertex(geometry,{1,0,0});
    session.add_vertex(geometry,{0,1,0});session.add_vertex(geometry,{0,0,1});
    const std::array<std::array<std::size_t,3>,4> loops{{{{0,2,1}},{{0,1,3}},{{1,2,3}},{{2,0,3}}}};
    for(std::size_t i=0;i<loops.size();++i){const auto v=std::get<std::vector<VertexHandle>>(session.vertices(geometry));
        if(!std::holds_alternative<FaceHandle>(session.add_face(geometry,
            {v[loops[i][0]],v[loops[i][1]],v[loops[i][2]]},LabelId{static_cast<std::int32_t>(90+i)})))return false;}
    if(!std::get<bool>(session.is_closed(geometry))){std::cerr<<"tetra not closed\n";return false;}
    const auto selected=std::get<std::vector<FaceHandle>>(session.faces(geometry)).front();
    const auto boundary=std::get<HalfedgeHandle>(session.halfedge(selected));
    const auto hole=session.make_hole(boundary);const auto faces_after=std::get<std::size_t>(session.face_count(geometry));
    const auto border_after=std::get<std::size_t>(session.border_halfedge_count(geometry));
    if(!std::holds_alternative<HalfedgeHandle>(hole)||faces_after!=3||border_after!=3){std::cerr<<"make hole failed/counts "<<std::holds_alternative<HalfedgeHandle>(hole)<<' '<<faces_after<<' '<<border_after<<"\n";return false;}
    const auto filled=session.fill_hole(std::get<HalfedgeHandle>(hole));
    if(!std::holds_alternative<HalfedgeHandle>(filled)||!std::get<bool>(session.is_closed(geometry))){std::cerr<<"fill failed/not closed\n";return false;}
    const auto new_face=std::get<std::optional<FaceHandle>>(session.face(std::get<HalfedgeHandle>(filled)));
    if(!new_face||std::get<LabelId>(session.label(*new_face))!=unassigned_label_id){std::cerr<<"fill label failed\n";return false;}
    const auto committed=session.commit({geometry});
    if(!committed.success()){std::cerr<<"hole commit failed: "<<(committed.errors.empty()?"none":committed.errors[0].message)<<'\n';return false;}
    return committed.outputs[0]->faces().size()==4;
}

bool creates_and_erases_center_vertex()
{
    using namespace phoenix;using namespace phoenix::scripting;
    RunElementIdAllocator ids{700};GeometryEditSession session{19,&ids};
    const auto geometry=std::get<GeometryHandle>(session.create_empty());
    session.add_vertex(geometry,{0,0,0});session.add_vertex(geometry,{2,0,0});
    session.add_vertex(geometry,{2,2,0});session.add_vertex(geometry,{0,2,0});
    const auto v=std::get<std::vector<VertexHandle>>(session.vertices(geometry));
    const auto face=std::get<FaceHandle>(session.add_face(geometry,v,LabelId{101}));
    const auto edge=std::get<HalfedgeHandle>(session.halfedge(face));
    const auto centered=session.create_center_vertex(edge);
    if(!std::holds_alternative<HalfedgeHandle>(centered)
        ||std::get<std::size_t>(session.vertex_count(geometry))!=5
        ||std::get<std::size_t>(session.face_count(geometry))!=4)return false;
    const auto center=std::get<VertexHandle>(session.target(std::get<HalfedgeHandle>(centered)));
    if(session.set_point(center,{1,1,0}))return false;
    const auto erased=session.erase_center_vertex(std::get<HalfedgeHandle>(centered));
    if(!std::holds_alternative<HalfedgeHandle>(erased)
        ||std::get<std::size_t>(session.vertex_count(geometry))!=4
        ||std::get<std::size_t>(session.face_count(geometry))!=1)return false;
    const auto committed=session.commit({geometry});
    return committed.success()&&committed.outputs[0]->faces()[0].label==LabelId{101};
}

bool splits_and_rejoins_vertex()
{
    using namespace phoenix;using namespace phoenix::scripting;
    RunElementIdAllocator ids{800};GeometryEditSession session{20,&ids};
    const auto geometry=std::get<GeometryHandle>(session.create_empty());
    session.add_vertex(geometry,{0,0,0});session.add_vertex(geometry,{2,0,0});
    session.add_vertex(geometry,{2,2,0});session.add_vertex(geometry,{0,2,0});
    const auto v=std::get<std::vector<VertexHandle>>(session.vertices(geometry));
    const auto face=std::get<FaceHandle>(session.add_face(geometry,v,LabelId{111}));
    const auto centered=std::get<HalfedgeHandle>(session.create_center_vertex(
        std::get<HalfedgeHandle>(session.halfedge(face))));
    const auto center=std::get<VertexHandle>(session.target(centered));
    const auto ring=std::get<std::vector<HalfedgeHandle>>(session.incident_halfedges(center));
    if(ring.size()!=4)return false;
    const auto split=session.split_vertex(ring[0],ring[2]);
    if(!std::holds_alternative<HalfedgeHandle>(split)
        ||std::get<std::size_t>(session.vertex_count(geometry))!=6)return false;
    const auto joined=session.join_vertex(std::get<HalfedgeHandle>(split));
    if(!std::holds_alternative<HalfedgeHandle>(joined)
        ||std::get<std::size_t>(session.vertex_count(geometry))!=5)return false;
    const auto committed=session.commit({geometry});
    return committed.success()&&committed.outputs[0]->faces().size()==4;
}

bool erases_facet_and_component()
{
    using namespace phoenix;using namespace phoenix::scripting;
    RunElementIdAllocator ids{900};GeometryEditSession session{21,&ids};
    const auto geometry=std::get<GeometryHandle>(session.create_empty());
    for(const auto p:std::array<Point3d,6>{{{0,0,0},{1,0,0},{0,1,0},{3,0,0},{4,0,0},{3,1,0}}})session.add_vertex(geometry,p);
    auto v=std::get<std::vector<VertexHandle>>(session.vertices(geometry));
    session.add_face(geometry,{v[0],v[1],v[2]},LabelId{121});
    v=std::get<std::vector<VertexHandle>>(session.vertices(geometry));
    session.add_face(geometry,{v[3],v[4],v[5]},LabelId{122});
    const auto faces=std::get<std::vector<FaceHandle>>(session.faces(geometry));
    if(session.erase_connected_component(std::get<HalfedgeHandle>(session.halfedge(faces[0])))
        ||std::get<std::size_t>(session.face_count(geometry))!=1
        ||std::get<std::size_t>(session.vertex_count(geometry))!=3)return false;
    const auto remaining=std::get<std::vector<FaceHandle>>(session.faces(geometry)).front();
    if(session.erase_facet(std::get<HalfedgeHandle>(session.halfedge(remaining)))
        ||!std::get<bool>(session.empty(geometry)))return false;
    const auto committed=session.commit({geometry});return committed.success()
        &&committed.outputs[0]->vertices().empty()&&committed.outputs[0]->faces().empty();
}

bool keeps_largest_component_and_clears()
{
    using namespace phoenix;using namespace phoenix::scripting;
    RunElementIdAllocator ids{1000};GeometryEditSession session{22,&ids};
    const auto geometry=std::get<GeometryHandle>(session.create_empty());
    for(const auto p:std::array<Point3d,7>{{{0,0,0},{1,0,0},{1,1,0},{0,1,0},{3,0,0},{4,0,0},{3,1,0}}})session.add_vertex(geometry,p);
    auto v=std::get<std::vector<VertexHandle>>(session.vertices(geometry));session.add_face(geometry,{v[0],v[1],v[2]});
    v=std::get<std::vector<VertexHandle>>(session.vertices(geometry));session.add_face(geometry,{v[0],v[2],v[3]});
    v=std::get<std::vector<VertexHandle>>(session.vertices(geometry));session.add_face(geometry,{v[4],v[5],v[6]});
    const auto removed=session.keep_largest_connected_components(geometry,1);
    if(!std::holds_alternative<std::size_t>(removed)||std::get<std::size_t>(removed)!=1
        ||std::get<std::size_t>(session.face_count(geometry))!=2
        ||std::get<std::size_t>(session.vertex_count(geometry))!=4)return false;
    if(session.clear(geometry)||!std::get<bool>(session.empty(geometry)))return false;
    return session.commit({geometry}).success();
}

bool reverses_orientation()
{
    using namespace phoenix;using namespace phoenix::scripting;
    GeometryEditSession session{23};const auto geometry=std::get<GeometryHandle>(session.clone(triangle()));
    if(session.inside_out(geometry))return false;const auto committed=session.commit({geometry});
    if(!committed.success()||committed.outputs[0]->faces().size()!=1)return false;
    const auto& g=*committed.outputs[0];const auto& h0=g.halfedges()[g.faces()[0].halfedge];
    const auto& h1=g.halfedges()[h0.next];const auto& h2=g.halfedges()[h1.next];
    const auto p0=g.vertices()[h0.origin_vertex].point,p1=g.vertices()[h1.origin_vertex].point,p2=g.vertices()[h2.origin_vertex].point;
    const auto z=(p1.x-p0.x)*(p2.y-p0.y)-(p1.y-p0.y)*(p2.x-p0.x);
    return z<0;
}

bool grows_border_with_facet()
{
    using namespace phoenix;using namespace phoenix::scripting;
    RunElementIdAllocator ids{1100};GeometryEditSession session{24,&ids};
    const auto geometry=std::get<GeometryHandle>(session.clone(triangle()));
    const auto first=std::get<std::vector<HalfedgeHandle>>(session.border_halfedges(geometry)).front();
    const auto second=std::get<HalfedgeHandle>(session.next(
        std::get<HalfedgeHandle>(session.next(first))));
    const auto created=session.add_facet_to_border(first,second);
    if(!std::holds_alternative<HalfedgeHandle>(created)
        ||std::get<std::size_t>(session.face_count(geometry))!=2)return false;
    const auto added_face=std::get<std::optional<FaceHandle>>(session.face(std::get<HalfedgeHandle>(created)));
    if(!added_face||std::get<LabelId>(session.label(*added_face))!=unassigned_label_id)return false;
    return session.commit({geometry}).success();
}

bool grows_border_with_vertex_and_facet()
{
    using namespace phoenix;using namespace phoenix::scripting;
    RunElementIdAllocator ids{1200};GeometryEditSession session{25,&ids};
    const auto geometry=std::get<GeometryHandle>(session.clone(triangle()));
    const auto first=std::get<std::vector<HalfedgeHandle>>(session.border_halfedges(geometry)).front();
    const auto second=std::get<HalfedgeHandle>(session.next(first));
    const auto created=session.add_vertex_and_facet_to_border(first,second);
    if(!std::holds_alternative<HalfedgeHandle>(created)
        ||std::get<std::size_t>(session.vertex_count(geometry))!=4
        ||std::get<std::size_t>(session.face_count(geometry))!=2)return false;
    const auto added_vertex=std::get<VertexHandle>(session.target(std::get<HalfedgeHandle>(created)));
    if(session.set_point(added_vertex,{2,2,0}))return false;
    return session.commit({geometry}).success();
}
}

int main(){const bool a=commits_private_edits(),b=topology_changes_stale_handles(),c=invalid_output_rolls_back_atomically(),d=untouched_clone_reuses_canonical_source(),e=builds_labeled_cgal_face(),f=splits_and_rejoins_facet(),g=splits_edge_with_cgal(),h=inspects_open_mesh_with_production_counts(),i=flips_interior_triangle_edge(),j=removes_and_fills_closed_face(),k=creates_and_erases_center_vertex(),l=splits_and_rejoins_vertex(),m=erases_facet_and_component(),n=keeps_largest_component_and_clears(),o=reverses_orientation(),p=grows_border_with_facet(),q=grows_border_with_vertex_and_facet();
std::cout<<"script geometry private commit: "<<a<<'\n'<<"script stale topology handles: "<<b<<'\n'<<"script atomic invalid rollback: "<<c<<'\n'<<"script lazy untouched passthrough: "<<d<<'\n'<<"script CGAL labeled face builder: "<<e<<'\n'<<"script CGAL split/join facet: "<<f<<'\n'<<"script CGAL split edge: "<<g<<'\n'<<"script production mesh inspection: "<<h<<'\n'<<"script CGAL flip edge: "<<i<<'\n'<<"script CGAL hole round trip: "<<j<<'\n'<<"script CGAL center round trip: "<<k<<'\n'<<"script CGAL split/join vertex: "<<l<<'\n'<<"script CGAL erase topology: "<<m<<'\n'<<"script CGAL component retention: "<<n<<'\n'<<"script CGAL inside out: "<<o<<'\n'<<"script CGAL border facet: "<<p<<'\n'<<"script CGAL border vertex/facet: "<<q<<'\n';return a&&b&&c&&d&&e&&f&&g&&h&&i&&j&&k&&l&&m&&n&&o&&p&&q?0:1;}
