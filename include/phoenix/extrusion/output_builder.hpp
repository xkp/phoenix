#pragma once

#include "phoenix/working_geometry_builder.hpp"

#include <cstdint>

namespace phoenix::extrusion {

inline constexpr std::int32_t cap_face_tag = -872348234;
inline constexpr std::int32_t side_face_tag = cap_face_tag + 2;

using OutputVertexIndex = WorkingVertexIndex;
using OutputFaceIndex = WorkingFaceIndex;
inline constexpr OutputFaceIndex invalid_output_face_index = invalid_working_face_index;
using OutputBuildResult = WorkingGeometryBuildResult;

// Extrusion compatibility boundary. It exposes the production construction
// vocabulary while the reusable staging and mesh construction live in the
// working-geometry layer. Extrusion classification constants stay here.
class OutputAdapter final : public WorkingGeometryBuilder {
public:
    using WorkingGeometryBuilder::WorkingGeometryBuilder;
};

} // namespace phoenix::extrusion
