#pragma once
#include "partition_solver.h"

DEFAULT_CGAL_TYPES()
DEFAULT_ARRANGEMENT_TYPES()
DEFAULT_UTILS()

struct base_length_pct_filter
{
	vm::variable_value min_dist_pct;
	vm::variable_value max_dist_pct;
	double _min_dist_pct;
	double _max_dist_pct;
	bool built;

	base_length_pct_filter(vm::variable_value min_dist_pct_, vm::variable_value max_dist_pct_) :
		built(false),
		min_dist_pct(min_dist_pct_),
		max_dist_pct(max_dist_pct_)
	{
	}

	void build(vm::const_icontext_ref ctx)
	{
		// td: variable not found error?
		// square and percent
		_min_dist_pct = min_dist_pct.value(ctx) * 0.01;
		_max_dist_pct = max_dist_pct.value(ctx) * 0.01;
		assert(_min_dist_pct >= 0 && _max_dist_pct >= 0);

		_min_dist_pct *= _min_dist_pct;
		_max_dist_pct *= _max_dist_pct;
		built = true;
	}

	bool operator ()(vm::const_icontext_ref ctx, const repo_edge2& he1, const repo_edge2& he2)
	{
		//if (!built) do not cache
			build(ctx);

		double d = CGAL::to_double(he1.seg.squared_length());
		double ref = CGAL::to_double(he2.seg.squared_length());
		return d >= _min_dist_pct * ref && d <= _max_dist_pct * ref;
	}

	static void add_to_model(partition_model& model, cut_segment_id segment_id, cut_segment_id ref_id, vm::variable_value min_dist, vm::variable_value max_dist)
	{
	    partition_filter pf = (partition_filter)base_length_pct_filter(min_dist, max_dist);
		model.add_filter(segment_id, ref_id, pf);
	}
};

struct base_angle_filter
{
	vm::variable_value min_angle;
	vm::variable_value max_angle;
	double _min_angle;
	double _max_angle;
	bool built;

	base_angle_filter(vm::variable_value min_angle_, vm::variable_value max_angle_):
		built(false),
		min_angle(min_angle_),
		max_angle(max_angle_)
	{
	}

	void build(vm::const_icontext_ref ctx)
	{
		_min_angle = min_angle.value(ctx) - 1e-5;
		_max_angle = max_angle.value(ctx) + 1e-5;
		//td: check values existence
		built = true;
	}

	bool operator ()(vm::const_icontext_ref ctx, const repo_edge2& he1, const repo_edge2& he2)
	{
		//if (!built) do not cache
			build(ctx);

		double angle = geom_utils::angle_between(he1.seg, he2.seg);
		double angle_in_degrees = angle * 180 / M_PI;
		if (angle_in_degrees < 0)
			angle_in_degrees += 180;
		if (angle_in_degrees >= 180)
			angle_in_degrees -= 180;
		if (angle_in_degrees > 90)
			angle_in_degrees = 180 - angle_in_degrees;

		if (angle_in_degrees >= _min_angle && angle_in_degrees <= _max_angle)
			return true;
		return false;
	}

	static void add_to_model(partition_model& model, cut_segment_id segment1, cut_segment_id segment2, vm::variable_value min_angle, vm::variable_value max_angle)
	{
	    partition_filter pf = (partition_filter)base_angle_filter(min_angle, max_angle);
		model.add_filter(segment1, segment2, pf);
	}
};

