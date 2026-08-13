#include "stdafx.h"
#include "partition_errors.h"

solver_error partition_errors::create(PARTITION_ERRORS id)
{
	property_tree data;
	return partition_errors::create(id, data);
}

solver_error partition_errors::create(PARTITION_ERRORS id, property_tree& data)
{
	switch (id)
	{
		//td: error, params
		case PARTITION_ERROR_LENGTH_ORIGINAL_ORIGINAL_NOT_IMPLEMENTED:
			return solver_error((int)id, "length constraint among base segments is not yet supported");
		case PARTITION_ERROR_NOT_ENOUGH_SPACE_FOR_REPEAT:
			return solver_error((int)id, "not enough space was found to perform a repeat cut");
		case PARTITION_ERROR_LABELED_MISSING_SEGMENT:
			return solver_error((int)id, "a label condition is missing a segment");
		case PARTITION_ERROR_LABELED_MISSING_LABEL:
			return solver_error((int)id, "a label condition is missing a label");
		case PARTITION_ERROR_SEGMENT_NOT_FOUND:
			return solver_error((int)id, "missing segment");
		case PARTITION_ERROR_LABEL_NOT_FOUND:
			return solver_error((int)id, "missing label");
		case PARTITION_ERROR_NO_CUTS:
			return solver_error((int)id, "the cut is empty");
		case PARTITION_ERROR_MISSING_REPEAT_LENGTH:
			return solver_error((int)id, "repeat requires length");
		case PARTITION_ERROR_DISTANCE_UNNAMED:
			return solver_error((int)id, "a distance condition is missing its segment");
		case PARTITION_ERROR_DISTANCE_MISSING_REFERENCE:
			return solver_error((int)id, "a distance condition is missing its reference");
		case PARTITION_ERROR_DISTANCE_MISSING_PCT_REFERENCE:
			return solver_error((int)id, "a distance condition is missing its percentage reference");
		case PARTITION_ERROR_DISTANCE_TO_BASE_NOT_IMPLEMENTED:
			return solver_error((int)id, "a distance condition can not yet be used with only base segments");
		case PARTITION_ERROR_DISTANCE_EXPECTS_CUT:
			return solver_error((int)id, "the target of a distance condition must be a cut");
		case PARTITION_ERROR_DISTANCE_INVALID_CUT_ORDER:
			return solver_error((int)id, "the target of the cut operation happens after its reference");
		case PARTITION_ERROR_UNNAMED_FACT:
			return solver_error((int)id, "a condition needs a target");
		case PARTITION_ERROR_UNKNOWN_FACT:
			return solver_error((int)id, "unknown fact");
		case PARTITION_ERROR_MISSING_VALUE:
			return solver_error((int)id, "a condition is missing its values");
		case PARTITION_ERROR_MISSING_RANGE:
			return solver_error((int)id, "a condition must specify at least a minimum or maximum value");
		case PARTITION_ERROR_MISSING_REFERENCE:
			return solver_error((int)id, "a condition is missing its reference segment");
		case PARTITION_ERROR_LENGTH_PCT_IN_BASE:
			return solver_error((int)id, "length conditions can not be used in base segments");
		case PARTITION_ERROR:
			return solver_error((int)id, "partition error", data);
		case PARTITION_ERROR_MISSING_VARIABLE:
			return solver_error((int)id, "missing variable");
		case PARTITION_ERROR_TESSELLATOR:
			return solver_error((int)id, "tessellator error: ").is_bug(true);
		case PARTITION_ERROR_CURVE_OUT_OF_BOUNDS:
			return solver_error((int)id, "tessellator error: curve is outside the arrangement");
		case PARTITION_ERROR_CURVE_COLLISION:
			return solver_error((int)id, "tessellator error: curve collision");
			

		default:
		{
			assert(false);
			throw;
		}
	}

	return solver_error((int)id, data);
}

