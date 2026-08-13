#pragma once

#include "phoenix/partition/ported/compatibility.hpp"
#include "phoenix/partition/sampling.hpp"

#include <climits>
#include <map>
#include <optional>
#include <vector>

namespace phoenix::partition::ported {

// Mechanically adapted from production backend/segment_repository.h and
// backend/partition/partition_solver.h (SHA-256
// F642705894F2FF886F3CE9A4E94F9747411AA90D0C33C0C60E7344ED29D948AB).

struct repo_segment_id
{
    repo_segment_id() : value(-1) {}
    explicit repo_segment_id(int segment_id) : value(segment_id) {}
    bool empty() const { return value < 0; }
    bool valid() const { return value >= 0; }
    void clear() { value = -1; }
    bool operator==(const repo_segment_id& rhs) const { return value == rhs.value; }
    bool operator<(const repo_segment_id& rhs) const { return value < rhs.value; }
    bool operator>(const repo_segment_id& rhs) const { return value > rhs.value; }
    int value;
};

enum cut_segment_type_
{
    BASE_SEGMENT = -1,
    CUT_SEGMENT = 0,
    SOURCE_LEFT = 1,
    SOURCE_RIGHT = 2,
    TARGET_LEFT = 3,
    TARGET_RIGHT = 4,
};

struct cut_segment_type
{
    cut_segment_type() : value(BASE_SEGMENT) {}
    cut_segment_type(cut_segment_type_ value_) : value(value_) {}
    void set(int value_) { value = static_cast<cut_segment_type_>(value_ % 5); }
    bool is_base() const { return value == BASE_SEGMENT; }
    bool is_cut() const { return value == CUT_SEGMENT; }
    bool is_result() const { return value >= SOURCE_LEFT && value <= TARGET_RIGHT; }
    bool is_source() const { return value == SOURCE_LEFT || value == SOURCE_RIGHT; }
    bool is_left() const { return value == SOURCE_LEFT || value == TARGET_LEFT; }
    cut_segment_type_ value;
};

struct cut_segment_id : public repo_segment_id
{
    cut_segment_id() : repo_segment_id() {}
    explicit cut_segment_id(int segment_id) : repo_segment_id(segment_id) {}
    cut_segment_id cut_result(cut_segment_type type) const
    { return cut_segment_id(value + type.value); }
};

struct segment_info
{
    cut_segment_id id;
    compat::point2 src;
    compat::point2 tgt;
    compat::point2 orig_src;
    compat::point2 orig_tgt;
    repo_edge2 redge;

    segment_info(cut_segment_id id_, const repo_edge2& edge) :
        id(id_), src(edge.segment.source()), tgt(edge.segment.target()),
        orig_src(edge.segment.source()), orig_tgt(edge.segment.target()), redge(edge) {}
    segment_info(cut_segment_id id_, const compat::point2& src_,
        const compat::point2& tgt_) :
        id(id_), src(src_), tgt(tgt_), orig_src(src_), orig_tgt(tgt_), redge() {}
    void reset() { src = orig_src; tgt = orig_tgt; }
    compat::segment2 segment() { return compat::segment2(orig_src, orig_tgt); }
    bool collapsed() { return src == tgt; }
    bool restrict_line(compat::line2& l);
};

typedef std::vector<segment_info> seg_info_list;
typedef std::map<repo_segment_id, segment_info> seg_info_map;

struct angle_range
{
    double min_angle;
    double max_angle;
    angle_range() : min_angle(INT_MIN), max_angle(INT_MAX) {}
    angle_range(double mina, double maxa) : min_angle(wrap(mina)), max_angle(wrap(maxa)) {}
    static inline double wrap(double angle)
    {
        const auto pi = std::acos(-1.0);
        angle = fmod(angle, 2 * pi);
        return angle > pi ? angle - 2 * pi :
            (angle <= -pi ? angle + 2 * pi : angle);
    }
    void reset() { min_angle = INT_MIN; max_angle = INT_MAX; }
    bool is_restricted() const { return min_angle > INT_MIN; }
    bool inside(double angle) const
    {
        if (min_angle <= max_angle)
            return (min_angle - 1e-5) <= angle && angle <= (max_angle + 1e-5);
        return (min_angle - 1e-5) <= angle || angle <= (max_angle + 1e-5);
    }
    angle_range opposite() const
    { const auto pi = std::acos(-1.0); return angle_range(min_angle + pi, max_angle + pi); }
    bool is_opposite(const angle_range& other) const
    {
        const auto pi = std::acos(-1.0);
        angle_range opposite_(other.min_angle + pi, other.max_angle + pi);
        return fabs(min_angle - opposite_.min_angle) <= 1e-5
            && fabs(max_angle - opposite_.max_angle) <= 1e-5;
    }
    bool intersect(const angle_range& other, angle_range& result) const
    {
        result.reset();
        if (inside(other.min_angle)) result.min_angle = other.min_angle;
        else if (other.inside(min_angle)) result.min_angle = min_angle;
        else return false;
        result.max_angle = inside(other.max_angle) ? other.max_angle : max_angle;
        return true;
    }
};

struct partition_view
{
    const ArrangementSegmentRepository* repo;
    CompatibilityRandomStream* rand;
    int cut_index;
    int instruction_index;
    seg_info_map segments;
    double error;
    double quality;
    std::vector<angle_range> angles;
    bool has_errors;
    std::optional<cut_segment_id> first_error;
    std::optional<compat::line2> cut_middle_line;

    partition_view(const ArrangementSegmentRepository& repo_,
        CompatibilityRandomStream& rand_) :
        repo(&repo_), rand(&rand_), instruction_index(0), cut_index(-1),
        error(1e10), angles(), has_errors(false), quality(0), cut_middle_line()
    {
        angles.push_back(angle_range());
    }

    partition_view(const partition_view& other) :
        repo(other.repo), rand(other.rand),
        instruction_index(other.instruction_index), cut_index(other.cut_index),
        segments(other.segments), error(other.error), angles(other.angles),
        has_errors(false), quality(other.quality),
        cut_middle_line(other.cut_middle_line)
    {
    }

    void copy(partition_view& other)
    {
        cut_index = other.cut_index;
        instruction_index = other.instruction_index;
        error = other.error;
        segments = other.segments;
        angles = other.angles;
        quality = 0;
        cut_middle_line.reset();
    }

    const segment_info* segment(cut_segment_id id) const
    {
        auto it = segments.find(id);
        return it == segments.end() ? nullptr : &it->second;
    }

    segment_info* segment(cut_segment_id id)
    {
        auto it = segments.find(id);
        return it == segments.end() ? nullptr : &it->second;
    }

    bool has_segment(cut_segment_id id) { return segment(id) != nullptr; }

    bool has_edge(const repo_edge2& he)
    {
        for (auto it : segments)
            if (it.second.redge == he) return true;
        return false;
    }

    segment_info* add_segment(const segment_info& seg_info)
    {
        segments.erase(seg_info.id);
        auto pr = segments.insert(seg_info_map::value_type(seg_info.id, seg_info));
        return &(pr.first->second);
    }

    bool is_angle_restricted()
    { return angles.size() != 1 || angles[0].is_restricted(); }

    void reset()
    {
        reset_angle_restriction();
        for (auto it = segments.begin(); it != segments.end(); it++)
            it->second.reset();
    }

    void reset_angle_restriction()
    {
        angles.clear();
        angles.push_back(angle_range());
    }

    void restrict_angles(double ref_angle, double mina, double maxa)
    {
        angle_range range1(ref_angle + mina, ref_angle + maxa);
        angle_range range2(ref_angle - maxa, ref_angle - mina);
        bool second_range = true;

        if (range1.inside(range2.max_angle)) {
            range1.min_angle = range2.min_angle;
            second_range = false;
        } else if (range1.is_opposite(range2)) {
            second_range = false;
        }

        auto range1_opp = range1.opposite();
        auto range2_opp = range2.opposite();
        std::vector<angle_range> new_angles;
        for (auto it = angles.begin(); it != angles.end(); it++) {
            angle_range result;
            if (it->intersect(range1, result) || it->intersect(range1_opp, result))
                new_angles.push_back(result);
            if (second_range)
                if (it->intersect(range2, result) || it->intersect(range2_opp, result))
                    new_angles.push_back(result);
        }
        angles.swap(new_angles);
    }

    void notify_error(cut_segment_id error_id = cut_segment_id(0))
    {
        if (!has_errors) {
            has_errors = true;
            first_error = error_id;
        }
    }
};

typedef std::vector<partition_view> partition_view_list;

} // namespace phoenix::partition::ported
