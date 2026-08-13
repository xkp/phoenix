#include "phoenix/partition/ported/bezier_utils.h"

#include "phoenix/partition/ported/solver_errors.h"

bezier_options var_bezier_options::to_bezier_options(vm::const_icontext_ref ctx) const
{
    bezier_options options;
    double step_length_value = -1;
    if (step_length) {
        step_length_value = step_length.value(ctx);
    }

    if (step_length_value > 0)
        options = bezier_options::by_length(step_length_value, adjust);
    else
        options = bezier_options::by_lod(ctx ? ctx->lod() : 1);

    return options;
}

solver_error solver_errors::create(SOLVER_ERRORS id)
{
    switch (id) {
    case SERR_INVALID_INSET_AMOUNT:
        return solver_error(static_cast<int>(id), "insets requires a positive amount");
    case SERR_TOO_MANY_ITERATIONS:
        return solver_error(static_cast<int>(id), "too many iterations");
    case SERR_INPUT_DOESNT_MATCH:
        return solver_error(static_cast<int>(id), "the input does not match the facts");
    default:
        return solver_error(static_cast<int>(id), "unknown solver error");
    }
}
