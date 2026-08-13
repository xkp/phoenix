#pragma once

#include "error.h"

enum SOLVER_ERRORS
{
	SERR_INVALID_INSET_AMOUNT = 2000,
	SERR_TOO_MANY_ITERATIONS = 2001,
	SERR_INPUT_DOESNT_MATCH = 2002,
};

class solver_errors
{
public:
	static solver_error create(SOLVER_ERRORS id);
};
