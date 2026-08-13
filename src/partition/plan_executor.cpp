#include "phoenix/partition/plan_executor.hpp"

#include <variant>

namespace phoenix::partition::adapted {
namespace {

trusted::CutSegmentId cut_id(SegmentRef value)
{
    return trusted::CutSegmentId{value.value()};
}

} // namespace

PlanExecutor::PlanExecutor(const PartitionPlan& plan,
    const trusted::BranchingModel& model,
    const LinkedPlanExecution& linked) noexcept
    : plan_(&plan), model_(&model), linked_(&linked)
{
}

AdvanceResult PlanExecutor::advance(trusted::PartitionView& view,
    trusted::PartitionViewList& branches) const
{
    const auto steps = plan_->ordered_steps();
    if (view.instruction_index >= static_cast<std::int32_t>(steps.size()))
        return AdvanceResult::succeed;

    const auto& step = steps[static_cast<std::size_t>(view.instruction_index++)];
    trusted::PartitionViewList proposals;
    bool is_brancher = false;
    if (const auto* select = std::get_if<SelectEdgesStep>(&step.payload)) {
        select_edges(*select, view, proposals);
        is_brancher = true;
    } else if (const auto* apply = std::get_if<ApplyCutStep>(&step.payload)) {
        const auto* cut = model_->get_cut(apply->cut_id);
        if (cut != nullptr) {
            if (view.has_segment(cut->segment)) {
                view.reset();
                proposals.push_back(view);
            } else {
                ++view.cut_index;
                model_->branch(view, *cut, proposals);
                for (auto& proposal : proposals) proposal.reset();
            }
        }
        is_brancher = true;
    } else if (const auto* constraint = std::get_if<ConstraintStep>(&step.payload)) {
        if (constraint->constraint_index < 0
            || static_cast<std::size_t>(constraint->constraint_index)
                >= linked_->constraints.size())
            return AdvanceResult::fail;
        return linked_->constraints[static_cast<std::size_t>(
            constraint->constraint_index)](view)
            ? AdvanceResult::continue_execution : AdvanceResult::fail;
    }

    if (!is_brancher || proposals.empty()) return AdvanceResult::fail;
    const auto old_branch_count = branches.size();
    const auto evaluator_index = static_cast<std::size_t>(step.insertion_order);
    const auto has_evaluators = step.insertion_order >= 0
        && evaluator_index < linked_->evaluators.size()
        && !linked_->evaluators[evaluator_index].empty();
    for (auto& proposal : proposals) {
        bool valid = true;
        if (has_evaluators) {
            for (const auto& evaluator : linked_->evaluators[evaluator_index]) {
                if (!evaluator(proposal)) {
                    valid = false;
                    break;
                }
            }
        }
        if (valid) branches.push_back(proposal);
    }
    return branches.size() == old_branch_count
        ? AdvanceResult::fail : AdvanceResult::branch;
}

void PlanExecutor::select_edges(const SelectEdgesStep& step,
    trusted::PartitionView& view, trusted::PartitionViewList& result) const
{
    const auto source = cut_id(step.source);
    const auto target = step.target.has_value()
        ? cut_id(*step.target) : trusted::CutSegmentId{};
    const bool has_source = view.has_segment(source);
    const bool has_target = view.has_segment(target);
    if ((has_source && has_target) || (has_source && target.empty())
        || (has_target && source.empty())) {
        result.push_back(view);
        if (step.randomize_source && step.randomize_target) {
            model_->branch(view, source, result, true);
            model_->branch(view, target, result, true);
            model_->branch(view, source, target, result, true);
        } else if (step.randomize_source || step.randomize_target) {
            model_->branch(view,
                step.randomize_source ? source : trusted::CutSegmentId{},
                step.randomize_target ? target : trusted::CutSegmentId{},
                result, true);
        }
    } else if (has_source && !has_target) {
        model_->branch(view,
            step.randomize_source ? source : trusted::CutSegmentId{}, target,
            result, step.check_orientation || step.randomize_source);
        if (result.empty()) view.notify_error(target);
    } else if (!has_source && has_target) {
        model_->branch(view, source,
            step.randomize_target ? target : trusted::CutSegmentId{}, result,
            step.check_orientation || step.randomize_target);
        if (result.empty()) view.notify_error(source);
    } else {
        const auto branch_result = model_->branch(
            view, source, target, result, step.check_orientation);
        if (result.empty()) view.notify_error(
            branch_result == trusted::BranchReturnType::fail_first
                ? source : target);
    }
}

} // namespace phoenix::partition::adapted
