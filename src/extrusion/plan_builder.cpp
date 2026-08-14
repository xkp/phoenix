#include "phoenix/extrusion/plan_builder.hpp"

#include <CGAL/Arrangement_on_surface_2.h>
#include <CGAL/number_utils.h>

#include <cassert>

namespace phoenix::extrusion {

PlanBuilder::Observer::Observer(Arrangement& arrangement)
    : Arrangement::Observer(arrangement)
{
}

void PlanBuilder::Observer::clear_collision()
{
    collision = Arrangement::Vertex_handle{};
}

void PlanBuilder::Observer::after_create_edge(Arrangement::Halfedge_handle halfedge)
{
    halfedge->set_data(current_corner);
    halfedge->twin()->set_data(current_corner);
}

void PlanBuilder::Observer::before_split_edge(
    Arrangement::Halfedge_handle,
    Arrangement::Vertex_handle vertex,
    const Arrangement::X_monotone_curve_2&,
    const Arrangement::X_monotone_curve_2&)
{
    collision = vertex;
}

void PlanBuilder::Observer::after_split_edge(
    Arrangement::Halfedge_handle first,
    Arrangement::Halfedge_handle second)
{
    if (first->data() >= 0) {
        second->set_data(first->data());
        second->twin()->set_data(first->data());
    } else {
        assert(second->data() >= 0);
        first->set_data(second->data());
        first->twin()->set_data(second->data());
    }
}

PlanBuilder::PlanBuilder() : observer_(arrangement_)
{
}

void PlanBuilder::add(Point p1, Point p2, int index, int previous)
{
    segments_[index] = SegmentItem{p1, p2, previous};
    observer_.current_corner = index;
    CGAL::insert(arrangement_, Traits::Curve_2{p1, p2});

    if (observer_.collision == Arrangement::Vertex_handle{}) return;
    const auto point = observer_.collision->point();
    auto incident = observer_.collision->incident_halfedges();
    const auto end = incident;
    do {
        const auto distance = CGAL::squared_distance(
            incident->source()->point(), incident->target()->point());
        if (CGAL::to_double(distance) < 1e-10 && incident->data() != index) {
            int source = -1;
            int target = -1;
            if (CGAL::to_double(CGAL::squared_distance(point, p2)) < 1e-10) source = index;
            else if (CGAL::to_double(CGAL::squared_distance(point, p1)) < 1e-10) source = previous;

            const auto segment = segments_.find(incident->data());
            if (segment != segments_.end()) {
                if (CGAL::to_double(CGAL::squared_distance(point, segment->second.p2)) < 1e-10) {
                    target = segment->first;
                } else if (CGAL::to_double(
                               CGAL::squared_distance(point, segment->second.p1)) < 1e-10) {
                    target = segment->second.previous;
                }
            }
            if (source >= 0 && target >= 0) collisions_[source] = target;
            break;
        }
        ++incident;
    } while (incident != end);
    observer_.clear_collision();
}

void PlanBuilder::clear_collisions()
{
    arrangement_.clear();
    collisions_.clear();
    segments_.clear();
}

void PlanBuilder::rebuild_plan()
{
    arrangement_.clear();
    std::map<int, Arrangement::Vertex_handle> vertices;
    for (const auto& segment : segments_) {
        if (collisions_.find(segment.first) == collisions_.end()) {
            vertices[segment.first] = arrangement_.insert_in_face_interior(
                segment.second.p2, arrangement_.unbounded_face());
        }
    }
    for (const auto& collision : collisions_) vertices[collision.first] = vertices[collision.second];
    for (const auto& segment : segments_) {
        const auto previous = vertices.find(segment.second.previous);
        const auto current = vertices.find(segment.first);
        if (previous == vertices.end() || current == vertices.end()) continue;
        const auto halfedge = arrangement_.insert_at_vertices(
            Traits::X_monotone_curve_2{previous->second->point(), current->second->point()},
            previous->second, current->second);
        halfedge->set_data(segment.first);
        halfedge->twin()->set_data(segment.first);
    }
}

void PlanBuilder::remove_inside_edges()
{
    std::vector<Arrangement::Halfedge_handle> remove;
    for (auto edge = arrangement_.edges_begin(); edge != arrangement_.edges_end(); ++edge) {
        if (!edge->face()->is_unbounded() && !edge->twin()->face()->is_unbounded()) {
            remove.push_back(edge);
        }
    }
    for (const auto edge : remove) arrangement_.remove_edge(edge);
}

void PlanBuilder::get_plan(Plans& results, std::set<int>& complexes)
{
    if (!collisions_.empty()) rebuild_plan();
    remove_inside_edges();
    for (auto face = arrangement_.faces_begin(); face != arrangement_.faces_end(); ++face) {
        if (face->is_unbounded()) continue;
        if (face->number_of_holes() != 0) continue;
        auto edge = face->outer_ccb();
        const auto end = edge;
        Plan plan;
        do {
            if (edge->target()->degree() > 2) {
                auto incident = edge->target()->incident_halfedges();
                const auto incident_end = incident;
                int bounded_count = 0;
                do {
                    if (!incident->face()->is_unbounded()
                        || !incident->twin()->face()->is_unbounded()) ++bounded_count;
                    ++incident;
                } while (incident != incident_end);
                if (bounded_count > 2) complexes.insert(edge->data());
            }
            plan.push_back(edge->data());
            ++edge;
        } while (edge != end);
        results.push_back(std::move(plan));
    }
}

} // namespace phoenix::extrusion
