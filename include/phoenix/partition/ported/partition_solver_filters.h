#pragma once

#include "phoenix/partition/ported/compatibility.hpp"

namespace phoenix::partition::ported {

// Mechanically adapted from production backend/partition/
// partition_solver_filters.h, SHA-256
// B9EA13EC61D3A204E10950DC25F1C22E6D20E3A9A611338FEB6AA5350315118F.
// VM lookup and model registration are Phoenix boundary responsibilities.

struct base_length_pct_filter
{
    linked_value min_dist_pct;
    linked_value max_dist_pct;
    double _min_dist_pct;
    double _max_dist_pct;
    bool built;

    base_length_pct_filter(linked_value min_dist_pct_, linked_value max_dist_pct_) :
        built(false),
        min_dist_pct(min_dist_pct_),
        max_dist_pct(max_dist_pct_)
    {
    }

    void build()
    {
        // td: variable not found error?
        // square and percent
        _min_dist_pct = min_dist_pct.value() * 0.01;
        _max_dist_pct = max_dist_pct.value() * 0.01;
        assert(_min_dist_pct >= 0 && _max_dist_pct >= 0);

        _min_dist_pct *= _min_dist_pct;
        _max_dist_pct *= _max_dist_pct;
        built = true;
    }

    bool operator ()(const repo_edge2& he1, const repo_edge2& he2)
    {
        //if (!built) do not cache
        build();

        double d = CGAL::to_double(he1.segment.squared_length());
        double ref = CGAL::to_double(he2.segment.squared_length());
        return d >= _min_dist_pct * ref && d <= _max_dist_pct * ref;
    }
};

struct base_angle_filter
{
    linked_value min_angle;
    linked_value max_angle;
    double _min_angle;
    double _max_angle;
    bool built;

    base_angle_filter(linked_value min_angle_, linked_value max_angle_):
        built(false),
        min_angle(min_angle_),
        max_angle(max_angle_)
    {
    }

    void build()
    {
        _min_angle = min_angle.value() - 1e-5;
        _max_angle = max_angle.value() + 1e-5;
        //td: check values existence
        built = true;
    }

    bool operator ()(const repo_edge2& he1, const repo_edge2& he2)
    {
        //if (!built) do not cache
        build();

        double angle = angle_between(he1.segment, he2.segment);
        double angle_in_degrees = angle * 180 / std::acos(-1.0);
        if (angle_in_degrees < 0)
            angle_in_degrees += 180;
        if (angle_in_degrees >= 180)
            angle_in_degrees -= 180;
        if (angle_in_degrees > 90)
            angle_in_degrees = 180 - angle_in_degrees;

        if (angle_in_degrees >= _min_angle && angle_in_degrees <= _max_angle)
            return true;
        return false;
    }
};

} // namespace phoenix::partition::ported
