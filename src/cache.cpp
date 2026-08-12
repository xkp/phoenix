#include "phoenix/cache.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace phoenix {

namespace {

void append_field(std::ostringstream& stream, const std::string& value)
{
    stream << value.size() << ':' << value;
}

void append_number_field(std::ostringstream& stream, std::uint64_t value)
{
    append_field(stream, std::to_string(value));
}

void append_bool_field(std::ostringstream& stream, bool value)
{
    append_field(stream, value ? "true" : "false");
}

void append_separator(std::ostringstream& stream)
{
    stream << '|';
}

void append_identity(std::ostringstream& stream, const CacheIdentity& identity)
{
    append_field(stream, identity.function_id);
    append_separator(stream);

    append_number_field(stream, static_cast<std::uint64_t>(identity.call_path.size()));
    for (const auto& segment : identity.call_path) {
        append_separator(stream);
        append_field(stream, segment);
    }
    append_separator(stream);

    append_field(stream, identity.graph_revision);
    append_separator(stream);
    append_field(stream, identity.input_fingerprint);
    append_separator(stream);
    append_number_field(stream, identity.global_seed);
}

CacheKey make_key(const char* kind, const CacheIdentity& identity)
{
    std::ostringstream stream;
    append_field(stream, kind);
    append_separator(stream);
    append_identity(stream, identity);
    return CacheKey{stream.str()};
}

void append_optional_number(std::ostringstream& stream, const std::optional<SeedValue>& value)
{
    append_bool_field(stream, value.has_value());
    if (value.has_value()) {
        append_separator(stream);
        append_number_field(stream, *value);
    }
}

void append_optional_node(std::ostringstream& stream, const std::optional<NodeId>& value)
{
    append_bool_field(stream, value.has_value());
    if (value.has_value()) {
        append_separator(stream);
        append_number_field(stream, *value);
    }
}

void append_optional_string(std::ostringstream& stream, const std::optional<std::string>& value)
{
    append_bool_field(stream, value.has_value());
    if (value.has_value()) {
        append_separator(stream);
        append_field(stream, *value);
    }
}

void append_port_descriptor(std::ostringstream& stream, const PortDescriptor& port)
{
    append_field(stream, port.id);
    append_separator(stream);
    append_field(stream, port.type);
    append_separator(stream);
    append_field(stream, port.direction == PortDirection::input ? "input" : "output");
}

std::vector<PortDescriptor> sorted_ports(std::vector<PortDescriptor> ports)
{
    std::sort(
        ports.begin(),
        ports.end(),
        [](const PortDescriptor& left, const PortDescriptor& right) {
            if (left.id != right.id) {
                return left.id < right.id;
            }
            if (left.type != right.type) {
                return left.type < right.type;
            }
            return static_cast<int>(left.direction) < static_cast<int>(right.direction);
        });
    return ports;
}

std::vector<EdgeDescriptor> sorted_edges(std::vector<EdgeDescriptor> edges)
{
    std::sort(
        edges.begin(),
        edges.end(),
        [](const EdgeDescriptor& left, const EdgeDescriptor& right) {
            if (left.from_node != right.from_node) {
                return left.from_node < right.from_node;
            }
            if (left.from_port != right.from_port) {
                return left.from_port < right.from_port;
            }
            if (left.to_node != right.to_node) {
                return left.to_node < right.to_node;
            }
            return left.to_port < right.to_port;
        });
    return edges;
}

std::vector<InstructionDescriptor> sorted_instructions(std::vector<InstructionDescriptor> instructions)
{
    std::sort(
        instructions.begin(),
        instructions.end(),
        [](const InstructionDescriptor& left, const InstructionDescriptor& right) {
            return left.id < right.id;
        });
    return instructions;
}

std::vector<PortValue> sorted_port_values(std::vector<PortValue> values)
{
    std::sort(
        values.begin(),
        values.end(),
        [](const PortValue& left, const PortValue& right) {
            return left.port < right.port;
        });
    return values;
}

void append_literal_scalar(std::ostringstream& stream, const LiteralScalar& scalar)
{
    if (const auto* value = std::get_if<std::int64_t>(&scalar)) {
        append_field(stream, "int");
        append_separator(stream);
        append_field(stream, std::to_string(*value));
        return;
    }

    if (const auto* value = std::get_if<double>(&scalar)) {
        append_field(stream, "double");
        append_separator(stream);
        stream.precision(17);
        append_field(stream, std::to_string(*value));
        return;
    }

    if (const auto* value = std::get_if<bool>(&scalar)) {
        append_field(stream, "bool");
        append_separator(stream);
        append_bool_field(stream, *value);
        return;
    }

    append_field(stream, "string");
    append_separator(stream);
    append_field(stream, std::get<std::string>(scalar));
}

void append_literal_value(std::ostringstream& stream, const LiteralValue& literal)
{
    if (const auto* scalar = std::get_if<LiteralScalar>(&literal)) {
        append_field(stream, "scalar");
        append_separator(stream);
        append_literal_scalar(stream, *scalar);
        return;
    }

    const auto& array = std::get<LiteralArray>(literal);
    append_field(stream, "array");
    append_separator(stream);
    append_number_field(stream, static_cast<std::uint64_t>(array.size()));
    for (const auto& scalar : array) {
        append_separator(stream);
        append_literal_scalar(stream, scalar);
    }
}

void append_geometry_value(std::ostringstream& stream, const GeometryValue& geometry)
{
    append_field(stream, geometry.debug_label);
    append_separator(stream);
    append_optional_string(stream, geometry.accumulation_actor_id);
    append_separator(stream);
    append_bool_field(stream, geometry.geometry != nullptr);
    if (geometry.geometry != nullptr) {
        append_separator(stream);
        append_number_field(stream, geometry.geometry->fingerprint());
    }
}

void append_runtime_value(std::ostringstream& stream, const RuntimeValue& value)
{
    append_field(stream, to_string(value.presence));
    append_separator(stream);

    if (const auto* geometry = value.as_geometry()) {
        append_field(stream, "geometry");
        append_separator(stream);
        append_geometry_value(stream, *geometry);
        return;
    }

    if (const auto* collection = value.as_geometry_collection()) {
        append_field(stream, "geometry_collection");
        append_separator(stream);
        append_number_field(stream, static_cast<std::uint64_t>(collection->contributions.size()));
        for (const auto& contribution : collection->contributions) {
            append_separator(stream);
            append_geometry_value(stream, contribution);
        }
        return;
    }

    if (const auto* literal = value.as_literal()) {
        append_field(stream, "literal");
        append_separator(stream);
        append_literal_value(stream, *literal);
        return;
    }

    if (const auto* default_value = value.as_default()) {
        append_field(stream, "default");
        append_separator(stream);
        append_field(stream, default_value->source_type);
        return;
    }

    append_field(stream, "empty");
}

bool starts_with(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size()
        && value.compare(0, prefix.size(), prefix) == 0;
}

template <typename Entry>
void remove_entries_with_prefix(
    std::unordered_map<std::string, Entry>& entries,
    const std::string& prefix)
{
    for (auto it = entries.begin(); it != entries.end();) {
        if (starts_with(it->first, prefix)) {
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

CacheKey CacheKeyBuilder::instruction_outputs(const InstructionCacheKeyInput& input) const
{
    auto key = make_key("instruction_outputs", input.identity);
    std::ostringstream stream;
    stream << key.stable_key;
    append_separator(stream);
    append_number_field(stream, input.node_id);
    append_separator(stream);
    append_number_field(stream, input.effective_seed.value_or(0));
    return CacheKey{stream.str()};
}

CacheKey CacheKeyBuilder::function_call(const FunctionCallCacheKeyInput& input) const
{
    return make_key("function_call", input.identity);
}

CacheKey CacheKeyBuilder::actor_subtree(const ActorSubtreeCacheKeyInput& input) const
{
    auto key = make_key("actor_subtree", input.identity);
    std::ostringstream stream;
    stream << key.stable_key;
    append_separator(stream);
    append_field(stream, input.actor_id);
    return CacheKey{stream.str()};
}

CacheKey CacheKeyBuilder::actor_prototype(const ActorPrototypeCacheKeyInput& input) const
{
    auto key = make_key("actor_prototype", input.identity);
    std::ostringstream stream;
    stream << key.stable_key;
    append_separator(stream);
    append_field(stream, input.actor_function_id);
    append_separator(stream);
    append_field(stream, input.instance_key);
    return CacheKey{stream.str()};
}

std::string CacheIdentityBuilder::graph_revision(const FunctionDescriptor& function) const
{
    std::ostringstream stream;
    append_field(stream, "graph_revision_v1");
    append_separator(stream);
    append_field(stream, function.id);
    append_separator(stream);
    append_bool_field(stream, function.generates_actor);
    append_separator(stream);
    append_optional_node(stream, function.output_node_id);

    const auto input_ports = sorted_ports(function.input_ports);
    append_separator(stream);
    append_number_field(stream, static_cast<std::uint64_t>(input_ports.size()));
    for (const auto& port : input_ports) {
        append_separator(stream);
        append_port_descriptor(stream, port);
    }

    const auto output_ports = sorted_ports(function.output_ports);
    append_separator(stream);
    append_number_field(stream, static_cast<std::uint64_t>(output_ports.size()));
    for (const auto& port : output_ports) {
        append_separator(stream);
        append_port_descriptor(stream, port);
    }

    const auto instructions = sorted_instructions(function.instructions);
    append_separator(stream);
    append_number_field(stream, static_cast<std::uint64_t>(instructions.size()));
    for (const auto& instruction : instructions) {
        append_separator(stream);
        append_number_field(stream, instruction.id);
        append_separator(stream);
        append_field(stream, instruction.kind);
        append_separator(stream);
        append_bool_field(stream, instruction.generates_actor);
        append_separator(stream);
        append_bool_field(stream, instruction.multiplexes_input);
        append_separator(stream);
        append_bool_field(stream, instruction.has_else_port);
        append_separator(stream);
        append_bool_field(stream, instruction.failure_is_critical);
        append_separator(stream);
        append_bool_field(stream, instruction.enables_instancing);
        append_separator(stream);
        append_bool_field(stream, instruction.consumes_geometry);
        append_separator(stream);
        append_field(
            stream,
            instruction.multiplex_seed_mode == MultiplexSeedMode::one_seed_for_all
                ? "one_seed_for_all"
                : "one_seed_each");
        append_separator(stream);
        append_optional_number(stream, instruction.local_seed);
        append_separator(stream);
        append_optional_string(stream, instruction.called_function_id);

        auto referenced_labels = instruction.referenced_label_uids;
        std::sort(referenced_labels.begin(), referenced_labels.end());
        append_separator(stream);
        append_number_field(stream, static_cast<std::uint64_t>(referenced_labels.size()));
        for (const auto& uid : referenced_labels) {
            append_separator(stream);
            append_field(stream, uid);
        }

        const auto instruction_inputs = sorted_ports(instruction.input_ports);
        append_separator(stream);
        append_number_field(stream, static_cast<std::uint64_t>(instruction_inputs.size()));
        for (const auto& port : instruction_inputs) {
            append_separator(stream);
            append_port_descriptor(stream, port);
        }

        const auto instruction_outputs = sorted_ports(instruction.output_ports);
        append_separator(stream);
        append_number_field(stream, static_cast<std::uint64_t>(instruction_outputs.size()));
        for (const auto& port : instruction_outputs) {
            append_separator(stream);
            append_port_descriptor(stream, port);
        }
    }

    const auto edges = sorted_edges(function.edges);
    append_separator(stream);
    append_number_field(stream, static_cast<std::uint64_t>(edges.size()));
    for (const auto& edge : edges) {
        append_separator(stream);
        append_number_field(stream, edge.from_node);
        append_separator(stream);
        append_field(stream, edge.from_port);
        append_separator(stream);
        append_number_field(stream, edge.to_node);
        append_separator(stream);
        append_field(stream, edge.to_port);
    }

    return stream.str();
}

std::string CacheIdentityBuilder::input_fingerprint(const std::vector<PortValue>& inputs) const
{
    std::ostringstream stream;
    append_field(stream, "input_fingerprint_v1");

    const auto sorted_inputs = sorted_port_values(inputs);
    append_separator(stream);
    append_number_field(stream, static_cast<std::uint64_t>(sorted_inputs.size()));
    for (const auto& input : sorted_inputs) {
        append_separator(stream);
        append_field(stream, input.port);
        append_separator(stream);
        append_runtime_value(stream, input.value);
    }

    return stream.str();
}

CacheIdentity CacheIdentityBuilder::identity(const CacheIdentityInput& input) const
{
    CacheIdentity identity;
    if (input.function != nullptr) {
        identity.function_id = input.function->id;
        identity.graph_revision = graph_revision(*input.function);
    }
    identity.call_path = input.call_path;
    identity.input_fingerprint = input_fingerprint(input.inputs);
    identity.global_seed = input.global_seed;
    return identity;
}

void MemoryCacheStore::put_instruction(InstructionCacheEntry entry)
{
    instruction_entries_[entry.key.stable_key] = std::move(entry);
}

void MemoryCacheStore::put_function_call(FunctionCallCacheEntry entry)
{
    function_call_entries_[entry.key.stable_key] = std::move(entry);
}

void MemoryCacheStore::put_actor_subtree(ActorSubtreeCacheEntry entry)
{
    actor_subtree_entries_[entry.key.stable_key] = std::move(entry);
}

void MemoryCacheStore::put_actor_prototype(ActorPrototypeCacheEntry entry)
{
    actor_prototype_entries_[entry.key.stable_key] = std::move(entry);
}

bool MemoryCacheStore::remove_instruction(const CacheKey& key)
{
    return instruction_entries_.erase(key.stable_key) > 0;
}

bool MemoryCacheStore::remove_function_call(const CacheKey& key)
{
    return function_call_entries_.erase(key.stable_key) > 0;
}

bool MemoryCacheStore::remove_actor_subtree(const CacheKey& key)
{
    return actor_subtree_entries_.erase(key.stable_key) > 0;
}

bool MemoryCacheStore::remove_actor_prototype(const CacheKey& key)
{
    return actor_prototype_entries_.erase(key.stable_key) > 0;
}

void MemoryCacheStore::clear_identity(const CacheIdentity& identity)
{
    remove_entries_with_prefix(
        instruction_entries_,
        make_key("instruction_outputs", identity).stable_key);
    remove_entries_with_prefix(
        function_call_entries_,
        make_key("function_call", identity).stable_key);
    remove_entries_with_prefix(
        actor_subtree_entries_,
        make_key("actor_subtree", identity).stable_key);
    remove_entries_with_prefix(
        actor_prototype_entries_,
        make_key("actor_prototype", identity).stable_key);
}

void MemoryCacheStore::clear()
{
    instruction_entries_.clear();
    function_call_entries_.clear();
    actor_subtree_entries_.clear();
    actor_prototype_entries_.clear();
}

std::optional<InstructionCacheEntry> MemoryCacheStore::find_instruction(const CacheKey& key) const
{
    const auto it = instruction_entries_.find(key.stable_key);
    if (it == instruction_entries_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::optional<FunctionCallCacheEntry> MemoryCacheStore::find_function_call(const CacheKey& key) const
{
    const auto it = function_call_entries_.find(key.stable_key);
    if (it == function_call_entries_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::optional<ActorSubtreeCacheEntry> MemoryCacheStore::find_actor_subtree(const CacheKey& key) const
{
    const auto it = actor_subtree_entries_.find(key.stable_key);
    if (it == actor_subtree_entries_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::optional<ActorPrototypeCacheEntry> MemoryCacheStore::find_actor_prototype(const CacheKey& key) const
{
    const auto it = actor_prototype_entries_.find(key.stable_key);
    if (it == actor_prototype_entries_.end()) {
        return std::nullopt;
    }

    return it->second;
}

} // namespace phoenix
