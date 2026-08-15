#include "phoenix/migration/production_migrated_package_io.hpp"

#include <fstream>
#include <sstream>

namespace phoenix::migration {
namespace {

std::string escape(const std::string& value)
{
    std::string result;
    for (const auto ch : value) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case '|': result += "\\p"; break;
        default: result += ch; break;
        }
    }
    return result;
}

std::string unescape(const std::string& value)
{
    std::string result;
    bool escaped = false;
    for (const auto ch : value) {
        if (!escaped) {
            if (ch == '\\') {
                escaped = true;
            } else {
                result += ch;
            }
            continue;
        }
        switch (ch) {
        case '\\': result += '\\'; break;
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case 'p': result += '|'; break;
        default: result += ch; break;
        }
        escaped = false;
    }
    return result;
}

std::vector<std::string> split(const std::string& line)
{
    std::vector<std::string> parts;
    std::string part;
    bool escaped = false;
    for (const auto ch : line) {
        if (!escaped && ch == '|') {
            parts.push_back(part);
            part.clear();
            continue;
        }
        if (!escaped && ch == '\\') {
            escaped = true;
        } else {
            escaped = false;
        }
        part += ch;
    }
    parts.push_back(part);
    return parts;
}

PortDirection parse_direction(const std::string& value)
{
    return value == "output" ? PortDirection::output : PortDirection::input;
}

std::string format_direction(PortDirection direction)
{
    return direction == PortDirection::output ? "output" : "input";
}

} // namespace

std::vector<PackageIoDiagnostic> MigratedProjectPackageWriter::write(
    const MigratedProjectPackage& package,
    const std::filesystem::path& path) const
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return {{PackageIoDiagnosticCode::cannot_open_file, "Could not open migrated package for writing."}};
    }

    output << "PHOENIX_MIGRATED_PACKAGE_TEXT_V0\n";
    output << "schema|" << escape(package.schema_version) << "\n";
    output << "root|" << escape(package.root_function_id) << "\n";
    output << "label_fingerprint|" << package.label_registry_fingerprint << "\n";

    for (const auto& function_entry : package.functions) {
        const auto& function = function_entry.second;
        output << "function|" << escape(function_entry.first)
               << "|" << escape(function.manifest_path.string())
               << "|" << escape(function.nodes_path.string())
               << "|" << function.fingerprint << "\n";
        for (const auto& origin : function.origins) {
            output << "origin|" << escape(function_entry.first)
                   << "|" << escape(origin.string()) << "\n";
        }
        const auto& graph = function.graph;
        for (const auto& port : graph.input_ports) {
            output << "function_port|" << escape(function_entry.first)
                   << "|input|" << escape(port.id)
                   << "|" << escape(port.type) << "\n";
        }
        for (const auto& port : graph.output_ports) {
            output << "function_port|" << escape(function_entry.first)
                   << "|output|" << escape(port.id)
                   << "|" << escape(port.type) << "\n";
        }
        for (const auto& instruction : graph.instructions) {
            output << "instruction|" << escape(function_entry.first)
                   << "|" << instruction.id
                   << "|" << escape(instruction.kind)
                   << "|" << escape(instruction.called_function_id.value_or(FunctionId{}))
                   << "|" << escape(instruction.configuration_revision) << "\n";
            for (const auto& port : instruction.input_ports) {
                output << "instruction_port|" << escape(function_entry.first)
                       << "|" << instruction.id
                       << "|" << format_direction(port.direction)
                       << "|" << escape(port.id)
                       << "|" << escape(port.type) << "\n";
            }
            for (const auto& port : instruction.output_ports) {
                output << "instruction_port|" << escape(function_entry.first)
                       << "|" << instruction.id
                       << "|" << format_direction(port.direction)
                       << "|" << escape(port.id)
                       << "|" << escape(port.type) << "\n";
            }
        }
        for (const auto& edge : graph.edges) {
            output << "edge|" << escape(function_entry.first)
                   << "|" << edge.from_node
                   << "|" << escape(edge.from_port)
                   << "|" << edge.to_node
                   << "|" << escape(edge.to_port) << "\n";
        }
        for (const auto& payload : function.payloads) {
            output << "payload|" << escape(function_entry.first)
                   << "|" << escape(payload.first)
                   << "|" << escape(payload.second.source_path.string())
                   << "|" << escape(payload.second.text) << "\n";
        }
    }

    return {};
}

PackageReadResult MigratedProjectPackageReader::read(const std::filesystem::path& path) const
{
    PackageReadResult result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.diagnostics.push_back(
            {PackageIoDiagnosticCode::cannot_open_file, "Could not open migrated package for reading."});
        return result;
    }

    std::string line;
    if (!std::getline(input, line) || line != "PHOENIX_MIGRATED_PACKAGE_TEXT_V0") {
        result.diagnostics.push_back(
            {PackageIoDiagnosticCode::invalid_format, "Migrated package header is invalid."});
        return result;
    }

    while (std::getline(input, line)) {
        const auto parts = split(line);
        if (parts.empty()) continue;
        const auto tag = parts[0];
        if (tag == "schema" && parts.size() == 2) {
            result.package.schema_version = unescape(parts[1]);
        } else if (tag == "root" && parts.size() == 2) {
            result.package.root_function_id = unescape(parts[1]);
        } else if (tag == "label_fingerprint" && parts.size() == 2) {
            result.package.label_registry_fingerprint = std::stoull(parts[1]);
        } else if (tag == "function" && parts.size() == 5) {
            const auto id = unescape(parts[1]);
            auto& function = result.package.functions[id];
            function.graph.id = id;
            function.manifest_path = unescape(parts[2]);
            function.nodes_path = unescape(parts[3]);
            function.fingerprint = std::stoull(parts[4]);
        } else if (tag == "origin" && parts.size() == 3) {
            result.package.functions[unescape(parts[1])].origins.push_back(unescape(parts[2]));
        } else if (tag == "function_port" && parts.size() == 5) {
            auto& graph = result.package.functions[unescape(parts[1])].graph;
            auto& ports = parts[2] == "output" ? graph.output_ports : graph.input_ports;
            ports.push_back(PortDescriptor{unescape(parts[3]), unescape(parts[4]), parse_direction(parts[2])});
        } else if (tag == "instruction" && parts.size() == 6) {
            InstructionDescriptor instruction;
            instruction.id = static_cast<NodeId>(std::stoull(parts[2]));
            instruction.kind = unescape(parts[3]);
            const auto called = unescape(parts[4]);
            if (!called.empty()) instruction.called_function_id = called;
            instruction.configuration_revision = unescape(parts[5]);
            result.package.functions[unescape(parts[1])].graph.instructions.push_back(std::move(instruction));
        } else if (tag == "instruction_port" && parts.size() == 6) {
            auto& instructions = result.package.functions[unescape(parts[1])].graph.instructions;
            const auto node_id = static_cast<NodeId>(std::stoull(parts[2]));
            for (auto& instruction : instructions) {
                if (instruction.id != node_id) continue;
                auto& ports = parts[3] == "output" ? instruction.output_ports : instruction.input_ports;
                ports.push_back(PortDescriptor{unescape(parts[4]), unescape(parts[5]), parse_direction(parts[3])});
                break;
            }
        } else if (tag == "edge" && parts.size() == 6) {
            result.package.functions[unescape(parts[1])].graph.edges.push_back(EdgeDescriptor{
                static_cast<NodeId>(std::stoull(parts[2])),
                unescape(parts[3]),
                static_cast<NodeId>(std::stoull(parts[4])),
                unescape(parts[5])});
        } else if (tag == "payload" && parts.size() == 5) {
            const auto function_id = unescape(parts[1]);
            const auto payload_id = unescape(parts[2]);
            result.package.functions[function_id].payloads.emplace(payload_id, MigratedInstructionPayload{
                payload_id,
                unescape(parts[3]),
                unescape(parts[4])});
        } else {
            result.diagnostics.push_back(
                {PackageIoDiagnosticCode::invalid_format, "Migrated package contains an invalid record."});
            return result;
        }
    }

    return result;
}

std::string to_string(PackageIoDiagnosticCode code)
{
    switch (code) {
    case PackageIoDiagnosticCode::cannot_open_file: return "cannot_open_file";
    case PackageIoDiagnosticCode::invalid_format: return "invalid_format";
    }
    return "unknown";
}

} // namespace phoenix::migration
