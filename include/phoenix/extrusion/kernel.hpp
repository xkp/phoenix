#pragma once

#include "phoenix/extrusion/profile.hpp"
#include "phoenix/working_geometry_builder.hpp"

#include <string>
#include <vector>

namespace phoenix::extrusion {

enum class KernelErrorCode {
    invalid_input,
    inconsistent_profile_sign,
    unhandled_edge_case,
    excessive_edge_length,
    malformed_output,
};

struct KernelDiagnostic {
    KernelErrorCode code = KernelErrorCode::invalid_input;
    std::string message;
};

struct KernelResult {
    bool success = false;
    WorkingGeometry working;
    std::vector<KernelDiagnostic> diagnostics;
};

// Runs the preserved extrusion geometry kernel only. Publication, consumption,
// repair policy, and command semantics belong to higher layers.
[[nodiscard]] KernelResult run_kernel(
    const KernelExtrusionInput& input,
    RunElementIdAllocator& ids);

[[nodiscard]] std::string to_string(KernelErrorCode code);

} // namespace phoenix::extrusion
