#pragma once

#include "phoenix/graph.hpp"
#include "phoenix/migration/production_function_linker.hpp"

#include <map>
#include <string>
#include <vector>

namespace phoenix::migration {

enum class GraphAdaptDiagnosticCode {
    unresolved_link_node,
    unresolved_link_socket,
};

struct GraphAdaptDiagnostic {
    GraphAdaptDiagnosticCode code;
    std::string message;
    FunctionId function_id;
    NodeId node_id = 0;
};

struct ProductionGraphAdaptResult {
    std::map<FunctionId, FunctionDescriptor> functions;
    std::vector<GraphAdaptDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class ProductionGraphAdapter {
public:
    [[nodiscard]] ProductionGraphAdaptResult adapt(
        const ProductionFunctionLinkResult& linked) const;
};

[[nodiscard]] std::string to_string(GraphAdaptDiagnosticCode code);

} // namespace phoenix::migration
