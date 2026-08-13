#pragma once

#include "phoenix/partition/geometry.h"
#include "phoenix/inset/ported/cleanup_face.h"
#include "phoenix/merge/ported/production/merge_borders3.h"
#include "phoenix/merge/ported/production/merge_faces.h"

#include <string>
#include <utility>

namespace phoenix::merge {

struct Options {
    bool merge_borders = false;
    bool join_vertices = false;
    bool merge_faces = false;
    bool merge_faces_labels = false;
    bool join_collinear = false;
};

struct PipelineResult {
    geometry::polyhedron3 mesh;
    std::string error;

    [[nodiscard]] bool success() const noexcept { return error.empty(); }
};

// Runs every mutating production operation on an invocation-local candidate.
// Callers publish the returned mesh only after success.
inline PipelineResult run_production_pipeline(const geometry::polyhedron3& input,
    const Options& options, vertex_id_generator vertices, edge_id_generator edges)
{
    PipelineResult result;
    if (!options.merge_borders && !options.merge_faces && !options.join_collinear) {
        result.error = "Merge requires at least one producing operation.";
        return result;
    }

    geometry::polyhedron3 candidate(input);
    if (options.merge_borders) {
        geometry::face3_list faces;
        for (auto face = candidate.facets_begin(); face != candidate.facets_end(); ++face)
            faces.push_back(face);
        geometry::polyhedron3 rebuilt;
        bool failed = false;
        error_function on_error = [&](solver_error& error) {
            failed = true;
            result.error = error.message().empty()
                ? "Production merge border reconstruction failed."
                : error.message();
        };
        using BorderKernel = ::merge_borders3<geometry::Kernel,
            geometry::arrangement2, geometry::polyhedron3>;
        BorderKernel::run(rebuilt, faces, options.join_vertices, on_error,
            std::move(vertices), std::move(edges));
        if (failed) return result;
        candidate = std::move(rebuilt);
    }

    if (options.merge_faces) {
        using FaceKernel = ::merge_faces<geometry::Kernel,
            geometry::arrangement2, geometry::polyhedron3>;
        FaceKernel::run(candidate, options.merge_faces_labels);
    }

    if (options.join_collinear) {
        using CleanupKernel = ::cleanup_face3<geometry::Kernel, geometry::polyhedron3>;
        CleanupKernel::request request;
        request.merge_predicate = [](geometry::edge3 first, geometry::edge3 second) {
            return first->data.label == second->data.label
                && first->opposite()->data.label == second->opposite()->data.label;
        };
        CleanupKernel::run(candidate, request);
    }

    if (!candidate.is_valid(false, 0)) {
        result.error = "Production merge pipeline produced invalid topology.";
        return result;
    }
    result.mesh = std::move(candidate);
    return result;
}

} // namespace phoenix::merge
