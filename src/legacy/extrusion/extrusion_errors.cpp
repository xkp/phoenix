#include "stdafx.h"
#include "extrusion_errors.h"

solver_error extrusion_errors::create(EXTRUSION_ERRORS id)
{
	property_tree data;
	switch (id)
	{
		//td: error, params
		case EXTRUSION_ERROR_NO_PROFILE:
			return solver_error((int)id, "no profile could be found to extrude");
		case EXTRUSION_ERROR_COLINEAR:
			return solver_error((int)id, "profile segments can not be colinear (for now)");
		case EXTRUSION_ERROR_MIXED_SIGNS:
			return solver_error((int)id, "a face with profiles of different sign can not be extruded");
		case EXTRUSION_ERROR_SHARED:
			return solver_error((int)id, "can't extrude inside a share");
		case EXTRUSION_ERROR_TIMEOUT:
			return solver_error((int)id, "the extrude took longer than expected");
		case EXTRUSION_ERROR_PLAN_INSERT_EDGE1:
			return solver_error((int)id, "can't insert edge on two different faces in plan_builder");
		case EXTRUSION_ERROR_PLAN_INSERT_EDGE_UNKNOWN:
			return solver_error((int)id, "can't insert edge in plan_builder");			
		case EXTRUSION_ERROR_EDGE_CASE:
			return solver_error((int)id, "an unhandled edge case has been found.");
		default:
			assert(false);
			throw;
	}

	return solver_error((int)id, data);
}
