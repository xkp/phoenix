#include "phoenix/execution.hpp"
#include "phoenix/labels.hpp"
#include "phoenix/migration/migrated_instruction_registry.hpp"
#include "phoenix/migration/migrated_package_runtime_loader.hpp"
#include "phoenix/migration/production_migrated_package_io.hpp"
#include "phoenix/working_geometry.hpp"
#include "phoenix/working_geometry_builder.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

struct LegacySegment {
    phoenix::Point3d start;
    phoenix::Point3d end;
    phoenix::LabelId label = phoenix::unassigned_label_id;
};

void print_usage()
{
    std::cerr << "usage: phoenix_run_migrated_package <package.phxmig> [capture_dir]\n";
}

std::set<std::string> instruction_kinds(const phoenix::migration::LoadedMigratedPackage& package)
{
    std::set<std::string> kinds;
    for (const auto& entry : package.functions) {
        for (const auto& instruction : entry.second.graph.instructions) {
            if (instruction.kind != "output" && !instruction.called_function_id.has_value()) {
                kinds.insert(instruction.kind);
            }
        }
    }
    return kinds;
}

std::string extract_string_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*"([^"]*)")regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    return it == std::sregex_iterator{} ? std::string{} : (*it)[1].str();
}

std::optional<double> extract_double_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{
        "\"" + key + R"regex("\s*:\s*(-?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?))regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    if (it == std::sregex_iterator{}) return std::nullopt;
    return std::stod((*it)[1].str());
}

bool extract_bool_value(const std::string& text, const std::string& key, bool default_value)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*(true|false))regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    if (it == std::sregex_iterator{}) return default_value;
    return (*it)[1].str() == "true";
}

std::string extract_array_text(const std::string& text, const std::string& key)
{
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return {};
    const auto array_start = text.find('[', key_pos);
    if (array_start == std::string::npos) return {};

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = array_start; index < text.size(); ++index) {
        const auto ch = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (ch == '[') ++depth;
        if (ch == ']') {
            --depth;
            if (depth == 0) return text.substr(array_start + 1, index - array_start - 1);
        }
    }
    return {};
}

std::vector<std::string> extract_array_items(const std::string& text)
{
    std::vector<std::string> items;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t item_start = std::string::npos;

    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto ch = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;

        if (ch == '[') {
            if (depth == 0) item_start = index;
            ++depth;
            continue;
        }
        if (ch == ']') {
            --depth;
            if (depth == 0 && item_start != std::string::npos) {
                items.push_back(text.substr(item_start, index - item_start + 1));
                item_start = std::string::npos;
            }
            continue;
        }
        if (ch == ',' && depth == 0) {
            continue;
        }
    }

    return items;
}

std::vector<std::string> extract_object_texts(const std::string& text)
{
    std::vector<std::string> objects;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t object_start = std::string::npos;

    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto ch = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (ch == '{') {
            if (depth == 0) object_start = index;
            ++depth;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0 && object_start != std::string::npos) {
                objects.push_back(text.substr(object_start, index - object_start + 1));
                object_start = std::string::npos;
            }
        }
    }

    return objects;
}

std::optional<LegacySegment> parse_legacy_segment(
    const std::string& text,
    const std::map<phoenix::LabelUid, std::int32_t>& label_ids)
{
    const auto objects = extract_object_texts(text);
    if (objects.size() < 3) return std::nullopt;

    const auto x1 = extract_double_value(objects[0], "x");
    const auto y1 = extract_double_value(objects[0], "y");
    const auto x2 = extract_double_value(objects[1], "x");
    const auto y2 = extract_double_value(objects[1], "y");
    if (!x1.has_value() || !y1.has_value() || !x2.has_value() || !y2.has_value()) {
        return std::nullopt;
    }

    LegacySegment segment;
    segment.start = phoenix::point_from_legacy_2d(*x1, *y1);
    segment.end = phoenix::point_from_legacy_2d(*x2, *y2);

    const auto label_uid = extract_string_value(objects[2], "label");
    const auto label_it = label_ids.find(label_uid);
    if (label_it != label_ids.end()) {
        segment.label = phoenix::LabelId{label_it->second};
    }

    return segment;
}

phoenix::CanonicalGeometryRef build_legacy_input_geometry(
    const std::string& text,
    const std::map<phoenix::LabelUid, std::int32_t>& label_ids)
{
    if (!extract_bool_value(text, "closed", false)) {
        return nullptr;
    }

    const auto segments_text = extract_array_text(text, "segments");
    const auto segment_items = extract_array_items(segments_text);
    if (segment_items.size() < 3) {
        return nullptr;
    }

    std::vector<LegacySegment> segments;
    segments.reserve(segment_items.size());
    for (const auto& item : segment_items) {
        const auto segment = parse_legacy_segment(item, label_ids);
        if (!segment.has_value()) {
            return nullptr;
        }
        segments.push_back(*segment);
    }

    phoenix::RunElementIdAllocator ids;
    phoenix::WorkingGeometryBuilder builder(ids);
    std::vector<phoenix::WorkingVertexIndex> vertices;
    vertices.reserve(segments.size());
    for (const auto& segment : segments) {
        vertices.push_back(builder.add_vertex(segment.start));
    }

    const auto face = builder.begin_facet();
    if (face == phoenix::invalid_working_face_index) {
        return nullptr;
    }
    for (const auto vertex : vertices) {
        if (!builder.add_vertex_to_facet(vertex)) {
            return nullptr;
        }
    }
    if (!builder.end_facet()) {
        return nullptr;
    }

    for (std::size_t index = 0; index < segments.size(); ++index) {
        const auto source = vertices[index];
        const auto target = vertices[(index + 1) % vertices.size()];
        builder.set_halfedge_label_by_target(face, target, segments[index].label);
        builder.set_halfedge_id_by_vertices(source, target, ids.next_halfedge());
        builder.set_edge_id_by_vertices(source, target, ids.next_edge());
    }

    const auto working = builder.build();
    if (!working.success) {
        return nullptr;
    }

    return phoenix::SurfaceMeshAdapter{}.demote(working.working).geometry;
}

struct RootInputLoadResult {
    std::vector<phoenix::PortValue> inputs;
    std::size_t decoded_geometry_payloads = 0;
    std::size_t attached_ports = 0;
};

RootInputLoadResult default_root_inputs(
    const phoenix::migration::LoadedMigratedPackage& package,
    const phoenix::migration::MigratedFunctionPackage& root_function)
{
    RootInputLoadResult result;
    for (const auto& instruction : root_function.graph.instructions) {
        if (instruction.kind != "input") {
            continue;
        }

        const auto node_data_it = root_function.instruction_node_data.find(instruction.id);
        if (node_data_it == root_function.instruction_node_data.end()) {
            continue;
        }

        const auto payload_id = extract_string_value(node_data_it->second, "file");
        if (payload_id.empty()) {
            continue;
        }

        const auto payload_it = root_function.payloads.find(payload_id);
        if (payload_it == root_function.payloads.end()) {
            continue;
        }

        const auto geometry = build_legacy_input_geometry(payload_it->second.text, package.label_ids);
        if (geometry == nullptr) {
            continue;
        }
        ++result.decoded_geometry_payloads;

        for (const auto& port : instruction.input_ports) {
            result.inputs.push_back(phoenix::PortValue{
                port.id,
                phoenix::RuntimeValue::geometry(geometry, payload_id),
            });
            ++result.attached_ports;
        }
    }
    return result;
}

bool report_missing_handlers(
    const phoenix::migration::LoadedMigratedPackage& package,
    const phoenix::InstructionRegistry& registry)
{
    bool missing = false;
    for (const auto& kind : instruction_kinds(package)) {
        if (registry.find_handler(kind) == nullptr) {
            std::cerr << "error runtime.missing_handler kind=" << kind << "\n";
            missing = true;
        }
    }
    return missing;
}

phoenix::FunctionLibrary make_function_library(
    const phoenix::migration::LoadedMigratedPackage& package)
{
    phoenix::FunctionLibrary library;
    for (const auto& entry : package.functions) {
        library.register_function(entry.second.graph);
    }
    return library;
}

class ScopeTrace final : public phoenix::FunctionExecutionScopeTraceSink {
public:
    std::size_t record_scope(phoenix::FunctionExecutionScopeRecord scope) override
    {
        records.push_back(std::move(scope));
        return records.size() - 1;
    }

    std::vector<phoenix::FunctionExecutionScopeRecord> records;
};

class InstructionTrace final : public phoenix::FunctionExecutionInstructionTraceSink {
public:
    void record_instruction(phoenix::FunctionExecutionInstructionRecord instruction) override
    {
        records.push_back(std::move(instruction));
    }

    std::vector<phoenix::FunctionExecutionInstructionRecord> records;
};

class PublicationTrace final : public phoenix::FunctionExecutionPublicationTraceSink {
public:
    void record_publication(phoenix::FunctionExecutionPublicationRecord publication) override
    {
        records.push_back(std::move(publication));
    }

    std::vector<phoenix::FunctionExecutionPublicationRecord> records;
};

class DiagnosticsTrace final : public phoenix::FunctionExecutionDiagnosticsSink {
public:
    void record_diagnostics(phoenix::FunctionExecutionDiagnosticsRecord diagnostics) override
    {
        records.push_back(std::move(diagnostics));
    }

    std::vector<phoenix::FunctionExecutionDiagnosticsRecord> records;
};

std::string join_call_path(const phoenix::FunctionCallPath& path)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (index > 0) stream << "/";
        stream << path[index];
    }
    return stream.str();
}

std::string scalar_to_string(const phoenix::LiteralScalar& value)
{
    return std::visit([](const auto& item) {
        std::ostringstream stream;
        stream << item;
        return stream.str();
    }, value);
}

std::string value_summary(const phoenix::RuntimeValue& value)
{
    std::ostringstream stream;
    stream << "presence=" << phoenix::to_string(value.presence);
    if (const auto* geometry = value.as_geometry()) {
        stream << " kind=geometry";
        if (geometry->geometry != nullptr) {
            stream << " faces=" << geometry->geometry->faces().size()
                   << " halfedges=" << geometry->geometry->halfedges().size()
                   << " vertices=" << geometry->geometry->vertices().size()
                   << " fingerprint=" << geometry->geometry->fingerprint();
        }
        if (!geometry->debug_label.empty()) {
            stream << " label=" << geometry->debug_label;
        }
        if (geometry->accumulation_actor_id.has_value()) {
            stream << " actor=" << *geometry->accumulation_actor_id;
        }
        return stream.str();
    }
    if (const auto* collection = value.as_geometry_collection()) {
        stream << " kind=geometry_collection contributions=" << collection->contributions.size();
        return stream.str();
    }
    if (const auto* literal = value.as_literal()) {
        stream << " kind=literal";
        if (const auto* scalar = std::get_if<phoenix::LiteralScalar>(literal)) {
            stream << " value=" << scalar_to_string(*scalar);
        } else {
            const auto* array = std::get_if<phoenix::LiteralArray>(literal);
            stream << " count=" << (array == nullptr ? 0 : array->size());
        }
        return stream.str();
    }
    if (const auto* selection = value.as_element_selection()) {
        stream << " kind=element_selection elements=" << selection->element_ids.size();
        return stream.str();
    }
    if (const auto* def = value.as_default()) {
        stream << " kind=default source_type=" << def->source_type;
        return stream.str();
    }
    if (value.is_empty()) {
        stream << " kind=empty";
    }
    return stream.str();
}

void write_text_file(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary);
    output << text;
}

void write_geometry_file(const std::filesystem::path& path, const phoenix::CanonicalGeometryRef& geometry)
{
    if (geometry == nullptr) return;
    write_text_file(path, geometry->serialize_canonical());
}

void write_obj_file(const std::filesystem::path& path, const phoenix::CanonicalGeometryRef& geometry)
{
    if (geometry == nullptr) return;

    std::ofstream output(path, std::ios::binary);
    const auto& vertices = geometry->vertices();
    const auto& halfedges = geometry->halfedges();
    const auto& faces = geometry->faces();

    output << "# Phoenix debug OBJ\n";
    output << "# vertices=" << vertices.size()
           << " halfedges=" << halfedges.size()
           << " faces=" << faces.size() << "\n";

    for (const auto& vertex : vertices) {
        output << "v " << vertex.point.x << " " << vertex.point.y << " " << vertex.point.z << "\n";
    }

    for (std::size_t face_index = 0; face_index < faces.size(); ++face_index) {
        output << "g face_" << face_index << "\n";
        output << "o face_" << face_index << "\n";

        std::vector<phoenix::GeometryIndex> loop;
        auto current = faces[face_index].halfedge;
        do {
            loop.push_back(current);
            current = halfedges[current].next;
        } while (current != faces[face_index].halfedge);

        output << "f";
        for (const auto halfedge_index : loop) {
            output << " " << (halfedges[halfedge_index].origin_vertex + 1);
        }
        output << "\n";
    }
}

void write_runtime_value_geometry_files(
    const std::filesystem::path& capture_dir,
    const std::string& stem,
    const phoenix::RuntimeValue& value)
{
    if (const auto* geometry = value.as_geometry()) {
        if (geometry->geometry == nullptr) return;
        write_geometry_file(capture_dir / (stem + ".phxgeom"), geometry->geometry);
        write_obj_file(capture_dir / (stem + ".obj"), geometry->geometry);
        return;
    }

    const auto* collection = value.as_geometry_collection();
    if (collection == nullptr) return;
    for (std::size_t index = 0; index < collection->contributions.size(); ++index) {
        if (collection->contributions[index].geometry == nullptr) continue;
        const auto contribution_stem = stem + "_contrib_" + std::to_string(index);
        write_geometry_file(capture_dir / (contribution_stem + ".phxgeom"),
            collection->contributions[index].geometry);
        write_obj_file(capture_dir / (contribution_stem + ".obj"),
            collection->contributions[index].geometry);
    }
}

std::string summarize_port_values(const std::vector<phoenix::PortValue>& values)
{
    std::ostringstream stream;
    for (const auto& value : values) {
        stream << value.port << " " << value_summary(value.value) << "\n";
    }
    return stream.str();
}

std::string summarize_scopes(const ScopeTrace& trace)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < trace.records.size(); ++index) {
        const auto& record = trace.records[index];
        stream << "scope[" << index << "]"
               << " function=" << record.function_id
               << " path=" << join_call_path(record.call_path)
               << " actor=" << record.actor_id
               << " seed=" << record.global_seed
               << " parent_scope=";
        if (record.parent_scope_index.has_value()) stream << *record.parent_scope_index;
        else stream << "none";
        stream << "\n";
        stream << summarize_port_values(record.inputs);
    }
    return stream.str();
}

std::string summarize_instruction_trace(const InstructionTrace& trace)
{
    std::ostringstream stream;
    for (const auto& record : trace.records) {
        stream << "function=" << record.function_id
               << " path=" << join_call_path(record.call_path)
               << " node=" << record.node_id
               << " kind=" << record.instruction_kind;
        if (record.actor_id.has_value()) {
            stream << " actor=" << *record.actor_id;
        }
        stream << "\n";
    }
    return stream.str();
}

std::string summarize_publications(const PublicationTrace& trace)
{
    std::ostringstream stream;
    for (const auto& record : trace.records) {
        stream << "function=" << record.function_id
               << " path=" << join_call_path(record.call_path)
               << " node=" << record.node_id
               << " outputs=" << record.produced_output_count
               << " failures=" << record.failure_count
               << " actor_children=" << record.actor_child_delta_count
               << " cache_hit=" << (record.instruction_cache_hit ? "true" : "false");
        if (record.actor_id.has_value()) {
            stream << " actor=" << *record.actor_id;
        }
        stream << "\n";
    }
    return stream.str();
}

std::string summarize_diagnostics(const DiagnosticsTrace& trace)
{
    std::ostringstream stream;
    for (const auto& record : trace.records) {
        stream << "function=" << record.function_id
               << " path=" << join_call_path(record.call_path)
               << " node=" << record.node_id
               << " kind=" << record.instruction_kind
               << " mode=" << (record.execution_mode == phoenix::FunctionExecutionMode::worker
                    ? "worker" : "serial")
               << " force_run=" << (record.force_run ? "true" : "false")
               << " workers=" << record.requested_worker_count
               << " elapsed_us=" << record.elapsed_microseconds
               << " outputs=" << record.produced_output_count
               << " failures=" << record.failure_count
               << " actor_children=" << record.actor_child_delta_count
               << " cache_hit=" << (record.instruction_cache_hit ? "true" : "false")
               << " multiplex_items=" << record.multiplex_item_count
               << " multiplex_prototype_work=" << record.multiplex_prototype_work_count
               << " multiplex_reused_instances=" << record.multiplex_reused_instance_count;
        if (record.actor_id.has_value()) {
            stream << " actor=" << *record.actor_id;
        }
        stream << "\n";
    }
    return stream.str();
}

std::string summarize_node_states(const std::vector<phoenix::NodeRuntimeState>& states)
{
    std::ostringstream stream;
    for (const auto& state : states) {
        stream << "node=" << state.node_id
               << " state=" << phoenix::to_string(state.state) << "\n";
        for (const auto& port : state.input_ports) {
            const auto promised = state.promised_input_counts.find(port.port);
            const auto received = state.received_input_counts.find(port.port);
            stream << "  port=" << port.port
                   << " expectation=" << phoenix::to_string(port.expectation)
                   << " promised_count=" << (promised == state.promised_input_counts.end() ? 0 : promised->second)
                   << " received_count=" << (received == state.received_input_counts.end() ? 0 : received->second)
                   << " " << value_summary(port.value) << "\n";
        }
    }
    return stream.str();
}

std::string summarize_failures(const std::vector<phoenix::InstructionFailure>& failures)
{
    std::ostringstream stream;
    for (const auto& failure : failures) {
        stream << "node=" << failure.node_id;
        if (failure.item_key.has_value()) {
            stream << " item=" << *failure.item_key;
        }
        stream << " message=" << failure.message
               << " path=" << join_call_path(
                    failure.call_stack.current() == nullptr
                        ? phoenix::FunctionCallPath{}
                        : failure.call_stack.current()->call_path)
               << "\n";
        stream << summarize_port_values(failure.input_context);
    }
    return stream.str();
}

std::string summarize_actor(const phoenix::ActorNode& actor, const std::string& indent = {})
{
    std::ostringstream stream;
    stream << indent << "actor=" << actor.id;
    if (actor.geometry.has_value() && actor.geometry->geometry != nullptr) {
        stream << " geometry_faces=" << actor.geometry->geometry->faces().size()
               << " geometry_fingerprint=" << actor.geometry->geometry->fingerprint();
    }
    stream << "\n";
    for (const auto& child : actor.children) {
        stream << summarize_actor(child, indent + "  ");
    }
    return stream.str();
}

std::filesystem::path default_capture_dir(const std::filesystem::path& package_path)
{
    auto base = package_path;
    const auto extension = base.extension().string();
    if (!extension.empty()) {
        base.replace_extension(extension + ".run");
    } else {
        base += ".run";
    }
    return base;
}

void write_run_capture(
    const std::filesystem::path& capture_dir,
    const std::filesystem::path& package_path,
    const phoenix::FunctionExecutionRequest& request,
    const RootInputLoadResult& root_inputs,
    const phoenix::FunctionExecutionResult& result,
    const ScopeTrace& scopes,
    const InstructionTrace& instructions,
    const PublicationTrace& publications,
    const DiagnosticsTrace& diagnostics)
{
    std::filesystem::create_directories(capture_dir);

    std::ostringstream summary;
    summary << "package=" << package_path.string() << "\n";
    summary << "root_function=" << request.context.function_id << "\n";
    summary << "call_path=" << join_call_path(request.context.call_path) << "\n";
    summary << "seed=" << request.context.global_seed << "\n";
    summary << "lod=" << request.context.lod << "\n";
    summary << "status=" << phoenix::to_string(result.status) << "\n";
    summary << "decoded_root_geometry_payloads=" << root_inputs.decoded_geometry_payloads << "\n";
    summary << "seeded_root_input_ports=" << root_inputs.attached_ports << "\n";
    summary << "outputs=" << result.outputs.size() << "\n";
    summary << "failures=" << result.failures.size() << "\n";
    summary << "scopes=" << scopes.records.size() << "\n";
    summary << "instructions=" << instructions.records.size() << "\n";
    summary << "publications=" << publications.records.size() << "\n";
    summary << "diagnostics=" << diagnostics.records.size() << "\n";
    if (result.actor.has_value() && result.actor->geometry.has_value()
        && result.actor->geometry->geometry != nullptr) {
        summary << "published_actor_geometry=yes\n";
        summary << "published_faces=" << result.actor->geometry->geometry->faces().size() << "\n";
        summary << "published_halfedges=" << result.actor->geometry->geometry->halfedges().size() << "\n";
        summary << "published_vertices=" << result.actor->geometry->geometry->vertices().size() << "\n";
        summary << "published_fingerprint=" << result.actor->geometry->geometry->fingerprint() << "\n";
    } else {
        summary << "published_actor_geometry=no\n";
    }
    if (result.failure_message.has_value()) {
        summary << "failure_message=" << *result.failure_message << "\n";
    }
    write_text_file(capture_dir / "summary.txt", summary.str());
    write_text_file(capture_dir / "request_inputs.txt", summarize_port_values(request.inputs));
    write_text_file(capture_dir / "function_outputs.txt", summarize_port_values(result.outputs));
    write_text_file(capture_dir / "scope_trace.txt", summarize_scopes(scopes));
    write_text_file(capture_dir / "instruction_trace.txt", summarize_instruction_trace(instructions));
    write_text_file(capture_dir / "publication_trace.txt", summarize_publications(publications));
    write_text_file(capture_dir / "diagnostics_trace.txt", summarize_diagnostics(diagnostics));
    write_text_file(capture_dir / "node_states.txt", summarize_node_states(result.node_states));
    write_text_file(capture_dir / "failures.txt", summarize_failures(result.failures));
    if (result.actor.has_value()) {
        write_text_file(capture_dir / "actor_tree.txt", summarize_actor(*result.actor));
    }

    std::unordered_map<std::uint64_t, phoenix::CanonicalGeometryRef> unique_inputs;
    for (std::size_t index = 0; index < request.inputs.size(); ++index) {
        const auto* geometry = request.inputs[index].value.as_geometry();
        if (geometry == nullptr || geometry->geometry == nullptr) continue;
        const auto fingerprint = geometry->geometry->fingerprint();
        if (!unique_inputs.emplace(fingerprint, geometry->geometry).second) continue;
        const auto stem = capture_dir / ("input_geometry_" + std::to_string(index));
        write_geometry_file(stem.string() + ".phxgeom", geometry->geometry);
        write_obj_file(stem.string() + ".obj", geometry->geometry);
    }
    if (result.actor.has_value() && result.actor->geometry.has_value()) {
        write_geometry_file(capture_dir / "published_actor_geometry.phxgeom",
            result.actor->geometry->geometry);
        write_obj_file(capture_dir / "published_actor_geometry.obj",
            result.actor->geometry->geometry);
    }

    for (const auto& state : result.node_states) {
        for (const auto& port : state.input_ports) {
            const auto stem = "node_" + std::to_string(state.node_id)
                + "_input_" + port.port;
            write_runtime_value_geometry_files(capture_dir, stem, port.value);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3) {
        print_usage();
        return 2;
    }

    const auto path = std::filesystem::path{argv[1]};
    const auto capture_dir = argc == 3
        ? std::filesystem::path{argv[2]}
        : default_capture_dir(path);
    const phoenix::migration::MigratedProjectPackageReader reader;
    const auto read = reader.read(path);
    if (!read.ok()) {
        for (const auto& diagnostic : read.diagnostics) {
            std::cerr << "error package_io." << phoenix::migration::to_string(diagnostic.code)
                      << ": " << diagnostic.message << "\n";
        }
        return 1;
    }

    const phoenix::migration::MigratedPackageRuntimeLoader loader;
    const auto loaded = loader.load(read.package);
    if (!loaded.ok()) {
        for (const auto& diagnostic : loaded.diagnostics) {
            std::cerr << "error package_load." << phoenix::migration::to_string(diagnostic.code)
                      << " function=" << diagnostic.function_id
                      << ": " << diagnostic.message << "\n";
        }
        return 1;
    }

    auto migrated_registry = phoenix::migration::make_migrated_instruction_registry(loaded.package);
    if (report_missing_handlers(loaded.package, migrated_registry.registry)) {
        std::cerr << "runtime_attempt: blocked\n";
        return 1;
    }
    if (!migrated_registry.unsupported_kinds.empty()) {
        for (const auto& kind : migrated_registry.unsupported_kinds) {
            std::cerr << "error runtime.unsupported_handler kind=" << kind << "\n";
            const auto total = migrated_registry.total_by_kind.find(kind);
            const auto adapted = migrated_registry.adapted_by_kind.find(kind);
            if (total != migrated_registry.total_by_kind.end()
                && adapted != migrated_registry.adapted_by_kind.end()
                && adapted->second > 0) {
                std::cerr << "info runtime.adapter_coverage kind=" << kind
                          << " adapted=" << adapted->second
                          << " total=" << total->second << "\n";
            }
            const auto reasons = migrated_registry.unsupported_reasons_by_kind.find(kind);
            if (reasons != migrated_registry.unsupported_reasons_by_kind.end()) {
                for (const auto& reason : reasons->second) {
                    std::cerr << "info runtime.unsupported_reason kind=" << kind
                              << " reason=" << reason.first
                              << " count=" << reason.second << "\n";
                }
            }
            const auto examples = migrated_registry.unsupported_examples_by_kind.find(kind);
            if (examples != migrated_registry.unsupported_examples_by_kind.end()) {
                for (const auto& example : examples->second) {
                    std::cerr << "info runtime.unsupported_example kind=" << kind
                              << " " << example << "\n";
                }
            }
        }
        std::cerr << "runtime_attempt: blocked\n";
        return 1;
    }

    const auto root_it = loaded.package.functions.find(loaded.package.root_function_id);
    if (root_it == loaded.package.functions.end()) {
        std::cerr << "error runtime.root_missing function=" << loaded.package.root_function_id << "\n";
        return 1;
    }

    auto library = make_function_library(loaded.package);
    phoenix::FunctionExecutionRequest request;
    request.function = &root_it->second.graph;
    const auto root_inputs = default_root_inputs(loaded.package, root_it->second);
    request.inputs = root_inputs.inputs;
    request.context.function_id = root_it->second.graph.id;
    request.context.call_path = {"root"};
    request.context.global_seed = 13;
    request.trace_level = phoenix::ExecutionTraceLevel::instruction;
    ScopeTrace scope_trace;
    InstructionTrace instruction_trace;
    PublicationTrace publication_trace;
    DiagnosticsTrace diagnostics_trace;
    request.scope_trace_sink = &scope_trace;
    request.instruction_trace_sink = &instruction_trace;
    request.publication_trace_sink = &publication_trace;
    request.diagnostics_sink = &diagnostics_trace;

    const phoenix::FunctionExecutor executor(migrated_registry.registry, library);
    const auto result = executor.run(request);
    write_run_capture(capture_dir, path, request, root_inputs, result,
        scope_trace, instruction_trace, publication_trace, diagnostics_trace);
    if (result.status != phoenix::FunctionExecutionStatus::completed) {
        std::cerr << "error runtime.execution_failed status=" << phoenix::to_string(result.status);
        if (result.failure_message.has_value()) {
            std::cerr << ": " << *result.failure_message;
        }
        std::cerr << "\n";
        std::cerr << "runtime_attempt: blocked\n";
        return 1;
    }

    std::cout << "runtime_attempt: ok\n";
    std::cout << "decoded_root_geometry_payloads: " << root_inputs.decoded_geometry_payloads << "\n";
    std::cout << "seeded_root_input_ports: " << root_inputs.attached_ports << "\n";
    std::cout << "outputs: " << result.outputs.size() << "\n";
    std::cout << "capture_dir: " << capture_dir.string() << "\n";
    if (result.actor.has_value() && result.actor->geometry.has_value()
        && result.actor->geometry->geometry != nullptr) {
        const auto& geometry = result.actor->geometry->geometry;
        std::cout << "published_actor_geometry: yes\n";
        std::cout << "published_faces: " << geometry->faces().size() << "\n";
        std::cout << "published_halfedges: " << geometry->halfedges().size() << "\n";
        std::cout << "published_vertices: " << geometry->vertices().size() << "\n";
    } else {
        std::cout << "published_actor_geometry: no\n";
    }
    return 0;
}
