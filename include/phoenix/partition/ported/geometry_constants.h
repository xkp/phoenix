#pragma once


#ifndef M_PI
#   define M_PI       3.14159265358979323846
#endif

#ifndef M_PI_2
#   define M_PI_2     1.57079632679489661923
#endif

#ifndef M_2_PI
#   define M_2_PI     2 * M_PI
#endif

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))


const double curve_distance_epsilon        = 1e-30;
const double curve_collinearity_epsilon    = 1e-30;
const double curve_angle_tolerance_epsilon = 0.01;


/*enum bezier_margin {
	BEZIER_MARGIN_BEGIN,
	BEZIER_MARGIN_END,
	BEZIER_MARGIN_ADJUST
};*/

enum bezier_adjust {
	BEZIER_ADJUST_BEGIN,
	BEZIER_ADJUST_END,
	BEZIER_ADJUST_BEGIN_END,
	BEZIER_ADJUST_SEGMENT_EXCESS,
	BEZIER_ADJUST_SEGMENT_DEFECT,
};

enum bezier_subdivision {
	BEZIER_BY_AUTO,
	BEZIER_BY_LENGTH,
	BEZIER_BY_COUNT,
};

