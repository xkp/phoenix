#pragma once

#include "phoenix/scripting/contract.hpp"
#include "phoenix/scripting/geometry_session.hpp"

#include <map>
#include <set>

namespace phoenix::scripting {

struct ScriptOutputSpec { PortId port; ScriptOutputKind kind=ScriptOutputKind::scalar; };
struct PublishedScriptOutput {
    PortId port;
    ScriptOutputKind kind=ScriptOutputKind::scalar;
    std::optional<Value> scalar;
    CanonicalGeometryRef geometry;
    std::vector<std::uint64_t> element_ids;
};
struct HostCommitResult {
    std::vector<PublishedScriptOutput> outputs;
    std::vector<Diagnostic> diagnostics;
    [[nodiscard]] bool success() const noexcept{return diagnostics.empty();}
};

class InvocationGeometryHost final : public GeometryScriptHost {
public:
    InvocationGeometryHost(std::uint64_t session_id,const ScriptRequest& request,
        std::vector<ScriptOutputSpec> outputs,RunElementIdAllocator* ids);
    [[nodiscard]] std::vector<PortId> input_ports() const override;
    [[nodiscard]] std::size_t vertex_count(const PortId& input) const override;
    [[nodiscard]] std::size_t face_count(const PortId& input) const override;
    [[nodiscard]] Point3d vertex(const PortId& input,std::size_t index) const override;
    [[nodiscard]] ScriptFace face(const PortId& input,std::size_t index) const override;
    [[nodiscard]] std::optional<ScriptOutputKind> input_selection_kind(
        const PortId& input) const override;
    [[nodiscard]] std::vector<std::uint64_t> input_selection_handles(
        const PortId& input) override;
    [[nodiscard]] std::uint64_t create_geometry(const PortId& output) override;
    [[nodiscard]] std::uint64_t clone_geometry(const PortId& input,const PortId& output) override;
    [[nodiscard]] std::size_t geometry_vertex_count(std::uint64_t geometry) const override;
    [[nodiscard]] std::size_t geometry_face_count(std::uint64_t geometry) const override;
    [[nodiscard]] Point3d geometry_vertex(std::uint64_t geometry,std::size_t index) const override;
    [[nodiscard]] ScriptFace geometry_face(std::uint64_t geometry,std::size_t index) const override;
    void set_vertex(std::uint64_t geometry,std::size_t index,Point3d point) override;
    [[nodiscard]] std::size_t add_vertex(std::uint64_t geometry,Point3d point) override;
    void set_face_label(std::uint64_t geometry,std::size_t face,LabelId label) override;
    void set_directed_edge_label(std::uint64_t geometry,std::size_t face,
        std::size_t edge,LabelId label) override;
    void add_face(std::uint64_t geometry,const ScriptFace& face) override;
    void remove_face(std::uint64_t geometry,std::size_t face) override;
    [[nodiscard]] std::vector<std::uint64_t> element_handles(
        std::uint64_t geometry,ScriptOutputKind kind) override;
    [[nodiscard]] ScriptMeshInspection inspect_geometry(std::uint64_t geometry) override;
    [[nodiscard]] ScriptElementInspection inspect_element(std::uint64_t element) override;
    [[nodiscard]] std::uint64_t split_edge(std::uint64_t halfedge) override;
    [[nodiscard]] std::uint64_t split_facet(std::uint64_t first,std::uint64_t second) override;
    [[nodiscard]] std::uint64_t join_facet(std::uint64_t halfedge) override;
    [[nodiscard]] std::uint64_t flip_edge(std::uint64_t halfedge) override;
    [[nodiscard]] std::uint64_t make_hole(std::uint64_t halfedge) override;
    [[nodiscard]] std::uint64_t fill_hole(std::uint64_t halfedge) override;
    [[nodiscard]] std::uint64_t split_vertex(std::uint64_t first,std::uint64_t second) override;
    [[nodiscard]] std::uint64_t join_vertex(std::uint64_t halfedge) override;
    [[nodiscard]] std::uint64_t create_center_vertex(std::uint64_t halfedge) override;
    [[nodiscard]] std::uint64_t erase_center_vertex(std::uint64_t halfedge) override;
    void erase_connected_component(std::uint64_t halfedge) override;
    [[nodiscard]] std::size_t keep_largest_connected_components(
        std::uint64_t geometry,std::size_t count) override;
    void clear_geometry(std::uint64_t geometry) override;
    void inside_out(std::uint64_t geometry) override;
    void normalize_border(std::uint64_t geometry) override;
    [[nodiscard]] std::uint64_t add_vertex_and_facet_to_border(
        std::uint64_t first,std::uint64_t second) override;
    [[nodiscard]] std::uint64_t add_facet_to_border(
        std::uint64_t first,std::uint64_t second) override;
    [[nodiscard]] HostCommitResult finalize(const ScriptResult& result);

private:
    using AnyElement=std::variant<VertexHandle,HalfedgeHandle,FaceHandle>;
    [[nodiscard]] const ScriptGeometryInput& input(const PortId& port) const;
    [[nodiscard]] GeometryHandle geometry(std::uint64_t token) const;
    [[nodiscard]] bool output_matches(const PortId& port,ScriptOutputKind kind) const;
    [[nodiscard]] std::uint64_t issue(AnyElement handle);
    [[nodiscard]] HalfedgeHandle halfedge(std::uint64_t token) const;
    GeometryEditSession session_;
    std::map<PortId,ScriptGeometryInput> inputs_;
    std::map<PortId,ScriptOutputKind> output_specs_;
    std::map<std::uint64_t,GeometryHandle> geometries_;
    std::map<std::uint64_t,PortId> geometry_outputs_;
    std::map<std::uint64_t,AnyElement> elements_;
    std::map<PortId,std::uint64_t> input_geometry_tokens_;
    std::uint64_t next_token_=1;
};

} // namespace phoenix::scripting
