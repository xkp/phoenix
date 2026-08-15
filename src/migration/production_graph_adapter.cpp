#include "phoenix/migration/production_graph_adapter.hpp"

#include <cctype>
#include <set>
#include <regex>

namespace phoenix::migration {
namespace {

struct RawPort {
    PortId id;
    TypeId type;
};

struct RawNode {
    NodeId id = 0;
    std::size_t order = 0;
    std::string kind;
    std::string file;
    bool disabled = false;
    std::vector<RawPort> inputs;
    std::vector<RawPort> outputs;
};

struct RawLink {
    std::uint64_t output_node = 0;
    std::size_t output_socket = 0;
    std::uint64_t input_node = 0;
    std::size_t input_socket = 0;
};

struct ResolvedLink {
    const RawNode* output_node = nullptr;
    std::size_t output_socket = 0;
    const RawNode* input_node = nullptr;
    std::size_t input_socket = 0;
};

bool looks_like_function_id(const std::string& value)
{
    static const std::regex pattern{
        R"(^[^@\\/:*?"<>|]+@[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$)"};
    return std::regex_match(value, pattern);
}

bool looks_like_guid(const std::string& value)
{
    static const std::regex pattern{
        R"(^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$)"};
    return std::regex_match(value, pattern);
}

std::string extract_string_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*"([^"]*)")regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    return it == std::sregex_iterator{} ? std::string{} : (*it)[1].str();
}

std::uint64_t extract_uint_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*([0-9]+))regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    return it == std::sregex_iterator{} ? 0 : static_cast<std::uint64_t>(std::stoull((*it)[1].str()));
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

std::vector<RawPort> extract_ports(const std::string& node_text, const std::string& key)
{
    std::vector<RawPort> ports;
    const auto ports_text = extract_array_text(node_text, key);
    std::size_t index = 0;
    for (const auto& object : extract_object_texts(ports_text)) {
        auto name = extract_string_value(object, "name");
        if (name.empty()) name = key + std::to_string(index);
        ports.push_back(RawPort{
            std::to_string(index) + ":" + name,
            extract_string_value(object, "dataType")});
        ++index;
    }
    return ports;
}

std::vector<RawNode> extract_nodes(const std::string& nodes_text)
{
    std::vector<RawNode> nodes;
    const auto raw_nodes = extract_array_text(nodes_text, "nodes");
    std::size_t order = 0;
    for (const auto& object : extract_object_texts(raw_nodes)) {
        RawNode node;
        node.id = extract_uint_value(object, "id");
        node.order = order;
        node.kind = extract_string_value(object, "typeName");
        node.file = extract_string_value(object, "file");
        node.disabled = extract_bool_value(object, "disabled", false);
        node.inputs = extract_ports(object, "inputs");
        node.outputs = extract_ports(object, "outputs");
        nodes.push_back(std::move(node));
        ++order;
    }
    return nodes;
}

std::vector<RawLink> extract_links(const std::string& nodes_text)
{
    std::vector<RawLink> links;
    const auto raw_links = extract_array_text(nodes_text, "links");
    for (const auto& object : extract_object_texts(raw_links)) {
        links.push_back(RawLink{
            extract_uint_value(object, "outputNode"),
            static_cast<std::size_t>(extract_uint_value(object, "outputSocket")),
            extract_uint_value(object, "inputNode"),
            static_cast<std::size_t>(extract_uint_value(object, "inputSocket"))});
    }
    return links;
}

const RawNode* resolve_node(
    std::uint64_t token,
    const std::vector<RawNode>& nodes)
{
    if (token < nodes.size()) return &nodes[static_cast<std::size_t>(token)];
    return nullptr;
}

bool is_function_boundary(const RawNode& node)
{
    return node.kind == "functionInput" || node.kind == "functionOutput";
}

std::set<NodeId> live_node_ids(const std::vector<RawNode>& nodes, const std::vector<ResolvedLink>& links)
{
    std::set<NodeId> active;
    std::set<NodeId> forward;
    std::set<NodeId> backward;
    std::vector<NodeId> pending;

    for (const auto& node : nodes) {
        if (!node.disabled) active.insert(node.id);
        if (!node.disabled && node.kind == "functionInput") {
            forward.insert(node.id);
            pending.push_back(node.id);
        }
    }

    if (pending.empty()) {
        forward = active;
    } else {
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            for (const auto& link : links) {
                if (link.output_node->id != id) continue;
                const auto next = link.input_node->id;
                if (forward.insert(next).second) pending.push_back(next);
            }
        }
    }

    pending.clear();
    for (const auto& node : nodes) {
        if (!node.disabled && node.kind == "functionOutput") {
            backward.insert(node.id);
            pending.push_back(node.id);
        }
    }

    if (pending.empty()) {
        backward = active;
    } else {
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            for (const auto& link : links) {
                if (link.input_node->id != id) continue;
                const auto previous = link.output_node->id;
                if (backward.insert(previous).second) pending.push_back(previous);
            }
        }
    }

    std::set<NodeId> live;
    for (const auto id : active) {
        if (forward.count(id) != 0 && backward.count(id) != 0) live.insert(id);
    }
    return live;
}

std::string config_revision_for_file(const std::string& file)
{
    if (file.empty()) return {};
    if (looks_like_function_id(file)) return {};
    if (looks_like_guid(file)) return "payload:" + file;
    return "file:" + file;
}

} // namespace

ProductionGraphAdaptResult ProductionGraphAdapter::adapt(
    const ProductionFunctionLinkResult& linked) const
{
    ProductionGraphAdaptResult result;

    for (const auto& entry : linked.functions) {
        const auto& linked_function = entry.second;
        const auto raw_nodes = extract_nodes(linked_function.function.nodes_text);
        const auto raw_links = extract_links(linked_function.function.nodes_text);

        FunctionDescriptor function;
        function.id = linked_function.id;

        std::vector<ResolvedLink> resolved_links;
        for (const auto& link : raw_links) {
            const auto* output_node = resolve_node(link.output_node, raw_nodes);
            const auto* input_node = resolve_node(link.input_node, raw_nodes);
            if (output_node == nullptr || input_node == nullptr) {
                result.diagnostics.push_back(GraphAdaptDiagnostic{
                    GraphAdaptDiagnosticCode::unresolved_link_node,
                    "Production link references a missing node.",
                    linked_function.id,
                    output_node == nullptr ? link.output_node : link.input_node});
                continue;
            }
            if (output_node->disabled || input_node->disabled) continue;
            resolved_links.push_back(ResolvedLink{
                output_node,
                link.output_socket,
                input_node,
                link.input_socket});
        }

        const auto live_nodes = live_node_ids(raw_nodes, resolved_links);

        for (const auto& node : raw_nodes) {
            if (node.disabled) continue;
            if (live_nodes.count(node.id) == 0 && !is_function_boundary(node)) continue;
            if (node.kind == "functionInput") {
                for (const auto& output : node.outputs) {
                    function.input_ports.push_back(PortDescriptor{
                        output.id,
                        output.type,
                        PortDirection::input});
                }
                continue;
            }
            if (node.kind == "functionOutput") {
                for (const auto& input : node.inputs) {
                    function.output_ports.push_back(PortDescriptor{
                        input.id,
                        input.type,
                        PortDirection::output});
                }
                function.output_node_id = node.id;
                continue;
            }

            InstructionDescriptor instruction;
            instruction.id = node.id;
            instruction.kind = node.kind;
            for (const auto& input : node.inputs) {
                instruction.input_ports.push_back(PortDescriptor{
                    input.id,
                    input.type,
                    PortDirection::input});
            }
            for (const auto& output : node.outputs) {
                instruction.output_ports.push_back(PortDescriptor{
                    output.id,
                    output.type,
                    PortDirection::output});
            }
            if (looks_like_function_id(node.file)) instruction.called_function_id = node.file;
            instruction.configuration_revision = config_revision_for_file(node.file);
            function.instructions.push_back(std::move(instruction));
        }

        for (const auto& link : resolved_links) {
            const auto* output_node = link.output_node;
            const auto* input_node = link.input_node;
            if (live_nodes.count(output_node->id) == 0 || live_nodes.count(input_node->id) == 0) continue;
            if (link.output_socket >= output_node->outputs.size()
                || link.input_socket >= input_node->inputs.size()) {
                result.diagnostics.push_back(GraphAdaptDiagnostic{
                    GraphAdaptDiagnosticCode::unresolved_link_socket,
                    "Production link references a missing socket.",
                    linked_function.id,
                    output_node->id});
                continue;
            }
            function.edges.push_back(EdgeDescriptor{
                output_node->id,
                output_node->outputs[link.output_socket].id,
                input_node->id,
                input_node->inputs[link.input_socket].id});
        }

        result.functions.emplace(function.id, std::move(function));
    }

    return result;
}

std::string to_string(GraphAdaptDiagnosticCode code)
{
    switch (code) {
    case GraphAdaptDiagnosticCode::unresolved_link_node: return "unresolved_link_node";
    case GraphAdaptDiagnosticCode::unresolved_link_socket: return "unresolved_link_socket";
    }
    return "unknown";
}

} // namespace phoenix::migration
