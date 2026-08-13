#include "stdafx.h"
#include "partition_solver_constraints.h"
#include "partition_solver_filters.h"
#include "partition_errors.h"
#include "phoenix/partition/ported/vm_compat.hpp"

DEFAULT_CGAL_TYPES()
DEFAULT_ARRANGEMENT_TYPES()
DEFAULT_UTILS()

//segment_length_constraint
segment_length_constraint::segment_length_constraint(int id, cut_segment_id segment_, vm::variable_value min_dist_, vm::variable_value max_dist_) :
	partition_solver_constraint(id),
	segment(segment_),
	min_dist(min_dist_),
	max_dist(max_dist_)
{
}

segment_length_constraint_pct::segment_length_constraint_pct(int id, cut_segment_id segment_, vm::variable_value min_dist_, vm::variable_value max_dist_, cut_segment_id seg_pct_ref_) :
	partition_solver_constraint(id),
	segment(segment_),
	min_dist(min_dist_),
	max_dist(max_dist_),
	seg_pct_ref(seg_pct_ref_)
{
}

struct restrict_cut_segment_length_base
{
	int cutid;
	bool is_source;
	bool is_left;

	restrict_cut_segment_length_base(int cutid_, bool is_source_, bool is_left_) :
		cutid(cutid_),
		is_source(is_source_),
		is_left(is_left_)
	{
	}

	bool cut_segment(const partition_model& model, segment_info* seg_src, segment_info* seg_dst, double min_length, double max_length, dj& dj_)
	{
		dj_.draw_all();
		dj_.add("cutid", cutid);
		dj_.add("is_source", is_source);
		dj_.add("is_left", is_left);
		dj_.add("min_length", min_length);
		dj_.add("max_length", max_length);

		segment_info* seg = is_source ? seg_src : seg_dst;
		dj_.add("segment", *seg, &model);

		// since segment_info orientation is not trustable, use the mid point of src and dst as the testing line
		// to correct the orientation
		point2 src_mid((seg_src->src.x() + seg_src->tgt.x()) / 2, (seg_src->src.y() + seg_src->tgt.y()) / 2);
		point2 dst_mid((seg_dst->src.x() + seg_dst->tgt.x()) / 2, (seg_dst->src.y() + seg_dst->tgt.y()) / 2);

		line2 test_line(src_mid, dst_mid);
		bool src_is_right = test_line.has_on_negative_side(seg->src);

		bool res = cut_segment(model, seg, min_length, max_length, !(is_left ^ src_is_right), dj_);
		return res;
	}

	bool cut_segment(const partition_model& model, segment_info* seg, double min_length, double max_length, bool reversed, dj& dj_)
	{
		point2 src = seg->src;
		point2 tgt = seg->tgt;

		vec2 dir(seg->orig_src, seg->orig_tgt);
		double len = CGAL::sqrt(CGAL::to_double(dir.squared_length()));
		dj_.add("len", len);
		geom_utils::normalize(dir);

		/*if (reversed)
		{
			auto max_len2 = max_length;
			max_length = len - min_length;
			min_length = len - max_len2;
		}*/

		if (min_length > len || max_length < 0)
		{
			dj_.error("segment too short");
			return false;
		}

		//apply max
		if (reversed)
		{
			if (min_length > 0 && min_length < len)
				tgt = seg->orig_tgt - dir*min_length;
		}
		else if (max_length < len)
			tgt = seg->orig_src + dir*max_length;

		//apply min
		if (reversed)
		{
			if (max_length < len)
				src = seg->orig_tgt - dir*max_length;
		}
		else if (min_length > 0)
			src = seg->orig_src + dir*min_length;

		//apply max
		/*if (max_length < len)
			tgt = seg->orig_src + dir*max_length;

		//apply min
		if (min_length > 0)
			src = seg->orig_src + dir*min_length;*/

		// obey previuos limits
		line2 old_src(seg->src, dir.perpendicular(CGAL::CLOCKWISE));
		if (!old_src.has_on_positive_side(src))
			src = seg->src;

		line2 old_tgt(seg->tgt, dir.perpendicular(CGAL::COUNTERCLOCKWISE));
		if (!old_tgt.has_on_positive_side(tgt))
			tgt = seg->tgt;

		auto src_dist = CGAL::squared_distance(seg->orig_src, src);
		auto tgt_dist = CGAL::squared_distance(seg->orig_src, tgt);
		if (src_dist > tgt_dist)
		{
			dj_.add("src", src);
			dj_.add("tgt", tgt);
			dj_.add("src_dist", CGAL::to_double(src_dist));
			dj_.add("tgt_dist", CGAL::to_double(tgt_dist));
			dj_.error("result segment emtpy");
			return false;
		}

		seg->src = src;
		seg->tgt = tgt;

		dj_.add("solution", *seg, &model);
		return true;
	}

};

struct restrict_cut_segment_length: restrict_cut_segment_length_base
{
	double min_dist;
	double max_dist;

	restrict_cut_segment_length(int cutid_, bool is_source_, bool is_left_, double min_dist_, double max_dist_) :
		restrict_cut_segment_length_base(cutid_, is_source_, is_left_),
		min_dist(min_dist_),
		max_dist(max_dist_)
	{
	}

	bool operator ()(partition_view& view, const partition_model& model)
	{
		auto dj_ = model.inst_dj().add_type("restrict_cut_segment_length");
		auto cut = model.get_cut(cutid); assert(cut != nullptr);
		segment_info* seg_src = view.segment(cut->src);
		segment_info* seg_dst = view.segment(cut->dst);

		return cut_segment(model, seg_src, seg_dst, min_dist, max_dist, dj_);
	}

	static void add_to_plan(partition_plan& plan, int cutid, bool is_source, bool is_left, double min_dist, double max_dist, const error_map& errors)
	{
		restrict_cut_segment_length cl(cutid, is_source, is_left, min_dist, max_dist);
		partition_plan_worker ppw(cl);
		plan.add_instruction(cutid, ppw, PRIORITY_LENGTH_CONSTRAINT, errors);
	}
};


struct restrict_cut_segment_length_pct: restrict_cut_segment_length_base
{
	double min_dist_pct;
	double max_dist_pct;
	cut_segment_id seg_pct_ref;

	restrict_cut_segment_length_pct(int cutid_, bool is_source_, bool is_left_, double min_dist_, double max_dist_, cut_segment_id seg_pct_ref_) :
		restrict_cut_segment_length_base(cutid_, is_source_, is_left_),
		min_dist_pct(min_dist_),
		max_dist_pct(max_dist_),
		seg_pct_ref(seg_pct_ref_)
	{
	}

	bool operator ()(partition_view& view, const partition_model& model)
	{
		auto dj_ = model.inst_dj().add_type("restrict_cut_segment_length_pct");
		auto cut = model.get_cut(cutid); assert(cut != nullptr);

		segment_info* seg_pct;
		if (!seg_pct_ref.empty())
		{
			seg_pct = view.segment(seg_pct_ref);
			if (!seg_pct)
			{
				//std::cout << "length_pct, branching simple for: " << model.segment_name(seg_pct_ref ) << std::endl;
				seg_pct = model.branch_simple(view, seg_pct_ref, false);
				if (!seg_pct)
					return true; // ignore length restriction if reference is not available
			}
		}

		segment_info* seg_src = view.segment(cut->src);
		segment_info* seg_dst = view.segment(cut->dst);
		if (seg_pct_ref.empty()) // self
			seg_pct = is_source ? seg_src : seg_dst;
		dj_.add("seg_pct_ref", seg_pct, &model);

		double len_ref = CGAL::sqrt(CGAL::to_double(
			CGAL::squared_distance(seg_pct->orig_src, seg_pct->orig_tgt)));
		//dj::add("len_ref", len_ref);

		dj_.add("min_length_pct", this->min_dist_pct);
		dj_.add("max_length_pct", this->max_dist_pct);

		double min_dist = this->min_dist_pct*0.01*len_ref;
		double max_dist = this->max_dist_pct*0.01*len_ref;
		return cut_segment(model, seg_src, seg_dst, min_dist, max_dist, dj_);
	}

	static void add_to_plan(partition_plan& plan, int cutid, bool is_source, bool is_left, double min_dist, double max_dist, cut_segment_id seg_pct_ref, const error_map& errors)
	{
		restrict_cut_segment_length_pct cl(cutid, is_source, is_left, min_dist, max_dist, seg_pct_ref);
		partition_plan_worker ppw(cl);
		plan.add_instruction(cutid, ppw, PRIORITY_LENGTH_CONSTRAINT, errors);
	}
};

struct restrict_cut_angle
{
	cut_segment_id ref_id;
	double min_angle;
	double max_angle;

	restrict_cut_angle(cut_segment_id _ref_id, double min_angle_, double max_angle_) :
		ref_id(_ref_id),
		min_angle(min_angle_),
		max_angle(max_angle_)
	{
	}

	void operator ()(partition_view& view, const partition_model& model, partition_view_list& result)
	{
		auto dj_ = model.inst_dj();
		dj_.add_type("restrict_cut_angle");
		dj_.add("ref_id", model.segment_name(ref_id));
		dj_.add("min_angle", min_angle);
		dj_.add("max_angle", max_angle);

		partition_view_list views;
		segment_info* s = view.segment(ref_id);
		if (s == nullptr)
		{
			if (model.is_base_segment(ref_id))
				model.branch(view, ref_id, views, false);
		}
		else
		{
			views.push_back(view);
		}

		if (views.size() == 0)
		{
			dj_.error("no segment found for reference (ref_id)");
			return;
		}

		// -- Convert degrees to radians
		double min_in_rads = min_angle * M_PI / 180;
		double max_in_rads = max_angle * M_PI / 180;

		auto dj_solutions = dj_.begin_array("solutions");
		for (auto& it : views)
		{
			auto dj_sol = dj_solutions.begin();
			segment_info* ref = it.segment(ref_id);
			dj_sol.add("ref_segment", ref, &model);

			vec2 v(ref->orig_src, ref->orig_tgt);

			//calculate the global angle of the reference
			double ref_ang = geom_utils::angle_between(vec2(1, 0), v);
			// as opposites angles are considered equivalent, use >= 0 angles whenever possible
			if (ref_ang < 0)
				ref_ang += M_PI;
			dj_sol.add("ref_angle", ref_ang * 180 / M_PI);
			it.restrict_angles(ref_ang, min_in_rads, max_in_rads);
			if (it.angles.size() > 0)
			{
				dj_sol.add("angles", it.angles);
				result.push_back(it);
			}
			else
				dj_sol.error("invalid angles");
		}
	}
};

void segment_length_constraint::build(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan)
{
	int cut_idx = -1;
	cut_segment_type segment_type = BASE_SEGMENT;
	cut_segment_id parent_segment;
	partition_cut* cut_ = nullptr;
	if (model.is_cut_segment(segment, cut_idx, segment_type))
	{
		_cut_idx = cut_idx;
		if (segment_type == CUT_SEGMENT)
			assert(false); //cut, this one is tricky
		else
		{	// cut result
			auto min_dist_ = min_dist.value(ctx);
			auto max_dist_ = max_dist.value(ctx);
			restrict_cut_segment_length::add_to_plan(plan, cut_idx, segment_type.is_source(), segment_type.is_left(), min_dist_, max_dist_, errors());

			// extra instruction to reserve at least 'min_dist' space on the parent
			parent_segment = model.get_parent_segment(segment);
			if (min_dist_ > 0 && parent_segment.valid() && model.is_cut_segment(parent_segment, cut_idx, segment_type))
			{
				if (segment_type.is_result()) // parent is a cut result
				{
					//std::cout << "restrict length on parent: " << model.segment_name(parent_segment) << std::endl;
					max_dist_ = std::numeric_limits<double>::max();
					restrict_cut_segment_length::add_to_plan(plan, cut_idx, segment_type.is_source(), segment_type.is_left(), min_dist_, max_dist_, errors());
				}
			}
		}
	}
};

void segment_length_constraint_pct::build(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan)
{
	_cut_idx = -1;
	cut_segment_type segment_type = BASE_SEGMENT;
	partition_cut* cut_ = nullptr;

	if (!model.is_cut_segment(segment, _cut_idx, segment_type))
	{
		if (model.is_base_segment(segment))
			assert(false);
		return;
	}

	if (seg_pct_ref.valid() && model.is_base_segment(seg_pct_ref))
	{
		select_edges se(seg_pct_ref);
		partition_plan_brancher ppb(se);
		auto iidx = plan.add_instruction(_cut_idx, ppb, PRIORITY_SELECT_EDGES_SECONDARY);
		property_tree error_data;
		if (model.select_edge_error(seg_pct_ref, error_data))
			plan.register_error(iidx, seg_pct_ref.value, error_data);
	}

	if (segment_type == CUT_SEGMENT)
	{
		assert(false); //cut, this one is tricky
		return;
	}

	auto min_dist_ = min_dist.value(ctx);
	auto max_dist_ = max_dist.value(ctx);
	restrict_cut_segment_length_pct::add_to_plan(plan, _cut_idx, segment_type.is_source(), segment_type.is_left(), min_dist_, max_dist_, seg_pct_ref, errors());

	// extra instruction to reserve at least 'min_dist' space on the parent
	if (seg_pct_ref.empty()) // not needed in self-references
		return;

	cut_segment_id parent_segment = model.get_parent_segment(segment);
	int parent_cut_idx;
	cut_segment_type parent_segment_type;
	if (min_dist_ > 0 && parent_segment.valid()
		&& model.is_cut_segment(parent_segment, parent_cut_idx, parent_segment_type)
		&& parent_segment_type.is_result()) // parent is a cut result
	{
		//std::cout << "restrict length pct on parent: " << model.segment_name(parent_segment) << std::endl;
		max_dist_ = std::numeric_limits<double>::max();
		restrict_cut_segment_length_pct::add_to_plan(plan, parent_cut_idx, parent_segment_type.is_source(), parent_segment_type.is_left(), min_dist_, max_dist_, seg_pct_ref, errors());
	}
};

//segments_angle_constraint
segments_angle_constraint::segments_angle_constraint(int id, cut_segment_id segment1_, cut_segment_id segment2_, vm::variable_value min_angle_, vm::variable_value max_angle_) :
	partition_solver_constraint(id),
	segment1(segment1_),
	segment2(segment2_),
	min_angle(min_angle_),
	max_angle(max_angle_)
{
}

void segments_angle_constraint::build(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan)
{
	bool cut1 = model.is_cut(segment1);
	bool original1 = model.is_base_segment(segment1);
	cut_segment_id segment1_;
	if (!original1 && !cut1)
	{
		//its a segment caused by a cut
		segment1_ = model.get_root_segment(segment1);
		cut1 = model.is_cut(segment1_);
		original1 = model.is_base_segment(segment1_);
		segment1 = segment1_;
	}

	bool original2 = model.is_base_segment(segment2);
	bool cut2 = model.is_cut(segment2);
	cut_segment_id segment2_;
	if (!original2 && !cut2)
	{
		//its a segment caused by a cut
		segment2_ = model.get_root_segment(segment2);
		cut2 = model.is_cut(segment2_);
		original2 = model.is_base_segment(segment2_);
		segment2 = segment2_;
	}

	//combinations
	cut_segment_id restrict_ref;
	cut_segment_id restrict_cut;
	if (original1 && original2)
	{
		//two original segments, just make sure the combinations are valid
		//segments_orig_angle_filter::add_to_model(model, segment1, segment2, min_angle, max_angle);
	}
	else if (cut1 && cut2)
	{
		//both cuts, add the instruction to the latest cut
		restrict_ref = segment1 > segment2 ? segment2 : segment1;
		restrict_cut = segment1 > segment2 ? segment1 : segment2;
	}
	else if (original1 && cut2)
	{
		//this will restrict the rom of the cut
		restrict_ref = segment1;
		restrict_cut = segment2;
	}
	else if (original2 && cut1)
	{
		//this will restrict the rom of the cut
		restrict_ref = segment2;
		restrict_cut = segment1;
	}

	if (restrict_ref.valid() && restrict_cut.valid())
	{
		cut_segment_type segment_type;
		if (model.is_cut_segment(restrict_cut, _cut_idx, segment_type))
		{
			select_edges se(restrict_ref);
			partition_plan_brancher ppb_edges(se);
			auto iidx = plan.add_instruction(_cut_idx, ppb_edges, PRIORITY_SELECT_EDGES_SECONDARY);

			property_tree error_data;
			if (model.select_edge_error(restrict_ref, error_data))
				plan.register_error(iidx, restrict_ref.value, error_data);

			restrict_cut_angle rca(restrict_ref, min_angle.value(ctx), max_angle.value(ctx));
			partition_plan_brancher ppb_angle(rca);
			plan.add_instruction(_cut_idx, ppb_angle, PRIORITY_ANGLE_CONSTRAINT, errors());
		}
	}
}

struct restrict_cut_distance_base
{
	int    _cutId;
	cut_segment_id    cutSegment;
	cut_segment_id    refId;
	bool   has_min_distance;
	bool   has_max_distance;

	restrict_cut_distance_base(int cutId_, cut_segment_id cutSegment_, cut_segment_id refId_, bool has_min_distance_, bool has_max_distance_) :
		_cutId(cutId_),
		cutSegment(cutSegment_),
		refId(refId_),
		has_min_distance(has_min_distance_),
		has_max_distance(has_max_distance_)
	{
	}

	bool do_restrict(partition_view& view, const partition_model& model, double min_distance, double max_distance)
	{
		auto dj_ = model.inst_dj();
		dj_.draw_all();
		dj_.add("cutSegment", model.segment_name(cutSegment));
		auto ref = view.segment(refId);
		dj_.add("ref_segment", ref, &model);
		if (has_min_distance)
			dj_.add("min_distance", min_distance);
		if (has_max_distance)
			dj_.add("max_distance", max_distance);

		vec2 dir(ref->src, ref->tgt);
		//auto angle = geom_utils::angle(dir);
		//dir = geom_utils::rotate_unit(angle + M_PI_2);
		dir = dir.perpendicular(CGAL::COUNTERCLOCKWISE);
		geom_utils::normalize(dir);

		auto cut = model.get_cut_by_segment(cutSegment); assert(cut);
		auto cut_src = view.segment(cut->src); assert(cut_src);
		auto cut_tgt = view.segment(cut->dst); assert(cut_tgt);
		dj_.add("cut_src", cut_src, &model);
		dj_.add("cut_tgt", cut_tgt, &model);
		//cut_src->print("cut_src");
		//cut_tgt->print("cut_tgt");

		segment2 ref_seg(ref->src, ref->tgt);
		if (model.is_cut(refId))
		{
			//decide on inter-cut direction
			line2 refLine = ref_seg.supporting_line();
			if (!refLine.has_on_positive_side(cut_src->src) && !refLine.has_on_positive_side(cut_src->tgt))
			{
				dir = -dir;
				ref_seg = ref_seg.opposite();
			}
		}

		line2 min_line, max_line;
		if (has_min_distance)
		{
			auto p1 = ref_seg.source() + dir*min_distance;
			auto p2 = ref_seg.target() + dir*min_distance;
			min_line = line2(p1, p2);
			dj_.add("min_line", segment2(p1, p2));
		}

		if (has_max_distance)
		{
			auto p1 = ref_seg.source() + dir*max_distance;
			auto p2 = ref_seg.target() + dir*max_distance;
			max_line = line2(p2, p1);
			dj_.add("max_line", segment2(p2, p1));
		}

		if (has_min_distance && !min_line.is_degenerate() && !(restrict_line(min_line, cut_src) && restrict_line(min_line, cut_tgt)))
		{
			view.notify_error();
			return false;
		}

		if (has_max_distance && !max_line.is_degenerate() && !(restrict_line(max_line, cut_src) && restrict_line(max_line, cut_tgt)))
		{
			view.notify_error();
			return false;
		}

		dj_.begin("solution")
			.add("cut_src", cut_src, &model)
			.add("cut_tgt", cut_tgt, &model);

		return true;
	}

	bool restrict_line(line2& l, segment_info* segment)
	{
		//segments must remain on the positive side of the line
		segment2 s(segment->src, segment->tgt);
		bool src_negative = l.has_on_negative_side(segment->src);
		bool tgt_negative = l.has_on_negative_side(segment->tgt);

		CGAL::Object iobj = CGAL::intersection(s, l);
		const point2* ipoint = CGAL::object_cast<point2>(&iobj);
		if (!ipoint)
			return !src_negative;

		if (src_negative)
			segment->src = *ipoint;

		if (tgt_negative)
			segment->tgt = *ipoint;

		return true;
	}
};

struct restrict_cut_distance: restrict_cut_distance_base
{
	double min_distance;
	double max_distance;

	restrict_cut_distance(int cutId_, cut_segment_id cutSegment_, cut_segment_id refId_, bool has_min_distance_, double min_distance_, bool has_max_distance_, double max_distance_) :
		restrict_cut_distance_base(cutId_, cutSegment_, refId_, has_min_distance_, has_max_distance_),
		min_distance(min_distance_),
		max_distance(max_distance_)
	{
	}

	bool operator ()(partition_view& view, const partition_model& model)
	{
		model.inst_dj().add_type("restrict_cut_distance");

		return do_restrict(view, model, min_distance, max_distance);
	}
};

struct restrict_cut_distance_pct: restrict_cut_distance_base
{
	double min_dist_pct;
	double max_dist_pct;
	cut_segment_id    seg_pct_ref;

	restrict_cut_distance_pct(int cutId_, cut_segment_id cutSegment_, cut_segment_id refId_, bool has_min_distance_, double min_distance_, bool has_max_distance_, double max_distance_, cut_segment_id seg_pct_ref_) :
		restrict_cut_distance_base(cutId_, cutSegment_, refId_, has_min_distance_, has_max_distance_),
		min_dist_pct(min_distance_),
		max_dist_pct(max_distance_),
		seg_pct_ref(seg_pct_ref_)
	{
	}

	bool operator ()(partition_view& view, const partition_model& model)
	{
		auto dj_ = model.inst_dj();
		dj_.add_type("restrict_cut_distance_pct");
		if (has_min_distance)
			dj_.add("min_dist_pct", min_dist_pct);
		if (has_max_distance)
			dj_.add("max_dist_pct", max_dist_pct);

		assert(seg_pct_ref.valid());
		if (!seg_pct_ref.valid())
			return false;

		segment_info* seg_pct = view.segment(seg_pct_ref);
		assert(seg_pct);
		if (!seg_pct)
			return false;
		dj_.add("seg_pct_ref", seg_pct, &model);

		double len_pct_ref = CGAL::sqrt(CGAL::to_double(
			CGAL::squared_distance(seg_pct->orig_src, seg_pct->orig_tgt)));
		//dj::add("len_ref", len_ref);

		double min_distance = min_dist_pct*0.01*len_pct_ref;
		double max_distance = max_dist_pct*0.01*len_pct_ref;

		return do_restrict(view, model, min_distance, max_distance);
	}
};

struct restrict_distance_extra
{
	int    cut_id;
	int	   child_cut_id;
	bool   is_left;
	double min_distance;

	restrict_distance_extra(int cut_id_, int child_cut_id_, bool is_left_, double min_distance_):
		cut_id(cut_id_),
		child_cut_id(child_cut_id_),
		is_left(is_left_),
		min_distance(min_distance_)
	{}

	bool operator ()(partition_view& view, const partition_model& model)
	{
		auto dj_ = model.inst_dj();
		dj_.add_type("restrict_distance_extra");
		dj_.add("cut_id", model.cut_name(cut_id));
		dj_.add("child_cut_id", model.cut_name(child_cut_id));

		dj_.add("min_distance", min_distance);
		dj_.add("is_left", is_left);

		bool angleCollapsed = view.angles.size() == 1 && view.angles[0].min_angle == view.angles[0].max_angle;
		if (!angleCollapsed)
		{
			dj_.text("angles not collapsed");
			return true;
		}

		dj_.add("angle", view.angles[0].min_angle * 180 / M_PI);

		auto cut = model.get_cut(cut_id);
		segment_info* src = view.segment(cut->src);
		segment_info* dst = view.segment(cut->dst);
		dj_.add("src", src, &model);
		dj_.add("dst", dst, &model);

		// child_src and child_dst does not work, as these segments may not exist yet and need to be branched
		auto child_cut = model.get_cut(child_cut_id);
		dj_.add("child_src", model.segment_name(child_cut->src));
		dj_.add("child_dst", model.segment_name(child_cut->dst));
		segment_info* child_src = view.segment(child_cut->src);
		if (!child_src) // look in the parent, since cut_results may not exists yet
			child_src = view.segment(model.get_parent_segment(child_cut->src));
		if (!child_src)
		{
			child_src = model.branch_simple(view, child_cut->src, true);
			if (child_src)
				std::cout << "branch_simple worked for: " << model.segment_name(child_cut->src) << std::endl;
		}

		segment_info* child_dst = view.segment(child_cut->dst);
		if (!child_dst) // look in the parent, since cut_results may not exists yet
			child_dst = view.segment(model.get_parent_segment(child_cut->dst));
		if (!child_dst)
		{
			child_dst = model.branch_simple(view, child_cut->dst, true);
			if (child_dst)
				std::cout << "branch_simple worked for: " << model.segment_name(child_cut->dst) << std::endl;
		}

		dj_.add("child_src2", child_src, &model);
		dj_.add("child_dst2", child_dst, &model);
		if (!child_src || !child_dst)
			return true; // can't do anything in these cases

		auto cut_dir = geom_utils::rotate_unit(view.angles[0].min_angle);
		// ensures cut_dir goes from src to dst (invert if necessary)
		line2 src_line(src->orig_src, src->orig_tgt);
		if (src_line.oriented_side(dst->orig_src) != src_line.oriented_side(src->orig_src + cut_dir))
			cut_dir = -cut_dir;
		dj_.add("cut_dir", segment2(point2(0, 0), point2(0, 0) + 10 * cut_dir));

		// vector from the cut to the child cut
		auto dir_to_child = cut_dir.perpendicular(is_left ? CGAL::COUNTERCLOCKWISE : CGAL::CLOCKWISE);
		dj_.add("dir_to_child", segment2(point2(0, 0), point2(0, 0) + 10 * dir_to_child));

		/*
		// old way
		line2 line_central(CGAL::midpoint(src->src, src->tgt), CGAL::midpoint(dst->src, dst->tgt));
		bool src_to_left = !is_left ^ line_central.has_on_positive_side(src->orig_tgt); // src goes right to left
		bool dst_to_left = !is_left ^ line_central.has_on_positive_side(dst->orig_tgt); // dst goes right to left

		line2 line_opp(src_to_left ? src->orig_src : src->orig_tgt, is_left ? dir : -dir);
		point2 pt_left1 = src_to_left ? src->orig_tgt : src->orig_src;
		point2 pt_left2 = dst_to_left ? dst->orig_tgt : dst->orig_src;
		auto eval1 = geom_utils::line_eval(line_opp, pt_left1);
		auto eval2 = geom_utils::line_eval(line_opp, pt_left2);
		point2 pt_ref = eval1 < eval2 ? pt_left1 : pt_left2;

		// using src->orig_src, but any point is valid
		line2 test_line(src->orig_src, is_left ? dir : -dir);
		*/

		// find out which point of src and dst (on the farther extreme) is closer to the cut,
		// that point will be the reference point
		point2 points[4] = { child_src->orig_src, child_src->orig_tgt, child_dst->orig_src, child_dst->orig_tgt };
		//point2 points[4] = { src->orig_src, src->orig_tgt, dst->orig_src, dst->orig_tgt };
		double eval[4];
		for(int i=0; i<4; i++)
			eval[i] = CGAL::to_double(dir_to_child * vec2(CGAL::ORIGIN, points[i]));

		// get the farther point inside each src and dst
		int src_idx = eval[0] > eval[1] ? 0 : 1;
		int dst_idx = eval[2] > eval[3] ? 2 : 3;
		// reference point, distance will be measured from this point
		point2 pt_ref = points[eval[src_idx] < eval[dst_idx] ? src_idx : dst_idx];
		dj_.add("pt_ref", pt_ref);

		// calculate the cut line
		point2 pt_cut = pt_ref - dir_to_child * min_distance;
		dj_.add("seg_cut", segment2(pt_cut, pt_cut + (is_left ? -cut_dir : cut_dir) ));
		line2 cut_line(pt_cut, is_left ? -cut_dir : cut_dir);

		if (src->restrict_line(cut_line) && dst->restrict_line(cut_line))
		{
			dj_.begin("solution")
				.add("src", src, &model)
				.add("dst", dst, &model);
			return true;
		}
		else
			return false;
	}

};

void segment_distance_constraint_base::build(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan)
{
	bool iscut1 = model.is_cut(segment1);
	bool isoriginal1 = model.is_base_segment(segment1);
	bool iscut2 = model.is_cut(segment2);
	bool isoriginal2 = model.is_base_segment(segment2);

	if (iscut1 && isoriginal2)
	{
		//distance a cut must be from an original segment
		select_edges se(segment2);
		partition_plan_brancher ppb(se);
		auto iidx = plan.add_instruction(_cut_idx, ppb, PRIORITY_SELECT_EDGES_SECONDARY);

		property_tree error_data;
		if (model.select_edge_error(segment2, error_data))
			plan.register_error(iidx, segment2.value, error_data);

		add_cut_instruction(ctx, plan);
	}
	else if (iscut1 && iscut2)
	{
		cut_segment_type segment_type;
		if (segment1 < segment2)
		{
			std::swap(segment1, segment2);
			model.is_cut_segment(segment1, _cut_idx, segment_type);
		}

		add_cut_instruction(ctx, plan);

		add_parent_distance_extra(ctx, model, plan);
	}
	else if (isoriginal1 && isoriginal2)
	{
		throw partition_errors::create(PARTITION_ERROR_LENGTH_ORIGINAL_ORIGINAL_NOT_IMPLEMENTED);
		assert(false); //td: add filters
	}
	else
	{
		//this should never happen, all cases must have been covered
		assert(false);
	}
}

//segment_distance_constraint
segment_distance_constraint::segment_distance_constraint(int id, int cut_id_, cut_segment_id segment1_, cut_segment_id segment2_, bool has_min_distance_, vm::variable_value min_distance_, bool has_max_distance_, vm::variable_value max_distance_) :
	segment_distance_constraint_base(id, cut_id_, segment1_, segment2_, has_min_distance_, has_max_distance_),
	min_distance(min_distance_),
	max_distance(max_distance_)
{
}

void segment_distance_constraint::add_cut_instruction(vm::const_icontext_ref ctx, partition_plan& plan)
{
	restrict_cut_distance worker(_cut_idx, segment1, segment2, has_min_distance, min_distance.value(ctx), has_max_distance, max_distance.value(ctx));
	partition_plan_worker ppw(worker);
	plan.add_instruction(_cut_idx, ppw, PRIORITY_DISTANCE_CONSTRAINT, errors());
}

void segment_distance_constraint::add_parent_distance_extra(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan)
{
	//return;
	if (has_min_distance)
	{
		int parent_cut_idx;
		cut_segment_type segment_type;
		model.is_cut_segment(segment2, parent_cut_idx, segment_type);
		auto cut = model.get_cut(_cut_idx);
		if (!cut->randomize_source && !cut->randomize_target && cut->parent && cut->parent->id == parent_cut_idx)
		{
			//if (model.get_parent_segment(cut->src) != cut->parent->src ||
			//	model.get_parent_segment(cut->dst) != cut->parent->dst)
			//	return; // parent and child should share the same src and dst

			// the parent cut is segment2, reserve the minimun distance on the parent
			//std::cout << "inmediate distance between: " << model.segment_name(segment1) << " " << model.segment_name(segment2) << std::endl;
			bool is_left = cut->parent->left == cut;
			restrict_distance_extra worker(parent_cut_idx, _cut_idx, is_left, min_distance.value(ctx));
			partition_plan_worker ppw(worker);
			plan.add_instruction(parent_cut_idx, ppw, PRIORITY_PRECUT_CONSTRAINT, errors());

			// TODO: verify existence of parallel constraint between the cuts? no needed
			/*auto& constraints = model.constraints();
			for (auto constraint1 : constraints)
			{
			if (cut->parent->id == constraint1->cut_idx)
			_cut_idx;
			segments_angle_constraint* angle_constraint = constraint1->as_angle_constraint();
			if (angle_constraint)
			}*/
		}
	}
}

segment_distance_constraint_pct::segment_distance_constraint_pct(int id, int cut1_, cut_segment_id segment1_, cut_segment_id segment2_, bool has_min_distance_, vm::variable_value min_distance_pct_, bool has_max_distance_, vm::variable_value max_distance_pct_, cut_segment_id seg_pct_ref_) :
	segment_distance_constraint_base(id, cut1_, segment1_, segment2_, has_min_distance_, has_max_distance_),
	min_distance_pct(min_distance_pct_),
	max_distance_pct(max_distance_pct_),
	seg_pct_ref(seg_pct_ref_)
{
}

void segment_distance_constraint_pct::add_cut_instruction(vm::const_icontext_ref ctx, partition_plan& plan)
{
	restrict_cut_distance_pct worker(_cut_idx, segment1, segment2, has_min_distance, min_distance_pct.value(ctx), has_max_distance, max_distance_pct.value(ctx), seg_pct_ref);
	partition_plan_worker ppw(worker);
	plan.add_instruction(_cut_idx, ppw, PRIORITY_DISTANCE_CONSTRAINT, errors());
}

void segment_distance_constraint_pct::build(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan)
{
	// select the percent reference
	if (seg_pct_ref.valid() && model.is_base_segment(seg_pct_ref))
	{
		select_edges se(seg_pct_ref);
		partition_plan_brancher ppb(se);
		auto iidx = plan.add_instruction(_cut_idx, ppb, PRIORITY_SELECT_EDGES_SECONDARY);

		property_tree error_data;
		if (model.select_edge_error(seg_pct_ref, error_data))
			plan.register_error(iidx, seg_pct_ref.value, error_data);
	}

	segment_distance_constraint_base::build(ctx, model, plan);
}

