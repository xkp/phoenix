#pragma once

#include "phoenix/partition/arrangement_segment_repository.hpp"
#include "phoenix/partition/sampling.hpp"

#include <map>
#include <optional>
#include <vector>

namespace phoenix::partition::trusted {

// Direct ports from backend/segment_repository.h and
// backend/partition/partition_solver.h. Keep source correspondence when editing.
struct RepoSegmentId {
    RepoSegmentId() = default;
    explicit RepoSegmentId(std::int32_t segment_id) : value(segment_id) {}

    [[nodiscard]] bool empty() const noexcept { return value < 0; }
    [[nodiscard]] bool valid() const noexcept { return value >= 0; }
    void clear() noexcept { value = -1; }
    [[nodiscard]] bool operator==(const RepoSegmentId& other) const noexcept
    { return value == other.value; }
    [[nodiscard]] bool operator<(const RepoSegmentId& other) const noexcept
    { return value < other.value; }
    [[nodiscard]] bool operator>(const RepoSegmentId& other) const noexcept
    { return value > other.value; }

    std::int32_t value = -1;
};

enum class CutSegmentKind : std::int32_t {
    base = -1,
    cut = 0,
    source_left = 1,
    source_right = 2,
    target_left = 3,
    target_right = 4,
};

struct CutSegmentType {
    CutSegmentType() = default;
    CutSegmentType(CutSegmentKind value_input) : value(value_input) {}

    void set(std::int32_t input)
    { value = static_cast<CutSegmentKind>(input % 5); }
    void set(CutSegmentKind input) { value = input; }
    [[nodiscard]] bool is_base() const { return value == CutSegmentKind::base; }
    [[nodiscard]] bool is_cut() const { return value == CutSegmentKind::cut; }
    [[nodiscard]] bool is_result() const
    {
        return value >= CutSegmentKind::source_left
            && value <= CutSegmentKind::target_right;
    }
    [[nodiscard]] bool is_source() const
    {
        return value == CutSegmentKind::source_left
            || value == CutSegmentKind::source_right;
    }
    [[nodiscard]] bool is_left() const
    {
        return value == CutSegmentKind::source_left
            || value == CutSegmentKind::target_left;
    }

    CutSegmentKind value = CutSegmentKind::base;
};

struct CutSegmentId : RepoSegmentId {
    CutSegmentId() = default;
    explicit CutSegmentId(std::int32_t segment_id) : RepoSegmentId(segment_id) {}
    [[nodiscard]] CutSegmentId cut_result(CutSegmentType type) const
    { return CutSegmentId{value + static_cast<std::int32_t>(type.value)}; }
};

struct SegmentInfo {
    CutSegmentId id;
    ExactPoint2 source;
    ExactPoint2 target;
    ExactPoint2 original_source;
    ExactPoint2 original_target;
    ArrangementRepoEdge repository_edge;

    SegmentInfo(CutSegmentId id_value, const ArrangementRepoEdge& edge);
    SegmentInfo(CutSegmentId id_value,
        const ExactPoint2& source_value, const ExactPoint2& target_value);

    void reset();
    [[nodiscard]] ExactKernel::Segment_2 segment() const;
    [[nodiscard]] bool collapsed() const;
    [[nodiscard]] bool restrict_line(const ExactKernel::Line_2& line);
};

using SegmentInfoMap = std::map<RepoSegmentId, SegmentInfo>;

struct AngleRange {
    AngleRange();
    AngleRange(double minimum_value, double maximum_value);

    static double wrap(double angle);
    void reset();
    [[nodiscard]] bool is_restricted() const;
    [[nodiscard]] bool inside(double angle) const;
    [[nodiscard]] AngleRange opposite() const;
    [[nodiscard]] bool is_opposite(const AngleRange& other) const;
    [[nodiscard]] bool intersect(const AngleRange& other, AngleRange& result) const;

    double minimum_angle;
    double maximum_angle;
};

class PartitionView {
public:
    PartitionView(const ArrangementSegmentRepository& repository,
        CompatibilityRandomStream& random);
    PartitionView(const PartitionView& other);

    void copy(const PartitionView& other);
    [[nodiscard]] const SegmentInfo* segment(CutSegmentId id) const;
    [[nodiscard]] SegmentInfo* segment(CutSegmentId id);
    [[nodiscard]] bool has_segment(CutSegmentId id) const;
    [[nodiscard]] bool has_edge(const ArrangementRepoEdge& edge) const;
    SegmentInfo* add_segment(const SegmentInfo& info);
    [[nodiscard]] bool is_angle_restricted() const;
    void reset();
    void reset_angle_restriction();
    void restrict_angles(double reference_angle, double minimum, double maximum);
    void notify_error(CutSegmentId error_id = CutSegmentId{0});

    const ArrangementSegmentRepository* repository;
    CompatibilityRandomStream* random;
    std::int32_t cut_index = -1;
    std::int32_t instruction_index = 0;
    SegmentInfoMap segments;
    double error = 1e10;
    double quality = 0.0;
    std::vector<AngleRange> angles;
    bool has_errors = false;
    std::optional<CutSegmentId> first_error;
    std::optional<ExactKernel::Line_2> cut_middle_line;
};

} // namespace phoenix::partition::trusted
