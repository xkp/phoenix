#pragma once

#include "phoenix/working_geometry.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace phoenix::smooth {

enum class SubdivisionScheme { bilinear, catmark, loop };
enum class BoundaryInterpolation { none, edge_only, edge_and_corner };

struct SubdivisionOptions {
    SubdivisionScheme scheme = SubdivisionScheme::bilinear;
    std::uint32_t max_refinement_level = 2;
    BoundaryInterpolation boundary_interpolation = BoundaryInterpolation::edge_only;
    std::int32_t smoothing_group = 0;
};

struct SubdivisionDiagnostic {
    std::string message;
};

struct SubdivisionResult {
    CanonicalGeometryRef geometry;
    std::vector<SubdivisionDiagnostic> diagnostics;
    [[nodiscard]] bool success() const noexcept { return geometry != nullptr; }
};

// Invocation-local adapter around the production OpenSubdiv refinement path.
// Refined faces inherit the label of their coarse ancestor. Production does
// not currently propagate directed-edge labels through subdivision.
[[nodiscard]] SubdivisionResult subdivide(
    const CanonicalGeometry& source,
    RunElementIdAllocator& ids,
    const SubdivisionOptions& options = {});

} // namespace phoenix::smooth
