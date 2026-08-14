#include "phoenix/scripting/conformance.hpp"
#include "phoenix/scripting/invocation_host.hpp"
#include "phoenix/scripting/quickjs_engine.hpp"

#include <iostream>

namespace {

class Cancelled final : public phoenix::scripting::CancellationToken {
public:
    [[nodiscard]] bool cancelled() const noexcept override { return true; }
};

bool passes_conformance_corpus()
{
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    for (const auto& test : version_one_conformance_corpus()) {
        EvaluationRequest request;
        request.program = test.program;
        request.global_bindings = test.global_bindings;
        request.local_bindings = test.local_bindings;
        request.limits.instruction_budget = 4096;
        const auto result = engine.evaluate(request);
        if (result.status != test.expected_status ||
            (test.expected_value && result.value != test.expected_value) ||
            (test.expected_diagnostic && (result.diagnostics.empty() ||
                result.diagnostics.front().code != *test.expected_diagnostic))) {
            std::cerr << "conformance failure: " << test.id << '\n';
            return false;
        }
    }
    return true;
}

bool isolates_invocations()
{
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    EvaluationRequest first;
    first.program.source = "(globalThis.leak = 7, 1)";
    EvaluationRequest second;
    second.program.source = "globalThis.leak";
    return engine.evaluate(first).success() &&
        engine.evaluate(second).status == EvaluationStatus::failed;
}

bool honors_pre_cancelled_request()
{
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    EvaluationRequest request;
    request.program.source = "1";
    Cancelled cancellation;
    const auto result = engine.evaluate(request, &cancellation);
    return result.status == EvaluationStatus::cancelled &&
        !result.diagnostics.empty() &&
        result.diagnostics.front().code == DiagnosticCode::cancelled;
}

bool enforces_memory_budget()
{
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    EvaluationRequest request;
    request.program.source = "new Array(1000000).fill('xxxxxxxxxxxxxxxx')";
    request.limits.memory_bytes = 256U << 10U;
    const auto result = engine.evaluate(request);
    return result.status == EvaluationStatus::budget_exceeded &&
        !result.diagnostics.empty() &&
        result.diagnostics.front().code == DiagnosticCode::memory_budget_exceeded;
}

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

bool executes_ordered_script_and_finalizes_outputs()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    ScriptRequest request;
    request.bindings = {{"offset",std::int64_t{4}}};
    request.labels = {{"wall",LabelId{70}}};
    request.geometry_inputs = {{"in",triangle(),"actor",{}, {}}};
    request.libraries = {
        {"first",1,1,"globalThis.libraryValue = 2;"},
        {"second",1,2,"globalThis.libraryValue *= 3;"},
    };
    request.program.source =
        "td.log('building answer');return { answer: libraryValue + td.vars.offset + td.labels.wall + "
        "td.inputs.in.mesh.size_of_vertices + td.input.mesh.size_of_facets };";
    RunElementIdAllocator ids{1000};
    InvocationGeometryHost host{91,request,{{"answer",ScriptOutputKind::scalar}},&ids};
    const auto result=engine.execute_script(request,host);
    const auto committed=host.finalize(result);
    return committed.success()&&committed.outputs.size()==1&&
        committed.outputs.front().scalar==std::optional<Value>{std::int64_t{84}}&&
        result.console_output=="building answer\n";
}

bool enforces_console_budget()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    ScriptRequest request;
    request.program.source="for(let i=0;i<1025;++i)td.log('x');return {};";
    request.limits.instruction_budget=1'000'000;
    RunElementIdAllocator ids{1150};
    InvocationGeometryHost host{97,request,{},&ids};
    const auto result=engine.execute_script(request,host);
    return result.status==EvaluationStatus::failed&&!host.finalize(result).success();
}

bool rejects_duplicate_libraries_before_execution()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    ScriptRequest request;
    request.program.source="return {};";
    request.libraries={{"same",1,1,"globalThis.a=1;"},{"same",2,2,"globalThis.a=2;"}};
    RunElementIdAllocator ids{1100};
    InvocationGeometryHost host{92,request,{},&ids};
    const auto result=engine.execute_script(request,host);
    return result.status==EvaluationStatus::rejected;
}

bool mutates_and_publishes_geometry()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    ScriptRequest request;
    request.geometry_inputs={{"in",triangle(),"actor",{}, {}}};
    request.program.source=
        "const mesh=td.cloneGeometry('in','mesh');"
        "td.setVertex(mesh,0,2,3,4);"
        "td.setFaceLabel(mesh,0,91);"
        "td.setDirectedEdgeLabel(mesh,0,0,92);"
        "const selected=td.elements(mesh,'faces');"
        "const point=td.vertex(mesh,0);const face=td.face(mesh,0);"
        "const check=point.x+td.vertexCount(mesh)+td.faceCount(mesh)+face.label+face.directedEdgeLabels[0];"
        "return {mesh,selected,check};";
    RunElementIdAllocator ids{1200};
    InvocationGeometryHost host{93,request,
        {{"mesh",ScriptOutputKind::geometry},{"selected",ScriptOutputKind::faces},
         {"check",ScriptOutputKind::scalar}},&ids};
    const auto committed=host.finalize(engine.execute_script(request,host));
    if(!committed.success()||committed.outputs.size()!=3)return false;
    const auto& geometry=*committed.outputs[0].geometry;
    return geometry.vertices()[0].point.x==2&&geometry.vertices()[0].point.y==3&&
        geometry.vertices()[0].point.z==4&&geometry.faces()[0].label==LabelId{91}&&
        geometry.halfedges()[0].label==LabelId{92}&&
        committed.outputs[1].element_ids==std::vector<std::uint64_t>{40}&&
        committed.outputs[2].scalar==std::optional<Value>{189.0};
}

bool builds_and_publishes_geometry()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    ScriptRequest request;
    request.program.source=
        "const mesh=build_mesh('mesh');"
        "mesh.add_vertex(0,0,0);mesh.add_vertex(1,0,0);mesh.add_vertex(0,1,0);"
        "mesh.add_face([0,1,2],77,[81,82,83]);return {mesh};";
    RunElementIdAllocator ids{1300};
    InvocationGeometryHost host{94,request,{{"mesh",ScriptOutputKind::geometry}},&ids};
    const auto committed=host.finalize(engine.execute_script(request,host));
    return committed.success()&&committed.outputs.size()==1&&
        committed.outputs[0].geometry->faces().size()==1&&
        committed.outputs[0].geometry->faces()[0].label==LabelId{77};
}

bool rolls_back_failed_script()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    const auto source=triangle();
    ScriptRequest request;
    request.geometry_inputs={{"in",source,"actor"}};
    request.program.source=
        "const mesh=td.cloneGeometry('in','mesh');td.setVertex(mesh,0,9,9,9);"
        "throw new Error('abort');";
    RunElementIdAllocator ids{1400};
    InvocationGeometryHost host{95,request,{{"mesh",ScriptOutputKind::geometry}},&ids};
    const auto result=engine.execute_script(request,host);
    const auto committed=host.finalize(result);
    return result.status==EvaluationStatus::failed&&!committed.success()&&
        source->vertices()[0].point.x==0;
}

bool rejects_stale_script_elements()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    ScriptRequest request;
    request.geometry_inputs={{"in",triangle(),"actor"}};
    request.program.source=
        "const mesh=td.cloneGeometry('in','mesh');const faces=td.elements(mesh,'faces');"
        "td.removeFace(mesh,0);return {faces};";
    RunElementIdAllocator ids{1500};
    InvocationGeometryHost host{96,request,{{"faces",ScriptOutputKind::faces}},&ids};
    return !host.finalize(engine.execute_script(request,host)).success();
}

bool executes_cgal_topology_from_script()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    ScriptRequest request;
    request.geometry_inputs={{"in",triangle(),"actor"}};
    request.program.source=
        "const mesh=td.cloneGeometry('in','mesh');"
        "const halfedges=td.elements(mesh,'halfedges');td.splitEdge(halfedges[0]);"
        "return {mesh,count:td.vertexCount(mesh)};";
    RunElementIdAllocator ids{1600};
    InvocationGeometryHost host{98,request,
        {{"mesh",ScriptOutputKind::geometry},{"count",ScriptOutputKind::scalar}},&ids};
    const auto committed=host.finalize(engine.execute_script(request,host));
    return committed.success()&&committed.outputs.size()==2&&
        committed.outputs[0].geometry->vertices().size()==4&&
        committed.outputs[1].scalar==std::optional<Value>{std::int64_t{4}};
}

bool executes_mesh_management_from_script()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    ScriptRequest request;
    request.geometry_inputs={{"in",triangle(),"actor"}};
    request.program.source=
        "const mesh=td.cloneGeometry('in','mesh');td.insideOut(mesh);td.insideOut(mesh);"
        "td.normalizeBorder(mesh);const removed=td.keepLargestConnectedComponents(mesh,1);"
        "return {mesh,removed};";
    RunElementIdAllocator ids{1700};
    InvocationGeometryHost host{99,request,
        {{"mesh",ScriptOutputKind::geometry},{"removed",ScriptOutputKind::scalar}},&ids};
    const auto committed=host.finalize(engine.execute_script(request,host));
    return committed.success()&&committed.outputs.size()==2&&
        committed.outputs[0].geometry->faces().size()==1&&
        committed.outputs[0].geometry->faces()[0].label==LabelId{70}&&
        committed.outputs[1].scalar==std::optional<Value>{std::int64_t{0}};
}

bool exposes_production_mesh_inspection()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    ScriptRequest request;
    request.geometry_inputs={{"in",triangle(),"actor"}};
    request.program.source=
        "const mesh=td.cloneGeometry('in','mesh'),mi=td.inspectGeometry(mesh);"
        "const vs=td.elements(mesh,'vertices'),hs=td.elements(mesh,'halfedges'),fs=td.elements(mesh,'faces');"
        "const v=td.inspect(vs[0]),h=td.inspect(hs[0]),f=td.inspect(fs[0]);"
        "const hn=td.inspect(h.next),target=td.inspect(h.vertex);"
        "return {mesh,counts:mi.size_of_vertices+mi.size_of_halfedges+mi.size_of_facets,"
        "shape:mi.is_pure_triangle&&f.is_triangle&&f.degree===3,"
        "incidence:v.incident_halfedges.length>0&&f.incident_halfedges.length===3&&hn.kind==='halfedges',"
        "point:v.point.x===v.x&&target.kind==='vertices',label:f.label};";
    RunElementIdAllocator ids{1750};
    InvocationGeometryHost host{101,request,
        {{"mesh",ScriptOutputKind::geometry},{"counts",ScriptOutputKind::scalar},
         {"shape",ScriptOutputKind::scalar},{"incidence",ScriptOutputKind::scalar},
         {"point",ScriptOutputKind::scalar},{"label",ScriptOutputKind::scalar}},&ids};
    const auto committed=host.finalize(engine.execute_script(request,host));
    return committed.success()&&committed.outputs.size()==6&&
        committed.outputs[1].scalar==std::optional<Value>{std::int64_t{10}}&&
        committed.outputs[2].scalar==std::optional<Value>{true}&&
        committed.outputs[3].scalar==std::optional<Value>{true}&&
        committed.outputs[4].scalar==std::optional<Value>{true}&&
        committed.outputs[5].scalar==std::optional<Value>{std::int64_t{70}};
}

bool executes_exact_3d_intersection_from_script()
{
    using namespace phoenix;
    using namespace phoenix::scripting;
    QuickJsEngine engine;
    ScriptRequest request;
    request.program.source=
        "const hit=intersection(line3(point3(0,0,-1),dir3(0,0,5)),plane3(0,0,1,0));"
        "const overlap=intersection(segment3(point3(0,0,0),point3(2,0,0)),"
        "segment3(point3(1,0,0),point3(3,0,0)));"
        "const moved=point3(0,0,0).add(vec3(1,0,0)).transform(transforms.translation(vec3(1,2,3)));"
        "const rayHit=ray3(point3(0,0,0),dir3(2,0,0)).has_on(point3(3,0,0));"
        "const area=triangle3(point3(0,0,0),point3(1,0,0),point3(0,1,0)).squared_area;"
        "const plane=plane3(0,0,1,0),shift=transforms.translation(vec3(0,0,5));"
        "const shifted=plane.transform(shift),restored=shifted.transform(shift.inverse());"
        "const projected=plane.projection(point3(1,2,3));"
        "const support=triangle3(point3(0,0,0),point3(1,0,0),point3(0,1,0)).supporting_plane();"
        "const composed=transforms.translation(vec3(1,0,0)).transform(transforms.translation(vec3(0,2,0)));"
        "const composedPoint=point3(0,0,0).transform(composed);"
        "const segmentDirection=segment3(point3(0,0,0),point3(2,0,0)).direction;"
        "const reversed=dir3(1,0,0).opposite();"
        "const n=normal(point3(0,0,0),point3(1,0,0),point3(0,1,0));"
        "const derived=projected.z===0&&shifted.d===-5&&restored.d===0&&support.c===1&&"
        "plane.oriented_side(point3(0,0,1))===1&&plane.has_on_positive_side(point3(0,0,1))&&"
        "plane.has_on_negative_side(point3(0,0,-1))&&composedPoint.x===1&&composedPoint.y===2&&"
        "shift.m(2,3)===5&&segmentDirection.dx===1&&segmentDirection.vector.x===1&&reversed.dx===-1&&"
        "sqrt(9)===3&&equals(PI,Math.PI)&&constants.positive_side===1&&n.dz===1;"
        "return {hitKind:hit.kind,atOrigin:hit.x===0&&hit.y===0&&hit.z===0,"
        "overlapKind:overlap.kind,overlapStart:overlap.source.x,moved:moved.x+moved.y+moved.z,rayHit,area,derived};";
    RunElementIdAllocator ids{1800};
    InvocationGeometryHost host{100,request,
        {{"hitKind",ScriptOutputKind::scalar},{"atOrigin",ScriptOutputKind::scalar},
         {"overlapKind",ScriptOutputKind::scalar},{"overlapStart",ScriptOutputKind::scalar},
         {"moved",ScriptOutputKind::scalar},{"rayHit",ScriptOutputKind::scalar},
         {"area",ScriptOutputKind::scalar},{"derived",ScriptOutputKind::scalar}},&ids};
    const auto committed=host.finalize(engine.execute_script(request,host));
    return committed.success()&&committed.outputs.size()==8&&
        committed.outputs[0].scalar==std::optional<Value>{std::string{"point3"}}&&
        committed.outputs[1].scalar==std::optional<Value>{true}&&
        committed.outputs[2].scalar==std::optional<Value>{std::string{"segment3"}}&&
        (committed.outputs[3].scalar==std::optional<Value>{std::int64_t{1}}||
         committed.outputs[3].scalar==std::optional<Value>{1.0})&&
        committed.outputs[4].scalar==std::optional<Value>{7.0}&&
        committed.outputs[5].scalar==std::optional<Value>{true}&&
        committed.outputs[6].scalar==std::optional<Value>{0.25}&&
        committed.outputs[7].scalar==std::optional<Value>{true};
}

} // namespace

int main()
{
    const bool conformance = passes_conformance_corpus();
    const bool isolation = isolates_invocations();
    const bool cancellation = honors_pre_cancelled_request();
    const bool memory = enforces_memory_budget();
    const bool script = executes_ordered_script_and_finalizes_outputs();
    const bool console = enforces_console_budget();
    const bool duplicate_libraries = rejects_duplicate_libraries_before_execution();
    const bool mutation = mutates_and_publishes_geometry();
    const bool builder = builds_and_publishes_geometry();
    const bool rollback = rolls_back_failed_script();
    const bool stale = rejects_stale_script_elements();
    const bool topology = executes_cgal_topology_from_script();
    const bool management = executes_mesh_management_from_script();
    const bool inspection = exposes_production_mesh_inspection();
    const bool primitives = executes_exact_3d_intersection_from_script();
    std::cout << "QuickJS expression conformance: " << conformance << '\n'
              << "QuickJS invocation isolation: " << isolation << '\n'
              << "QuickJS cancellation: " << cancellation << '\n'
              << "QuickJS memory budget: " << memory << '\n'
              << "QuickJS ordered script host: " << script << '\n'
              << "QuickJS bounded console: " << console << '\n'
              << "QuickJS duplicate library rejection: " << duplicate_libraries << '\n'
              << "QuickJS geometry mutation/publication: " << mutation << '\n'
              << "QuickJS geometry builder/publication: " << builder << '\n'
              << "QuickJS script rollback: " << rollback << '\n'
              << "QuickJS stale element rejection: " << stale << '\n';
    std::cout << "QuickJS CGAL topology bridge: " << topology << '\n';
    std::cout << "QuickJS mesh management bridge: " << management << '\n';
    std::cout << "QuickJS mesh inspection bridge: " << inspection << '\n';
    std::cout << "QuickJS exact 3D intersection bridge: " << primitives << '\n';
    return conformance && isolation && cancellation && memory && script && console &&
        duplicate_libraries && mutation && builder && rollback && stale && topology &&
        management && inspection && primitives ? 0 : 1;
}
