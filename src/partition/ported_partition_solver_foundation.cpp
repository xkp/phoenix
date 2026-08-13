#include "phoenix/partition/ported/partition_solver_foundation.h"

#include <CGAL/intersections.h>

#include <variant>

namespace phoenix::partition::ported {

bool segment_info::restrict_line(compat::line2& l)
{
    //segments must remain on the positive side of the line
    compat::segment2 s(src, tgt);
    bool src_negative = l.has_on_negative_side(src);
    bool tgt_negative = l.has_on_negative_side(tgt);

    // Mechanical CGAL 6.2 change: Object/object_cast became optional variant.
    auto iobj = CGAL::intersection(s, l);
    const compat::point2* ipoint = iobj
        ? std::get_if<compat::point2>(&*iobj) : nullptr;
    if (!ipoint)
        return !src_negative;

    if (src_negative)
        src = *ipoint;
    if (tgt_negative)
        tgt = *ipoint;
    return true;
}

} // namespace phoenix::partition::ported
