#pragma once

#include <CGAL/Arr_extended_dcel.h>
#include <CGAL/Arr_observer.h>
#include <CGAL/Arr_segment_traits_2.h>
#include <CGAL/Arrangement_2.h>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>

#include <map>
#include <set>
#include <vector>

namespace phoenix::extrusion {

class PlanBuilder {
public:
    using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
    using Point = Kernel::Point_2;
    using Segment = Kernel::Segment_2;
    using Traits = CGAL::Arr_segment_traits_2<Kernel>;
    using Dcel = CGAL::Arr_extended_dcel<Traits, int, int, int>;
    using Arrangement = CGAL::Arrangement_2<Traits, Dcel>;
    using Plan = std::vector<int>;
    using Plans = std::vector<Plan>;
    using CollisionMap = std::map<int, int>;

    PlanBuilder();

    void add(Point p1, Point p2, int index, int previous);
    void get_plan(Plans& results, std::set<int>& complexes);
    void clear_collisions();

    [[nodiscard]] const CollisionMap& collisions() const noexcept { return collisions_; }

private:
    class Observer final : public CGAL::Arr_observer<Arrangement> {
    public:
        explicit Observer(Arrangement& arrangement);
        void clear_collision();

        void after_create_edge(Arrangement::Halfedge_handle halfedge) override;
        void before_split_edge(
            Arrangement::Halfedge_handle edge,
            Arrangement::Vertex_handle vertex,
            const Arrangement::X_monotone_curve_2& first,
            const Arrangement::X_monotone_curve_2& second) override;
        void after_split_edge(
            Arrangement::Halfedge_handle first,
            Arrangement::Halfedge_handle second) override;

        int current_corner = -1;
        Arrangement::Vertex_handle collision;
    };

    struct SegmentItem {
        Point p1;
        Point p2;
        int previous = -1;
    };

    void rebuild_plan();
    void remove_inside_edges();

    Arrangement arrangement_;
    Observer observer_;
    CollisionMap collisions_;
    std::map<int, SegmentItem> segments_;
};

} // namespace phoenix::extrusion
