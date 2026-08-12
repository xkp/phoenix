#pragma once

#include "phoenix/extrusion/kernel.hpp"
#include "phoenix/extrusion/output_builder.hpp"
#include "phoenix/extrusion/plan_builder.hpp"

#include <CGAL/Cartesian.h>
#include <CGAL/Cartesian_converter.h>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/intersections.h>
#include <CGAL/number_utils.h>
#include <CGAL/squared_distance_2.h>
#include <CGAL/squared_distance_3.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace phoenix::extrusion::detail {

using DoubleKernel = CGAL::Cartesian<double>;
using ExactKernel = CGAL::Exact_predicates_exact_constructions_kernel;
using point2 = DoubleKernel::Point_2;
using segment2 = DoubleKernel::Segment_2;
using vec2 = DoubleKernel::Vector_2;
using line2 = DoubleKernel::Line_2;
using point3 = DoubleKernel::Point_3;
using segment3 = DoubleKernel::Segment_3;
using triangle3 = DoubleKernel::Triangle_3;
using vec3 = DoubleKernel::Vector_3;
using line3 = DoubleKernel::Line_3;
using plane3 = DoubleKernel::Plane_3;
using exact_point2 = ExactKernel::Point_2;
using exact_segment2 = ExactKernel::Segment_2;
using exact_line2 = ExactKernel::Line_2;
using exact_point3 = ExactKernel::Point_3;
using exact_vec3 = ExactKernel::Vector_3;
using exact_line3 = ExactKernel::Line_3;
using exact_plane3 = ExactKernel::Plane_3;
using profile_ref = ProfileRef;
using uniqueid = std::int64_t;
using incremental_builder = OutputAdapter;
using face3 = OutputFaceIndex;
using extrude_plan_builder = PlanBuilder;

class kernel_failure final : public std::runtime_error {
public:
    kernel_failure(KernelErrorCode code, const char* message)
        : std::runtime_error(message), code(code) {}
    KernelErrorCode code;
};

struct geometry_math {
    static void normalize(vec3& value)
    {
        const auto length = std::sqrt(CGAL::to_double(value.squared_length()));
        if (length != 0.0) value = value / length;
    }

    static double angle_between(const vec3& left, const vec3& right)
    {
        const auto denominator = std::sqrt(CGAL::to_double(
            left.squared_length() * right.squared_length()));
        if (denominator == 0.0) return 0.0;
        const auto cosine = std::clamp(
            CGAL::to_double(left * right) / denominator, -1.0, 1.0);
        return std::acos(cosine);
    }
};

class ExactProjector {
public:
    explicit ExactProjector(exact_plane3 plane, bool flip)
        : plane_(flip ? plane.opposite() : plane), origin_(plane_.point()),
          base1_(plane_.base1()), base2_(plane_.base2())
    {
        const auto length2 = base1_.squared_length();
        if (length2 == 0) throw kernel_failure(
            KernelErrorCode::unhandled_edge_case, "Degenerate extrusion projection plane.");
        const auto scale = std::sqrt(CGAL::to_double(ExactKernel::FT(1) / length2));
        base1_ = base1_ * scale;
        base2_ = base2_ * scale;
    }

    [[nodiscard]] exact_point2 to_2d(const exact_point3& point) const
    {
        ExactKernel::FT alpha, beta, gamma;
        const auto normal = plane_.orthogonal_vector();
        const auto delta = point - origin_;
        CGAL::solve(base1_.x(), base1_.y(), base1_.z(),
            base2_.x(), base2_.y(), base2_.z(),
            normal.x(), normal.y(), normal.z(),
            delta.x(), delta.y(), delta.z(), alpha, beta, gamma);
        return {alpha, -beta};
    }

private:
    exact_plane3 plane_;
    exact_point3 origin_;
    ExactKernel::Vector_3 base1_;
    ExactKernel::Vector_3 base2_;
};

} // namespace phoenix::extrusion::detail
