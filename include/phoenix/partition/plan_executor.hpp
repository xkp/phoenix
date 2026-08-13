#pragma once

// QUARANTINED BEHAVIORAL SCAFFOLDING. Production partition_plan::advance must
// be compiled from the mechanically adapted production source.

#include "phoenix/partition/plan.hpp"
#include "phoenix/partition/trusted_branching.hpp"

#include <functional>
#include <vector>

namespace phoenix::partition::adapted {

enum class AdvanceResult {
    succeed,
    fail,
    branch,
    continue_execution,
};

using ConstraintWorker = std::function<bool(trusted::PartitionView&)>;
using ProposalEvaluator = std::function<bool(trusted::PartitionView&)>;

struct LinkedPlanExecution {
    std::vector<ConstraintWorker> constraints;
    // Indexed by immutable ScheduledPlanStep::insertion_order. Empty entries
    // mean no evaluator, matching production's empty equal_range.
    std::vector<std::vector<ProposalEvaluator>> evaluators;
};

class PlanExecutor {
public:
    PlanExecutor(const PartitionPlan& plan, const trusted::BranchingModel& model,
        const LinkedPlanExecution& linked) noexcept;

    [[nodiscard]] AdvanceResult advance(trusted::PartitionView& view,
        trusted::PartitionViewList& branches) const;

private:
    void select_edges(const SelectEdgesStep& step, trusted::PartitionView& view,
        trusted::PartitionViewList& result) const;

    const PartitionPlan* plan_;
    const trusted::BranchingModel* model_;
    const LinkedPlanExecution* linked_;
};

} // namespace phoenix::partition::adapted
