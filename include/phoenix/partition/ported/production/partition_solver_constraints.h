#pragma once
#include "partition_solver.h"

struct segment_length_constraint: partition_solver_constraint
{
	cut_segment_id    segment;
	vm::variable_value min_dist;
	vm::variable_value max_dist;

	segment_length_constraint(int id, cut_segment_id segment_, vm::variable_value min_dist_, vm::variable_value max_dist_);

	virtual void build(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan);
};

struct segment_length_constraint_pct : partition_solver_constraint
{
	cut_segment_id    segment;
	vm::variable_value min_dist;
	vm::variable_value max_dist;
	cut_segment_id seg_pct_ref;

	segment_length_constraint_pct(int id, cut_segment_id segment_, vm::variable_value min_dist_, vm::variable_value max_dist_, cut_segment_id seg_pct_ref_);

	virtual void build(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan);
};

struct segments_angle_constraint: partition_solver_constraint
{
	cut_segment_id    segment1;
	cut_segment_id    segment2;
	vm::variable_value min_angle;
	vm::variable_value max_angle;

	segments_angle_constraint(int id, cut_segment_id segment1_, cut_segment_id segment2_, vm::variable_value min_angle_, vm::variable_value max_angle_);

	virtual void build(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan);
};

struct segment_distance_constraint_base : partition_solver_constraint
{
	cut_segment_id    segment1;
	cut_segment_id    segment2;
	bool   has_min_distance;
	bool   has_max_distance;

	segment_distance_constraint_base(int id, int cut_id_, cut_segment_id segment1_, cut_segment_id segment2_, bool has_min_distance_, bool has_max_distance_):
		partition_solver_constraint(id, cut_id_),
		segment1(segment1_),
		segment2(segment2_),
		has_min_distance(has_min_distance_),
		has_max_distance(has_max_distance_)
		{}

	virtual void build(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan);

	virtual void add_cut_instruction(vm::const_icontext_ref ctx, partition_plan& plan) = 0;

	virtual void add_parent_distance_extra(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan) {};
};

struct segment_distance_constraint : segment_distance_constraint_base
{
	vm::variable_value min_distance;
	vm::variable_value max_distance;

	segment_distance_constraint(int id, int cut1_, cut_segment_id segment1_, cut_segment_id segment2_, bool has_min_distance_, vm::variable_value min_distance_, bool has_max_distance_, vm::variable_value max_distance_);

	virtual void add_cut_instruction(vm::const_icontext_ref ctx, partition_plan& plan);

	virtual void add_parent_distance_extra(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan);
};

struct segment_distance_constraint_pct : segment_distance_constraint_base
{
	vm::variable_value min_distance_pct;
	vm::variable_value max_distance_pct;
	cut_segment_id    seg_pct_ref;

	segment_distance_constraint_pct(int id, int cut1_, cut_segment_id segment1_, cut_segment_id segment2_, bool has_min_distance_, vm::variable_value min_distance_pct_, bool has_max_distance_, vm::variable_value max_distance_pct_, cut_segment_id seg_pct_ref_);

	virtual void build(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan);

	virtual void add_cut_instruction(vm::const_icontext_ref ctx, partition_plan& plan);

};

