#pragma once

#include "phoenix/partition/geometry.h"
#include "phoenix/partition/ported/null_diagnostics.hpp"
#include <CGAL/create_straight_skeleton_2.h>
#include <CGAL/Straight_skeleton_2.h>

struct inset_labels
{
	inset_labels() :
		result_face(-1),
		side_face(-1),
		result_edge(-1),
		left_edge(-1),
		right_edge(-1),
		top_edge(-1),
		bottom_edge(-1)
	{
	}

	int result_face;
	int side_face;
	int result_edge;
	int left_edge;
	int right_edge;
	int top_edge;
	int bottom_edge;
};

struct inset_request
{
	DEFAULT_CGAL_TYPES()
	DEFAULT_ARRANGEMENT_TYPES()

	inset_request(double amount_,
		arrangement2& arr_,  
		face2& f_, 
		face2_list* faces_ = nullptr, 
		face2_list* sides_ = nullptr) :
		amount(amount_),
		arr(arr_),
		f(f_),
		faces(faces_),
		sides(sides_)
	{
	}

	double amount;
	arrangement2& arr;
	face2& f;
	face2_list* faces;
	face2_list* sides;
	inset_labels labels;
};

struct inset
{
	DEFAULT_CGAL_TYPES()
	DEFAULT_ARRANGEMENT_TYPES()

	const static int SIDE_TAG = 873476;
	const static int RESULT_TAG = 1873476;

	static bool run(inset_request& request, debug_json* dj_, bool randomize = true);
};
