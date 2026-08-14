#pragma once

#include "phoenix/geometry.hpp"

#include <array>
#include <optional>

namespace phoenix::scripting {

struct Vector3d { double x=0, y=0, z=0; };
struct Direction3d { double x=1, y=0, z=0; };
struct Segment3d { Point3d source, target; };
struct Line3d { Point3d point; Direction3d direction; };
struct Ray3d { Point3d source; Direction3d direction; };
struct Triangle3d { std::array<Point3d,3> vertices; };
struct Plane3d { double a=0, b=0, c=1, d=0; };

struct Transform3d {
    std::array<double,16> matrix{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    [[nodiscard]] static Transform3d identity() noexcept;
    [[nodiscard]] static Transform3d translation(Vector3d value) noexcept;
    [[nodiscard]] static Transform3d scaling(double x,double y,double z) noexcept;
    [[nodiscard]] static Transform3d rotation(Direction3d axis,double radians) noexcept;
};

[[nodiscard]] bool finite(Point3d value) noexcept;
[[nodiscard]] bool finite(Vector3d value) noexcept;
[[nodiscard]] Vector3d vector(Point3d from,Point3d to) noexcept;
[[nodiscard]] Point3d add(Point3d point,Vector3d value) noexcept;
[[nodiscard]] Point3d subtract(Point3d point,Vector3d value) noexcept;
[[nodiscard]] Vector3d add(Vector3d left,Vector3d right) noexcept;
[[nodiscard]] Vector3d subtract(Vector3d left,Vector3d right) noexcept;
[[nodiscard]] Vector3d multiply(Vector3d value,double scalar) noexcept;
[[nodiscard]] Vector3d divide(Vector3d value,double scalar) noexcept;
[[nodiscard]] double dot(Vector3d left,Vector3d right) noexcept;
[[nodiscard]] Vector3d cross(Vector3d left,Vector3d right) noexcept;
[[nodiscard]] double squared_length(Vector3d value) noexcept;
[[nodiscard]] std::optional<Direction3d> direction(Vector3d value) noexcept;
[[nodiscard]] Direction3d opposite(Direction3d value) noexcept;

[[nodiscard]] Point3d transform(Point3d point,const Transform3d& value) noexcept;
[[nodiscard]] Vector3d transform(Vector3d vector,const Transform3d& value) noexcept;
[[nodiscard]] Direction3d transform(Direction3d direction,const Transform3d& value) noexcept;
[[nodiscard]] Transform3d compose(const Transform3d& left,const Transform3d& right) noexcept;
[[nodiscard]] std::optional<Transform3d> inverse(const Transform3d& value) noexcept;
[[nodiscard]] bool is_even(const Transform3d& value) noexcept;

[[nodiscard]] double squared_length(Segment3d value) noexcept;
[[nodiscard]] bool is_degenerate(Segment3d value) noexcept;
[[nodiscard]] bool has_on(Segment3d segment,Point3d point);
[[nodiscard]] Point3d min(Segment3d value) noexcept;
[[nodiscard]] Point3d max(Segment3d value) noexcept;
[[nodiscard]] std::optional<Point3d> vertex(Segment3d value,std::size_t index) noexcept;
[[nodiscard]] Vector3d to_vector(Segment3d value) noexcept;
[[nodiscard]] std::optional<Direction3d> direction(Segment3d value) noexcept;
[[nodiscard]] Segment3d opposite(Segment3d value) noexcept;
[[nodiscard]] Line3d supporting_line(Segment3d value) noexcept;
[[nodiscard]] Segment3d transform(Segment3d value,const Transform3d& transform) noexcept;
[[nodiscard]] Point3d projection(Line3d line,Point3d point) noexcept;
[[nodiscard]] Plane3d perpendicular_plane(Line3d line,Point3d point) noexcept;
[[nodiscard]] Vector3d to_vector(Line3d line) noexcept;
[[nodiscard]] bool has_on(Line3d line,Point3d point);
[[nodiscard]] Line3d opposite(Line3d value) noexcept;
[[nodiscard]] Line3d transform(Line3d value,const Transform3d& transform) noexcept;
[[nodiscard]] bool has_on(Ray3d ray,Point3d point);
[[nodiscard]] Line3d supporting_line(Ray3d value) noexcept;
[[nodiscard]] Ray3d opposite(Ray3d value) noexcept;
[[nodiscard]] Ray3d transform(Ray3d value,const Transform3d& transform) noexcept;
[[nodiscard]] double squared_area(Triangle3d triangle) noexcept;
[[nodiscard]] bool is_degenerate(Triangle3d triangle);
[[nodiscard]] bool has_on(Triangle3d triangle,Point3d point);
[[nodiscard]] std::optional<Point3d> vertex(Triangle3d value,std::size_t index) noexcept;
[[nodiscard]] Plane3d supporting_plane(Triangle3d value) noexcept;
[[nodiscard]] Triangle3d transform(Triangle3d value,const Transform3d& transform) noexcept;
[[nodiscard]] bool is_degenerate(Plane3d plane) noexcept;
[[nodiscard]] bool has_on(Plane3d plane,Point3d point);
[[nodiscard]] int oriented_side(Plane3d plane,Point3d point);
[[nodiscard]] Point3d projection(Plane3d plane,Point3d point) noexcept;
[[nodiscard]] Plane3d opposite(Plane3d value) noexcept;
[[nodiscard]] Vector3d orthogonal_vector(Plane3d value) noexcept;
[[nodiscard]] std::optional<Direction3d> orthogonal_direction(Plane3d value) noexcept;
[[nodiscard]] Point3d point(Plane3d value) noexcept;
[[nodiscard]] Vector3d base1(Plane3d value) noexcept;
[[nodiscard]] Vector3d base2(Plane3d value) noexcept;
[[nodiscard]] Line3d perpendicular_line(Plane3d value,Point3d point) noexcept;
[[nodiscard]] Plane3d transform(Plane3d value,const Transform3d& transform) noexcept;

} // namespace phoenix::scripting
