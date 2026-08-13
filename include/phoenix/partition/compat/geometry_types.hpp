#pragma once

#include "phoenix/partition/working_arrangement.hpp"

#include <vector>

namespace phoenix::partition::compat {

// Compatibility vocabulary for mechanically adapting the production partition
// sources. These aliases intentionally mirror DEFAULT_CGAL_TYPES() and
// DEFAULT_ARRANGEMENT_TYPES(); they do not introduce another geometry model.
using Kernel = ExactKernel;
using point2 = Kernel::Point_2;
using segment2 = Kernel::Segment_2;
using vec2 = Kernel::Vector_2;
using ray2 = Kernel::Ray_2;
using dir2 = Kernel::Direction_2;
using line2 = Kernel::Line_2;
using circle2 = Kernel::Circle_2;

using point2_list = std::vector<point2>;
using segment2_list = std::vector<segment2>;

using arrangement2 = ExactArrangement;
using edge2_circulator = arrangement2::Ccb_halfedge_circulator;
using edge2_iterator = arrangement2::Edge_iterator;
using face2_iterator = arrangement2::Face_iterator;
using vertex2_iterator = arrangement2::Vertex_iterator;
using vertex2 = arrangement2::Vertex_handle;
using const_vertex2 = arrangement2::Vertex_const_handle;
using edge2 = arrangement2::Halfedge_handle;
using const_edge2 = arrangement2::Halfedge_const_handle;
using face2 = arrangement2::Face_handle;
using const_face2 = arrangement2::Face_const_handle;

using vertex2_list = std::vector<vertex2>;
using edge2_list = std::vector<edge2>;
using face2_list = std::vector<face2>;

inline constexpr LabelId label_unbounded = static_cast<LabelId>(-1000);
inline constexpr LabelId label_layout = static_cast<LabelId>(-1001);

} // namespace phoenix::partition::compat
