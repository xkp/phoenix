#include "phoenix/migration/production_project_discovery.hpp"

#include <fstream>
#include <regex>
#include <sstream>

namespace phoenix::migration {
namespace {

struct ScanRoot {
    std::filesystem::path path;
    bool allow_project_candidates = true;
};

bool looks_like_function_id(const std::string& name)
{
    static const std::regex pattern{
        R"(^[^@\\/:*?"<>|]+@[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$)"};
    return std::regex_match(name, pattern);
}

std::string name_from_id(const FunctionId& id)
{
    const auto at = id.find('@');
    return at == std::string::npos ? id : id.substr(0, at);
}

std::string read_file(const std::filesystem::path& path, std::vector<DiscoveryDiagnostic>& diagnostics)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics.push_back(DiscoveryDiagnostic{
            DiscoveryDiagnosticCode::unreadable_file,
            "Could not read production project file.",
            path,
            {}});
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::set<FunctionId> extract_string_values(const std::string& text, const std::string& key)
{
    std::set<FunctionId> values;
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*"([^"]+)")regex"};
    for (std::sregex_iterator it{text.begin(), text.end(), pattern}, end; it != end; ++it) {
        values.insert((*it)[1].str());
    }
    return values;
}

std::string extract_string_value(const std::string& text, const std::string& key)
{
    const std::regex pattern{"\"" + key + R"regex("\s*:\s*"([^"]*)")regex"};
    const std::sregex_iterator it{text.begin(), text.end(), pattern};
    return it == std::sregex_iterator{} ? std::string{} : (*it)[1].str();
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

std::set<FunctionId> extract_manifest_function_ids(const std::string& text)
{
    std::set<FunctionId> ids;
    const auto all_ids = extract_string_values(text, "id");
    for (const auto& id : all_ids) {
        if (looks_like_function_id(id)) ids.insert(id);
    }
    return ids;
}

std::set<FunctionId> extract_nodes_function_ids(const std::string& text)
{
    std::set<FunctionId> ids;
    const auto nodes_text = extract_array_text(text, "nodes");
    for (const auto& object : extract_object_texts(nodes_text)) {
        if (extract_bool_value(object, "disabled", false)) continue;
        const auto value = extract_string_value(object, "file");
        if (looks_like_function_id(value)) ids.insert(value);
    }
    return ids;
}

bool contains_manifest_key(const std::string& text, const std::string& key)
{
    return text.find("\"" + key + "\"") != std::string::npos;
}

std::string normalized_key(const std::filesystem::path& path)
{
    return path.lexically_normal().string();
}

void add_scan_root(
    std::vector<ScanRoot>& scan_roots,
    std::set<std::string>& seen,
    const std::filesystem::path& path,
    bool allow_project_candidates)
{
    if (!std::filesystem::exists(path)) return;
    const auto key = normalized_key(path);
    if (!seen.insert(key).second) return;
    scan_roots.push_back(ScanRoot{path, allow_project_candidates});
}

std::vector<ScanRoot> expand_scan_roots(const std::vector<std::filesystem::path>& roots)
{
    std::vector<ScanRoot> scan_roots;
    std::set<std::string> seen;

    for (const auto& root : roots) {
        if (!std::filesystem::exists(root)) continue;
        const auto directory = std::filesystem::is_directory(root)
            ? root
            : root.parent_path();
        const auto name = directory.filename().string();
        const auto parent_name = directory.parent_path().filename().string();

        if (looks_like_function_id(name) && parent_name == "projects") {
            add_scan_root(scan_roots, seen, directory, true);
            const auto sibling_functions = directory.parent_path().parent_path() / "functions";
            const auto matching_function_set = sibling_functions / name;
            add_scan_root(scan_roots, seen,
                std::filesystem::exists(matching_function_set) ? matching_function_set : sibling_functions,
                false);
            continue;
        }

        if (looks_like_function_id(name) && parent_name == "functions") {
            add_scan_root(scan_roots, seen, directory, false);
            continue;
        }

        if (name == "projects") {
            add_scan_root(scan_roots, seen, directory, true);
            add_scan_root(scan_roots, seen, directory.parent_path() / "functions", false);
            continue;
        }

        if (name == "functions") {
            add_scan_root(scan_roots, seen, directory, false);
            continue;
        }

        if (std::filesystem::exists(directory / "projects")
            || std::filesystem::exists(directory / "functions")) {
            add_scan_root(scan_roots, seen, directory / "projects", true);
            add_scan_root(scan_roots, seen, directory / "functions", false);
            continue;
        }

        add_scan_root(scan_roots, seen, directory, true);
    }

    return scan_roots;
}

} // namespace

ProductionProjectDiscovery ProductionProjectDiscoverer::discover(
    const std::vector<std::filesystem::path>& roots) const
{
    ProductionProjectDiscovery result;

    for (const auto& scan_root : expand_scan_roots(roots)) {
        for (std::filesystem::recursive_directory_iterator it{scan_root.path}, end; it != end; ++it) {
            if (!it->is_regular_file()) continue;
            const auto path = it->path();
            if (path.extension() == ".nodes" || path.has_extension()) continue;
            const auto id = path.filename().string();
            if (!looks_like_function_id(id)) continue;

            const auto text = read_file(path, result.diagnostics);
            if (text.empty()) continue;
            if (!contains_manifest_key(text, "sceneId")) continue;

            const auto nodes_path = path.parent_path() / (id + ".nodes");
            if (!std::filesystem::exists(nodes_path)) {
                result.diagnostics.push_back(DiscoveryDiagnostic{
                    DiscoveryDiagnosticCode::missing_nodes_file,
                    "Production function manifest has no matching .nodes file.",
                    nodes_path,
                    id});
            }

            ProductionFunctionCandidate function;
            function.id = id;
            function.name = name_from_id(id);
            function.manifest_path = path;
            function.nodes_path = nodes_path;
            const auto manifest_ids = extract_manifest_function_ids(text);
            function.imported = text.find(R"("imported":true)") != std::string::npos;

            if (std::filesystem::exists(nodes_path)) {
                const auto nodes_text = read_file(nodes_path, result.diagnostics);
                function.referenced_function_ids = extract_nodes_function_ids(nodes_text);
                result.imported_function_references.insert(
                    function.referenced_function_ids.begin(),
                    function.referenced_function_ids.end());
            }

            result.functions[function.id].push_back(function);

            if (scan_root.allow_project_candidates && path.parent_path().filename() == path.stem()) {
                ProductionProjectCandidate project;
                project.id = id;
                project.name = function.name;
                project.directory = path.parent_path();
                project.manifest_path = path;
                project.nodes_path = nodes_path;
                project.declared_function_ids = manifest_ids;
                result.projects.push_back(std::move(project));
            }
        }
    }

    for (const auto& id : result.imported_function_references) {
        if (result.functions.find(id) == result.functions.end()) {
            result.unresolved_imported_function_ids.insert(id);
        }
    }

    return result;
}

std::string to_string(DiscoveryDiagnosticCode code)
{
    switch (code) {
    case DiscoveryDiagnosticCode::missing_nodes_file: return "missing_nodes_file";
    case DiscoveryDiagnosticCode::unreadable_file: return "unreadable_file";
    }
    return "unknown";
}

} // namespace phoenix::migration
