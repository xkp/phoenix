
#include "stdafx.h"
#include "partition_tesselator.h"
#include "partition_errors.h"
#include "../geometry_ops.h"
#include "../bezier_utils.h"
#include <CGAL/Arr_batched_point_location.h>

DEFAULT_UTILS();
DEFAULT_BEZIER_UTILS();

// end is past the colinear range
segment2 get_colinear_range(const edge2& src, edge2& start, edge2& end)
{
	auto line = line2(src->source()->point(), src->target()->point());

	edge2 edge = src->prev();
	while (line.has_on(edge->source()->point()))
	{
		// break infinite loop
		if (edge == src)
		{
			//std::cout << "breaking infinite loop\n";
			throw std::runtime_error("breaking infinite loop");
			edge = src->prev();
			break;
		}
		edge = edge->prev();
	}
	start = edge->next();
	auto source = edge->target()->point();

	edge = src->next();
	while (line.has_on(edge->target()->point()))
	{
		// break infinite loop
		if (edge == src)
		{
			//std::cout << "breaking infinite loop\n";
			throw std::runtime_error("breaking infinite loop");
			edge = src->next();
			break;
		}
		edge = edge->next();
	}
	end = edge;
	auto target = edge->source()->point();

	return segment2(source, target);
}

void compass_labels::apply(geometry::edge2 he_north, geometry::edge2 he_south, ignore_what ignore) const
{
	edge2 north_start, north_end;
	get_colinear_range(he_north, north_start, north_end);
	if (ignore != IGNORE_NORTH)
		north.apply(north_start, north_end);

	edge2 south_start, south_end;
	get_colinear_range(he_south, south_start, south_end);
	if (ignore != IGNORE_SOUTH)
		south.apply(south_start, south_end);

	west.apply(north_end, south_start);
	opp_west.apply_twin(north_end, south_start);

	east.apply(south_end, north_start);
	opp_east.apply_twin(south_end, north_start);
}

// north edge is collapsed into a point
void compass_labels::apply(point2 pt_north, geometry::edge2 he_south, ignore_what ignore) const
{
	if (ignore != IGNORE_SOUTH)
		south.apply(he_south);

	edge2 north_start = he_south->next();
	while (north_start->source()->point() != pt_north)
	{
		north_start = north_start->next();
		if (north_start == he_south)
			return; // sanity check for infinite loop
	}

	west.apply(north_start, he_south);

	east.apply(he_south->next(), north_start);
}

void compass_labels::apply(geometry::edge2 he_north, point2 pt_south, ignore_what ignore) const
{
	if (ignore != IGNORE_NORTH)
		north.apply(he_north);

	edge2 south_start = he_north->next();
	while (south_start->source()->point() != pt_south)
	{
		south_start = south_start->next();
		if (south_start == he_north)
			return; // sanity check for infinite loop
	}

	west.apply(he_north->next(), south_start);

	east.apply(south_start, he_north);
}

void partition_tesselator::apply_labels(geometry::edge2 he_north, geometry::edge2 he_south, int face_label, const compass_labels& edge_labels, ignore_what ignore)
{
	if (face_label >= 0)
		he_north->face()->data().label = face_label;

	edge_labels.apply(he_north, he_south, ignore);
}

void partition_tesselator::apply_labels(point2 pt_north, geometry::edge2 he_south, int face_label, const compass_labels& edge_labels, ignore_what ignore)
{
	if (face_label >= 0)
		he_south->face()->data().label = face_label;

	edge_labels.apply(pt_north, he_south, ignore);
}

void partition_tesselator::apply_labels(geometry::edge2 he_north, point2 pt_south, int face_label, const compass_labels& edge_labels, ignore_what ignore)
{
	if (face_label >= 0)
		he_north->face()->data().label = face_label;

	edge_labels.apply(he_north, pt_south, ignore);
}


const int TAG_FACE_ADDED = 1;

class face_observer : public CGAL::Arr_observer<geometry::arrangement2>
{
	face2_list& _faces;
	//std::set<face2> added_faces;

public:
	face_observer(geometry::arrangement2& arr, face2_list& faces) :
		CGAL::Arr_observer<geometry::arrangement2>(arr),
		_faces(faces)
	{
	}

	void add_face(face2 f)
	{
		//if (added_faces.find(f) == added_faces.end())
		{
			//added_faces.insert(f);
			_faces.push_back(f);
		}
	}

	virtual void after_split_face(Face_handle f1, Face_handle f2, bool is_hole)
	{
		add_face(f2);
	}
};

//void partition_tesselator::run(vm::const_icontext_ref ctx, arrangement2& arr, face2 f, const partition_model& model, partition_view& view, randomizer* rand, face2_list& result_faces)
void partition_tesselator::run(face2_list& result_faces)
{
	try
	{
		clock_t start = clock();

		face_observer observer(_arr, result_faces);
		observer.add_face(_f);

		edge_map edges;
		for (auto it = _view.segments.begin(); it != _view.segments.end(); it++)
		{
			if (it->second.redge.he_start != edge2())
				edges[it->first].edge = it->second.redge.he_start;
		}

		auto dj_tess = _model.get_dj().begin("<Tesselator");
		dj_tess.add("input", _arr);

		// put the labels into the base segments
		auto& base_labels = _model.get_base_labels();
		std::set<repo_edge2*> used_segments;

		// set the labels of the base segments (will-be-labeled facts)
		for (auto it : base_labels)
		{
			auto* segInfo = _view.segment(it.first);
			if (segInfo && segInfo->redge.has_edge())
			{
				if (it.second.label >= 0)
					segInfo->redge.set_label(it.second.label);
				if (it.second.opp_label >= 0)
					segInfo->redge.set_opp_label(it.second.opp_label);
			}
			else
			{
				//this is an unmatched segment, pick any matching edge
				auto* edges = (repo_edge2_list*)_view.repo->get(it.first);
				if (edges)
				{
					auto found = false;
					for (auto& eit : *edges)
					{
						//make sure said segment is not already in use
						if (!_view.has_edge(eit) && used_segments.find(&eit) == used_segments.end())
						{
							found = true;
							used_segments.insert(&eit);
							if (it.second.label >= 0)
								eit.set_label(it.second.label);
							if (it.second.opp_label >= 0)
								eit.set_opp_label(it.second.opp_label);

							break;
						}
					}

					if (!found)
					{
						//td: error: no segment to be labeled
						//std::cout << "tesselator: label not found1\n";
					}
				}
				else
				{
					//td: error: no segment to be labeled
					//std::cout << "tesselator: label not found2\n";
				}
			}
		}

		curves_original_seg_t curves_seg;
		if (_model.cut_root())
        {
            debug_json tdj = dj_tess.begin("cut_root");
			_root_node = tessellator_node_ref(new tessellator_node());
			do_cut(_model.cut_root(), edges, tdj, curves_seg, _root_node);
        }

		//dj_tess.add("solution", _arr);

		clock_t finish = clock();
		double time = (start > 0) ? (double)(finish - start) / CLOCKS_PER_SEC : -1;
		//dj_tess.add("time", time);
	}
	catch (const std::exception &exc)
	{
		throw partition_errors::create(PARTITION_ERROR_TESSELLATOR).add_message(exc.what());
	}

}

unsigned partition_tesselator::distribute_by_length(const partition_repeat& repeat, double dist_left, double dist_right, cut_slope& slope)
{
	double min_dist = dist_left;
	double max_dist = dist_right;

	if (dist_left > dist_right)
		std::swap(min_dist, max_dist);

	auto maximun_cuts = (int)repeat.maximun_cuts.value(_ctx);
	auto maximun_cuts_range = (int)repeat.maximun_cuts_range.value(_ctx);
	if (maximun_cuts_range > 0)
		maximun_cuts = _rand->random(maximun_cuts, maximun_cuts_range);
	double max_length = 0;
	auto length_range = repeat.length_range.value(_ctx);
	auto length_step = repeat.length_step.value(_ctx);
	if (length_range > 0)
	{
		if (maximun_cuts > 0)
		{
			max_length = min_dist / maximun_cuts;
			if (max_length > length_range)
				max_length = length_range;
		}
		else
			max_length = length_range;
	}

	double secondary_len = repeat.secondary.value(_ctx);
	auto secondary_len_range = repeat.secondary_range.value(_ctx);
	auto secondary_len_step = repeat.secondary_step.value(_ctx);
	if (secondary_len_range > 0)
		secondary_len = _rand->random(secondary_len, secondary_len_range, secondary_len_step);

	double margin_start = repeat.margin_start.value(_ctx);
	double margin_end = repeat.margin_end.value(_ctx);
	bool has_margin_start = margin_start >= 0;
	bool has_margin_end = margin_end >= 0;
	if (!has_margin_start)
		margin_start = secondary_len;
	if (!has_margin_end)
		margin_end = secondary_len;

	double length = repeat.length.value(_ctx);
	if (length <= 0)
		throw partition_errors::create(PARTITION_ERROR_MISSING_REPEAT_LENGTH);

	double primary_len;
	if (max_length <= 0)
		primary_len = length;
	else if (max_length < length)
		return 0;
	else
		primary_len = _rand->random(length, max_length, length_step);

	//times = (int)floor(1e-5 + (min_dist - secondary_len) / (primary_len + secondary_len));
	double ftimes = 1e-5 + (min_dist - margin_start - margin_end + secondary_len) / (primary_len + secondary_len);
	//if (repeat.adjust_mode == CUT_ADJ_PRIMARY || repeat.adjust_mode == CUT_ADJ_SECONDARY)
	int times = (int)floor(ftimes + 1e-5);
	//else
	//	times = (int)ceil(ftimes - 1e-5);

	if (times <= 0)
		return 0;

	if (maximun_cuts > 0 && maximun_cuts < times)
		times = maximun_cuts;

	// make the adjustements due to integer conversion of 'times'
	if (repeat.adjust_mode == CUT_ADJ_PRIMARY)
	{
		// re-adjust primary_len
		primary_len = (min_dist - margin_start - margin_end + secondary_len*(1 - times)) / times;
		if (primary_len < 0)
			return 0;
	}
	else if (repeat.adjust_mode == CUT_ADJ_SECONDARY)
	{
		// re-adjust secondary
		if (!has_margin_start && !has_margin_end)
			secondary_len = (min_dist - primary_len*times) / (times + 1);
		else if (has_margin_start && has_margin_end)
		{
			if (times == 1)
				secondary_len = -1; // imposible to adjust in this case (no secondary cuts)
			else
				secondary_len = (min_dist - primary_len*times - margin_start - margin_end) / (times - 1);
		}
		else if (has_margin_start && !has_margin_end)
			secondary_len = (min_dist - margin_start - primary_len*times) / times;
		else// if (!has_margin_start && has_margin_end)
			secondary_len = (min_dist - margin_end - primary_len*times) / times;

		if (secondary_len < 0)
			return 0;
		if (secondary_len < 0.001)
			secondary_len = 0;

		if (!has_margin_start)
			margin_start = secondary_len;
		if (!has_margin_end)
			margin_end = secondary_len;
	}
	else if (repeat.adjust_mode == CUT_ADJ_FIRST)
	{
		// re-adjust margin
		margin_start = min_dist - margin_end + secondary_len - (primary_len + secondary_len)*times;

		if (margin_start < 0)
			return 0;
		if (margin_start < 0.001)
			margin_start = 0;
	}
	else if (repeat.adjust_mode == CUT_ADJ_LAST)
	{
		// re-adjust margin
		margin_end = min_dist - margin_start + secondary_len - (primary_len + secondary_len)*times;

		if (margin_end < 0)
			return 0;
		if (margin_end < 0.001)
			margin_end = 0;
	}
	else if (repeat.adjust_mode == CUT_ADJ_EXTREMES)
	{
		// re-adjust margin
		margin_start = (min_dist + secondary_len - (primary_len + secondary_len)*times)*0.5;

		if (margin_start < 0)
			return 0;
		if (margin_start < 0.001)
			margin_start = 0;
		margin_end = margin_start;
	}

	auto primary_len_max = (max_dist - margin_start - margin_end + secondary_len*(1 - times)) / times;

	if (dist_left > dist_right)
	{
		slope.pri_lens = primary_len_max;
		slope.pri_lent = primary_len;
	}
	else
	{
		slope.pri_lens = primary_len;
		slope.pri_lent = primary_len_max;
	}

	slope.sec_len = secondary_len;
	slope.margin_start = margin_start;
	slope.margin_end = margin_end;

	return times;
}

/*void partition_tesselator::fix_limits(const line2& middle_line, point2& pt_left, point2& pt_right, const line2& line_left, const line2& line_right, bool is_target)
{
	auto perp_line = middle_line.perpendicular(pt_left);
	auto side = perp_line.oriented_side(pt_right);
	if (side == CGAL::ON_BOUNDARY)
		return;

	if (side == (is_target ? CGAL::ON_NEGATIVE_SIDE : CGAL::ON_POSITIVE_SIDE))
	{	// use line of pt_left to find the correct pt_right
		auto inter_p = CGAL::intersection(perp_line, line_right);
		auto pi = boost::get<point2>(&*inter_p);
		if (pi)
			pt_right = *pi;
		else
			std::cout << "!!!!! \n";
		std::cout << "fix pt_right: " << pt_right << "\n";
	}
	else
	{	// use line of pt_right to find the correct pt_left
		perp_line = middle_line.perpendicular(pt_right);
		auto inter_p = CGAL::intersection(perp_line, line_left);
		auto pi = boost::get<point2>(&*inter_p);
		if (pi)
			pt_left = *pi;
		else
			std::cout << "!!!!! \n";
		std::cout << "fix pt_left: " << pt_left << "\n";
	}
}*/

unsigned partition_tesselator::distribute_by_count(const partition_repeat& repeat, double dist_left, double dist_right, cut_slope& slope)
{
	unsigned count;

	int repeat_count = (int)round(repeat.count.value(_ctx));
	int repeat_count_range = (int)round(repeat.count_range.value(_ctx));
	int repeat_count_step = (int)round(repeat.count_step.value(_ctx));
	auto repeat_secondary = repeat.secondary.value(_ctx);
	auto repeat_secondary_range = repeat.secondary_range.value(_ctx);
	auto repeat_secondary_step = repeat.secondary_step.value(_ctx);

	for (int i = 0; i < 10; i++) // make 10 attemps to find valid values
	{
		count = repeat_count_range < 0 ? repeat_count : _rand->random(repeat_count, repeat_count_range, repeat_count_step);
		auto secondary_len = repeat_secondary_range < 0 ? repeat_secondary : _rand->random(repeat_secondary, repeat_secondary_range, repeat_secondary_step);

		double margin_start = repeat.margin_start.value(_ctx);
		double margin_end = repeat.margin_end.value(_ctx);
		if (margin_start < 0)
			margin_start = secondary_len;
		if (margin_end < 0)
			margin_end = secondary_len;

		slope.sec_len = secondary_len;
		slope.margin_start = margin_start;
		slope.margin_end = margin_end;

		slope.pri_lens = (dist_left - margin_start - margin_end - secondary_len*(count - 1)) / count;
		slope.pri_lent = (dist_right - margin_start - margin_end - secondary_len*(count - 1)) / count;

		if ((slope.pri_lens >= 0 && slope.pri_lent >= 0) || (repeat_count_range < 0 && repeat_secondary_range < 0))
			break;
	}

	return count;
}

edge2 partition_tesselator::cut_repeat_face(face2 f, const line2& l)
{
	typedef cut_face<GeometryKernel, arrangement2> cutter;
	cutter c(_arr, f, l);
	edge2 edge = c.cut();
	if (edge != edge2())
	{
		edge->data().id = _ctx->fctx()->edge_id();
		edge->twin()->data().id = edge->data().id;
		if (edge->source()->data().id < 0)
			edge->source()->data().id = _ctx->fctx()->vertex_id();
		if (edge->target()->data().id < 0)
			edge->target()->data().id = _ctx->fctx()->vertex_id();

		auto testLine = l.perpendicular(edge->source()->point());
		//auto test = testLine.has_on_negative_side(edge->target()->point());
		edge = testLine.has_on_negative_side(edge->target()->point()) ? edge : edge->twin();
		return edge;
	}

	return edge;
}

//void partition_tesselator::repeat_cut(const partition_cut* cut, edge2 src, edge2 dst, dj& dj_, tessellator_node_ref tess_node)
void partition_tesselator::repeat_cut(const partition_cut* cut, repo_item& repo_src, repo_item& repo_dst, dj& dj_, tessellator_node_ref tess_node)
{
	//DebugTimer timer("repeat_cut");

	//assert(src->face() == dst->face());

	edge2 src = repo_src.edge;
	edge2 dst = repo_dst.edge;

	dj_.add("src", src);
	dj_.add("dst", dst);

	if (src == edge2() || dst == edge2())
		throw std::runtime_error("degenerated segment in repeat cut");

	// get the expanded segments, in case they were splitted by other cuts
	edge2 src_start, src_end;
	edge2 dst_start, dst_end;
	segment2 src_exp = src != edge2() ? get_colinear_range(src, src_start, src_end) : segment2(repo_src.vertex->point(), repo_src.vertex->point());
	segment2 dst_exp = dst != edge2() ? get_colinear_range(dst, dst_start, dst_end) : segment2(repo_dst.vertex->point(), repo_dst.vertex->point());
	//dj_.add("src_exp", src_exp);
	//dj_.add("dst_exp", dst_exp);

	point2 src_left = src_exp.source();
	point2 dst_left = dst_exp.target();
	point2 src_right = src_exp.target();
	point2 dst_right = dst_exp.source();

	auto& repeat = cut->repeat;
	auto count = 0;
	line2 main_cut_line;

	auto direction = repeat.direction;
	if (direction == CUT_DIR_SOURCE && src_exp.is_degenerate())
		direction = CUT_DIR_CUT;
	else if (direction == CUT_DIR_TARGET && dst_exp.is_degenerate())
		direction = CUT_DIR_CUT;

	if (direction == CUT_DIR_CUT)
	{
		main_cut_line = line2(CGAL::midpoint(src_left, src_right), CGAL::midpoint(dst_left, dst_right));
	}
	else if (direction == CUT_DIR_TARGET)
	{
		main_cut_line = line2(dst_left, dst_right).perpendicular(dst_left);
	}
	else if (direction == CUT_DIR_SOURCE)
	{
		main_cut_line = line2(src_left, src_right).perpendicular(src_right);
	}

	cut_slope slope;
	point2 pt_start = src_right;
	point2 pt_end;

	double dist_left, dist_right;
	if (repeat.direction == CUT_DIR_INTERPOLATE)
	{
		dist_left = CGAL::sqrt(CGAL::to_double(CGAL::squared_distance(src_left, dst_left)));
		dist_right = CGAL::sqrt(CGAL::to_double(CGAL::squared_distance(src_right, dst_right)));
		pt_start = src_right;
	}
	else
	{
		// calculate start point
		auto perpendicular_line = main_cut_line.perpendicular(src_left);
		bool invert = !(repeat.extent == CUT_EXT_SOURCE_TARGET || repeat.extent == CUT_EXT_SOURCE);
		pt_start = perpendicular_line.has_on_negative_side(src_right) ^ invert ? src_left : src_right;

		// calculate end point
		perpendicular_line = main_cut_line.perpendicular(dst_left);
		invert = !(repeat.extent == CUT_EXT_SOURCE_TARGET || repeat.extent == CUT_EXT_TARGET);
		pt_end = perpendicular_line.has_on_negative_side(dst_right) ^ invert ? dst_right : dst_left;

		dist_right = dist_left = CGAL::sqrt(CGAL::to_double(CGAL::squared_distance(main_cut_line.perpendicular(pt_start), main_cut_line.perpendicular(pt_end))));
	}

	switch (cut->repeat.kind)
	{
	case REPEAT_N_TIMES:
		dj_.add("kind", "REPEAT_N_TIMES");
		count = distribute_by_count(repeat, dist_left, dist_right, slope);
		break;
	case REPEAT_BY_LENGTH:
		dj_.add("kind", "REPEAT_BY_LENGTH");
		dj_.add("length", repeat.length.value(_ctx));

		count = distribute_by_length(repeat, dist_left, dist_right, slope);
		/*if (space > 0)
		{
			count = re_distribute_length(space, count, margin, slope, rand);
			margin = space;
		}*/

		break;
	default:
		assert(false); //not implemented
	}

	if (repeat.direction == CUT_DIR_SOURCE || repeat.direction == CUT_DIR_TARGET)
	{	// use the smaller
		if (slope.pri_lens < slope.pri_lent)
			slope.pri_lent = slope.pri_lens;
		else
			slope.pri_lens = slope.pri_lent;
	}

	dj_.add("count", count);
	dj_.add("secondary_len", slope.sec_len);
	dj_.add("lengths", slope.pri_lens);
	dj_.add("lengtht", slope.pri_lent);

	if (count <= 0 || slope.pri_lens < 0 || slope.pri_lent < 0)
	{
		//std::cout << "NOT ENOUGH SPACE FOR REPEAT\n";
		dj_.error("NOT ENOUGH SPACE FOR REPEAT");
		throw partition_errors::create(PARTITION_ERROR_NOT_ENOUGH_SPACE_FOR_REPEAT).add_data("length", std::min(dist_left, dist_right));
		return;
	}

	// organize the labels
	compass_labels primary_labels, secondary_labels, margin_start_labels, margin_end_labels;

	int default_north_label = dst->data().label;
	int default_south_label = src->data().label;

	auto primary_explicit_north = compass_labels::label(cut->repeat.faceTopLabel, cut->repeat.primaryEdgeLabel);
	primary_labels.north.set(primary_explicit_north(), default_north_label);
	auto primary_explicit_south = compass_labels::label(cut->repeat.faceBottomLabel, cut->repeat.primaryEdgeLabel);
	primary_labels.south.set(primary_explicit_south(), default_south_label);
	primary_labels.west.set(cut->repeat.faceLeftLabel, cut->repeat.primaryEdgeLabel);
	primary_labels.east.set(cut->repeat.faceRightLabel, cut->repeat.primaryEdgeLabel);
	primary_labels.opp_west.set(cut->repeat.faceLeftLabelOpp);
	primary_labels.opp_east.set(cut->repeat.faceRightLabelOpp);

	auto sec_explicit_north = compass_labels::label(cut->repeat.secondaryTopLabel, cut->repeat.secondaryEdgeLabel);
	secondary_labels.north.set(sec_explicit_north(), default_north_label);
	auto sec_explicit_south = compass_labels::label(cut->repeat.secondaryBottomLabel, cut->repeat.secondaryEdgeLabel);
	secondary_labels.south.set(sec_explicit_south(), default_south_label);
	secondary_labels.west.set(cut->repeat.secondaryLeftLabel, cut->repeat.secondaryEdgeLabel);
	secondary_labels.east.set(cut->repeat.secondaryRightLabel, cut->repeat.secondaryEdgeLabel);
	secondary_labels.opp_west.set(cut->repeat.secondaryLeftLabelOpp);
	secondary_labels.opp_east.set(cut->repeat.secondaryRightLabelOpp);

	auto margin_start_explicit_north = compass_labels::label(cut->repeat.marginStartTopLabel, cut->repeat.marginStartEdgeLabel, sec_explicit_north());
	margin_start_labels.north.set(margin_start_explicit_north(), default_north_label);
	auto margin_start_explicit_south = compass_labels::label(cut->repeat.marginStartBottomLabel, cut->repeat.marginStartEdgeLabel, sec_explicit_south());
	margin_start_labels.south.set(margin_start_explicit_south(), default_south_label);
	margin_start_labels.west.set(cut->repeat.marginStartLeftLabel, cut->repeat.marginStartEdgeLabel)
		.if_empty(secondary_labels.west());
	margin_start_labels.east.set(cut->repeat.marginStartRightLabel, cut->repeat.marginStartEdgeLabel)
		.if_empty(secondary_labels.east());
	margin_start_labels.opp_west.set(cut->repeat.marginStartLeftLabelOpp, secondary_labels.opp_west());
	margin_start_labels.opp_east.set(cut->repeat.marginStartRightLabelOpp, secondary_labels.opp_east());

	auto margin_end_explicit_north = compass_labels::label(cut->repeat.marginEndTopLabel, cut->repeat.marginEndEdgeLabel, sec_explicit_north());
	margin_end_labels.north.set(margin_end_explicit_north(), default_north_label);
	auto margin_end_explicit_south = compass_labels::label(cut->repeat.marginEndBottomLabel, cut->repeat.marginEndEdgeLabel, sec_explicit_south());
	margin_end_labels.south.set(margin_end_explicit_south(), default_south_label);
	margin_end_labels.west.set(cut->repeat.marginEndLeftLabel, cut->repeat.marginEndEdgeLabel)
		.if_empty(secondary_labels.west());
	margin_end_labels.east.set(cut->repeat.marginEndRightLabel, cut->repeat.marginEndEdgeLabel)
		.if_empty(secondary_labels.east());
	margin_end_labels.opp_west.set(cut->repeat.marginEndLeftLabelOpp, secondary_labels.opp_west());
	margin_end_labels.opp_east.set(cut->repeat.marginEndRightLabelOpp, secondary_labels.opp_east());

	face2 f = src->face();

	edge2 cut_edge = src;
	edge2 he_south = cut_edge;

//#define DEBUG_REPEAT
	auto dj_iterations = dj_.begin_array("iterations");

#ifndef DEBUG_REPEAT
	dj_iterations.set_enabled(false);
#endif

	bool is_first_tess_node = true;
	auto add_tessellator_node = [&is_first_tess_node, &tess_node](point2 source, point2 target)
	{
		if (!is_first_tess_node)
			tess_node = tess_node->create_left();

		is_first_tess_node = false;
		tess_node->seg = segment2(source, target);
	};

	if (repeat.direction == CUT_DIR_INTERPOLATE)
	{
		vec2 dir_left = vec2(src_left, dst_left); geom_utils::normalize(dir_left);
		vec2 dir_right = vec2(src_right, dst_right); geom_utils::normalize(dir_right);

		point2 currs = src_left;
		point2 currt = src_right;

		for (int i = 0; i < count; i++)
		{
			auto dj_iter = dj_iterations.begin();
			dj_iter.set_flags(dj::COLLAPSED);
			dj_iter.add("face", f);
			if ((i == 0 && slope.margin_start > 0) || slope.sec_len > 0)
			{	// margin start or secondary chunk
				if (i == 0)
				{
					slope.advance_margin_start(currs, dir_left);
					slope.advance_margin_start(currt, dir_right);
				}
				else
				{
					slope.advance_sec(currs, dir_left);
					slope.advance_sec(currt, dir_right);
				}
				//currs = currs + dir1*secondary_len;
				//currt = currt + dir2*secondary_len;
				add_tessellator_node(currs, currt);

				//dj_iter.add("cut_line1", segment2(currs, currt));
				auto new_cut_edge = cut_repeat_face(f, line2(currs, currt));
				if (new_cut_edge != edge2())
				{
					cut_edge = new_cut_edge;
					f = cut_edge->face();

					if (i == 0 && slope.margin_start > 0) // first margin
					{
						tess_node->right_face_label = cut->repeat.marginStartFaceLabel;
						tess_node->repeat_edge_labels = margin_start_labels;
						apply_labels(cut_edge->twin(), he_south, cut->repeat.marginStartFaceLabel, margin_start_labels, (i == 0 && margin_start_explicit_south.empty()) ? IGNORE_SOUTH : IGNORE_NONE);
					}
					else
					{
						tess_node->right_face_label = cut->repeat.secondaryLabel;
						tess_node->repeat_edge_labels = secondary_labels;
						apply_labels(cut_edge->twin(), he_south, cut->repeat.secondaryLabel, secondary_labels, (i == 0 && sec_explicit_south.empty()) ? IGNORE_SOUTH : IGNORE_NONE);
					}

					he_south = cut_edge;
					dj_iter.add("face_margin", cut_edge->twin()->face());
				}
				else
					dj_iter.error("margin cut failed");
			}

			if (i == count - 1 && slope.margin_end == 0)
			{	// last primary chunk in case of no margin
				tess_node->left_face_label = cut->repeat.faceLabel;
				tess_node->last_repeat_edge_labels = primary_labels;
				apply_labels(dst, he_south, cut->repeat.faceLabel, primary_labels, primary_explicit_north.empty() ? IGNORE_NORTH : IGNORE_NONE);
			}
			else
			{	// primary chunks
				slope.advance_pri_left(currs, dir_left);
				slope.advance_pri_right(currt, dir_right);
				dj_iter.add("cut_line2", segment2(currs, currt));
				add_tessellator_node(currs, currt);

				auto new_cut_edge = cut_repeat_face(f, line2(currs, currt));
				if (new_cut_edge != edge2())
				{
					cut_edge = new_cut_edge;
					f = cut_edge->face();
				}
				else
					dj_iter.error("face cut failed");

				tess_node->right_face_label = cut->repeat.faceLabel;
				tess_node->repeat_edge_labels = primary_labels;

				if (i == 0 && slope.margin_start <= 0 && primary_explicit_south.empty())
				{
					// first primary chunk in case of no margin
					apply_labels(cut_edge->twin(), he_south, cut->repeat.faceLabel, primary_labels, primary_explicit_south.empty() ? IGNORE_SOUTH : IGNORE_NONE);
				}
				else
				{
					apply_labels(cut_edge->twin(), he_south, cut->repeat.faceLabel, primary_labels, IGNORE_NONE);
				}

				he_south = cut_edge;

				dj_iter.add("face_main", cut_edge->twin()->face());
			}

			if (i == count - 1 && slope.margin_end > 0)
			{	// last margin
				tess_node->left_face_label = cut->repeat.marginEndFaceLabel;
				tess_node->last_repeat_edge_labels = margin_end_labels;
				apply_labels(dst, he_south, cut->repeat.marginEndFaceLabel, margin_end_labels, margin_end_explicit_north.empty() ? IGNORE_NORTH : IGNORE_NONE);
			}

			//io_utils<GeometryKernel, geometry::arrangement2, geometry::polyhedron3>::save(arr, "c:\\dev\\repeat.svg");
		}
	}
	else
	{
		point2 pt = pt_start;
		vec2 dir = main_cut_line.to_vector(); geom_utils::normalize(dir);
		double primary_len = slope.pri_lent;
		double secondary_len = slope.sec_len;
		double margin_start = slope.margin_start;

		for (int i = 0; i < count; i++)
		{

			if ((i == 0 && slope.margin_start > 0) || slope.sec_len > 0)
			{	// margin start or secondary chunk
				pt = pt + dir*(i == 0 ? margin_start : secondary_len);

				auto new_cut_edge = cut_repeat_face(f, main_cut_line.opposite().perpendicular(pt));
				if (new_cut_edge != edge2())
				{
					add_tessellator_node(new_cut_edge->source()->point(), new_cut_edge->target()->point());

					cut_edge = new_cut_edge;
					f = cut_edge->face();
					edge2 he_north = cut_edge->twin();

					if (i > 0 || slope.margin_start <= 0)
					{	// secondary chunks
						tess_node->right_face_label = cut->repeat.secondaryLabel;
						tess_node->repeat_edge_labels = secondary_labels;

						apply_labels(he_north, he_south, cut->repeat.secondaryLabel, secondary_labels);
					}
					else
					{	// start margin
						tess_node->right_face_label = cut->repeat.marginStartFaceLabel;
						tess_node->repeat_edge_labels = margin_start_labels;

						if (src_exp.has_on(he_north->source()->point()))
						{ // south segment is collapsed
							apply_labels(he_north, src_exp.source(), cut->repeat.marginStartFaceLabel, margin_start_labels, IGNORE_NONE);
						}
						else if (src_exp.has_on(he_north->target()->point()))
						{ // south segment is collapsed
							apply_labels(he_north, src_exp.target(), cut->repeat.marginStartFaceLabel, margin_start_labels, IGNORE_NONE);
						}
						else
							apply_labels(he_north, he_south, cut->repeat.marginStartFaceLabel, margin_start_labels, (i == 0 && margin_start_explicit_south.empty()) ? IGNORE_SOUTH : IGNORE_NONE);
					}

					he_south = cut_edge;
				}
				else
					;//dj_iter.error("margin cut failed");
			}

			if (i == count - 1 && slope.margin_end == 0)
			{	// last primary chunk in case of no margin
				tess_node->left_face_label = cut->repeat.faceLabel;
				tess_node->last_repeat_edge_labels = primary_labels;

				apply_labels(dst, he_south, cut->repeat.faceLabel, primary_labels, primary_explicit_north.empty() ? IGNORE_NORTH : IGNORE_NONE);
			}
			else
			{	// primary chunks
				pt = pt + dir*primary_len;

				auto new_cut_edge = cut_repeat_face(f, main_cut_line.opposite().perpendicular(pt));
				if (new_cut_edge != edge2())
				{
					add_tessellator_node(new_cut_edge->source()->point(), new_cut_edge->target()->point());

					cut_edge = new_cut_edge;
					f = cut_edge->face();
				}
				else
					;//dj_iter.error("face cut failed");

				tess_node->right_face_label = cut->repeat.faceLabel;
				tess_node->repeat_edge_labels = primary_labels;

				if (i == 0 && slope.margin_start <= 0 && primary_explicit_south.empty())
					// first primary chunk in case of no margin
					apply_labels(cut_edge->twin(), he_south, cut->repeat.faceLabel, primary_labels, primary_explicit_south.empty() ? IGNORE_SOUTH : IGNORE_NONE);
				else
					apply_labels(cut_edge->twin(), he_south, cut->repeat.faceLabel, primary_labels, IGNORE_NONE);
				he_south = cut_edge;
			}

			if (i == count - 1 && slope.margin_end > 0)
			{	// last margin
				tess_node->left_face_label = cut->repeat.marginEndFaceLabel;
				tess_node->last_repeat_edge_labels = margin_end_labels;

				if (dst_exp.has_on(he_south->source()->point()))
				{ // north segment is collapsed
					apply_labels(dst_exp.source(), he_south, cut->repeat.marginEndFaceLabel, margin_end_labels, IGNORE_NONE);
				}
				else if (dst_exp.has_on(he_south->target()->point()))
				{ // north segment is collapsed
					apply_labels(dst_exp.target(), he_south, cut->repeat.marginEndFaceLabel, margin_end_labels, IGNORE_NONE);
				}
				else
					apply_labels(dst, he_south, cut->repeat.marginEndFaceLabel, margin_end_labels, margin_end_explicit_north.empty() ? IGNORE_NORTH : IGNORE_NONE);
			}

			//io_utils<GeometryKernel, geometry::arrangement2, geometry::polyhedron3>::save(arr, "c:\\dev\\repeat.svg");
		}
	}
}

edge2 find_correct_edge(edge2 src, const point2& ps)
{
	// find the correct segment to split
	auto start = src;
	auto line = line2(src->source()->point(), src->target()->point());
	bool forward = line.perpendicular(src->source()->point()).has_on_negative_side(ps);
	while (!((segment2)src->curve()).has_on(ps))
	{
		src = forward ? src->next() : src->prev();
		if (src == start) 
		{
			//std::cout << "edge not found\n";
			break; // avoid infinite loop, degenerated face?
		}

		// verify next vertex is on the line
		point2 next_vertex = (forward ? src->target() : src->source())->point();
		if (!line.has_on(next_vertex)) 
		{
			//std::cout << "edge is out of the line\n";
			break; // next edge is out of the line
		}
	}

	return src;
}

bool get_edge_range(const segment2& seg, edge2 hint, edge2& he_start, edge2& he_end)
{
	if (hint == edge2())
		return false;

	point2 start_pt = seg.source();
	point2 end_pt = seg.target();
	// seg direction may be contrary to halfedges direction
	if (hint->source()->point() == end_pt || hint->target()->point() == start_pt)
		std::swap(start_pt, end_pt);

	he_start = hint->prev();
	he_end = hint;

	for (int i=0; he_start->target()->point() != start_pt; he_start = he_start->prev())
	{
		if (he_start->prev() == hint)
		{
			std::cout << "infinite loop1_" << i << std::endl;
			return false;
		}
		i++;
	}

	he_start = he_start->next();

	for (int i=0; he_end->target()->point() != end_pt; he_end = he_end->next())
	{
		if (he_end->next() == hint)
		{
			std::cout << "infinite loop2_" << i << std::endl;
			return false;
		}
		i++;
	}

	// he_end points to one edge pass the valid range
	he_end = he_end->next();

	return true;
}

void apply_edge_labels(edge2 he, int label, int label_opp, const segment2& seg)
{
	if ((label < 0 && label_opp < 0) || seg == segment2())
		return;

	edge2 he_start, he_end;
	if (get_edge_range(seg, he, he_start, he_end))
	{
		for (he = he_start; he != he_end; he = he->next())
		{
			if (label >= 0)
				he->data().label = label;
			if (label_opp >= 0)
				he->twin()->data().label = label_opp;
		}
	}
}

edge2 prev_colinear(const edge2& he)
{
	auto prev = he->prev();
	return he->curve().line().has_on(prev->source()->point()) ? prev : edge2();
}

edge2 next_colinear(const edge2& he)
{
	auto next = he->next();
	return he->curve().line().has_on(next->target()->point()) ? next : edge2();
}

bool collapsable(const point2& p1, const point2& p2)
{
	const double epsilon2 = 1e-5*1e-5;
	return p1 == p2; // || CGAL::squared_distance(p1, p2) <= epsilon2;
}

enum side_result { SIDE_POSITIVE, SIDE_NEGATIVE, SIDE_BOTH, SIDE_COLINEAR };

void classify_point(const line2& line, const point2& pt, bool& negative, bool& positive)
{
	auto side = line.oriented_side(pt);
	if (side == CGAL::ON_NEGATIVE_SIDE)
		negative = true;
	else if (side == CGAL::ON_POSITIVE_SIDE)
		positive = true;
}

side_result classify_edges(const line2& line, const edge2& seg1, const edge2& seg2)
{
	bool negative = false, positive = false;

	if (seg1 != edge2())
	{
		classify_point(line, seg1->source()->point(), negative, positive);
		classify_point(line, seg1->target()->point(), negative, positive);
	}

	if (seg2 != edge2())
	{
		classify_point(line, seg2->source()->point(), negative, positive);
		classify_point(line, seg2->target()->point(), negative, positive);
	}

	if (negative && positive)
		return SIDE_BOTH;
	else if (negative)
		return SIDE_NEGATIVE;
	else if (positive)
		return SIDE_POSITIVE;
	else
		return SIDE_COLINEAR;
}

edge2 find_edge(const vertex2& source, const vertex2& target)
{
	auto start = target->incident_halfedges();
	auto it = start;
	do
	{
		//std::cout << ", s: " << it->source()->point() << ", t: " << it->target()->point() << "\n";
		if (it->source() == source)
			return it;
	} while (++it != start);

	return edge2();
}

// fix the case:
//----------------------
//    |    *
//    |    *
//    |    *
//-----    * <
//         * <
//         * <
//-----    * <
//    |    *
//    |    *
//    |    *
//    |    *
//    ------------------
//
// where the * is the cut and the -| are arrangement edges, the * and the | are on the same line.
// the < is the gap and interval returned.
// this only advance one edge in both directions, some looping may be necesary for more complex cases.
// advance one step from source and target towards the interior of that interval while remaining on the same line
// if there is no gap then return a single edge on the line
edge2 fix_endpoints(vertex2& source, vertex2& target)
{
	edge2 edge;
	auto segment = segment2(source->point(), target->point());
	auto start = target->incident_halfedges();
	auto it = start;
	do
	{
		if (segment.has_on(it->source()->point()))
		{
			target = it->source();
			break;
		}
	} while (++it != start);

	start = source->incident_halfedges();
	it = start;
	do
	{
		if (segment.has_on(it->source()->point()))
		{
			if (it->source() == target)
				edge = find_edge(source, target);
			else
				source = it->source();
			break;
		}
	} while (++it != start);

	return edge;
}

edge2 partition_tesselator::insert_at_vertices(vertex2& vs, vertex2& vd, bool& inserted)
{
	//file_utils::save(arr, "c:\\dev\\tess_before.svg", save_request(SYMETRIC));
	/*file_utils::svg_doc()
		.add(arr)
		.add(segment2(vs->point(), vd->point()), svg::Color::Red)
		.save("c:\\dev\\tess_before.svg");*/
	//td: this does not work with splitted edges
	auto existing_edge = find_edge(vs, vd);
	edge2 result;
	if (existing_edge != edge2())
	{
		// pathological case: there is already an edge between vs and vd, return that and dont insert any edge
		result = existing_edge;
		inserted = false;
	}
	else
	{
		//static int kk = 0;
		//kk++;
		//std::cout << kk << std::endl;
		existing_edge = fix_endpoints(vs, vd);
		if (existing_edge != edge2())
		{
			// pathological case: there is already an edge between vs and vd, return that and dont insert any edge
			result = existing_edge;
			inserted = false;
		}
		else
		{
			/*file_utils::svg_doc()
				.add(arr)
				.add(segment2(vs->point(), vd->point()), svg::Color::Red)
				.save("c:\\dev\\tess_before2.svg");*/

			segment2 cut_segment(vs->point(), vd->point());
			if (cut_segment.is_degenerate())
			{
				throw std::logic_error("inserting vertices, degenerated segment");
			}
			try
			{
				result = _arr.insert_at_vertices(cut_segment, vs, vd);

				result->data().id = _ctx->fctx()->edge_id();
				result->twin()->data().id = result->data().id;
			}
			catch (...)
			{
				/*file_utils::svg_doc()
					.add(arr)
					.add(cut_segment, svg::Color::Red)
					.save("c:\\dev\\tess_error.svg");*/

				throw std::logic_error("inserting vertices");
			}

			inserted = true;
		}
	}

	//file_utils::save(arr, "c:\\dev\\tess_after.svg", save_request(SYMETRIC));
	return result;
}

edge2_list partition_tesselator::insert_bezier(curves_original_seg_t& curves_seg, face2& f, vertex2& vs, vertex2& vd, const partition_cut* cut)
{
	bezier_cp control_points = cut->control_points;
	int label = cut->cutLeft;
	int label_opp = cut->cutRight;

	point2_list points;
	edge2_list edges;

	//bezier_options options;// = bezier_options::by_count(10);
	bezier_options options = control_points.bez_options.to_bezier_options(_ctx);
	point2 cp1, cp2;
	control_points.compute(f, curves_seg, vs, vd, cp1, cp2);
	//std::cout << "cp: " << cp1 << " | " << cp2 << "\n";
	bezier_utils::subdivide_bezier(points, vs->point(), cp1, cp2, vd->point(), options);
	//std::cout << "points: " << points.size() << "\n";
	//for (auto it = points.begin(); it != points.end(); it++)
	//	std::cout << *it << ";    ";
	//std::cout << "\n";

	if (points.size() > 2)
	{
		typedef CGAL::Arr_point_location_result<arrangement2>  Point_location_result;
		typedef std::pair<point2, Point_location_result::Type> Query_result;

		std::list<Query_result>  results;
		auto it1 = points.begin();
		auto it2 = points.end();
		it1++; it2--;
		CGAL::locate(_arr, it1, it2, std::back_inserter(results));
		//CGAL::locate(arr, points.begin() + 1, points.end() - 1, std::back_inserter(results));
		std::list<Query_result>::const_iterator it;
		bool point_outside = false;
		bool point_in_unbounded = false;
		for (it = results.begin(); !point_outside && it != results.end(); ++it)
		{
			if (const const_face2* f2 = boost::get<const_face2>(&(it->second)))       // inside a face
			{
				if (*f2 != f)
				{
					point_in_unbounded = is_unbounded_face(*f2);
					point_outside = true;
					//std::cout << "outside " << (*f2)->data().label << "\n";
				}
			}
			else if (const const_edge2* e = boost::get<const_edge2>(&(it->second))) // on an edge
			{
				point_outside = true;
				//std::cout << "point on edge\n";
			}
			else if (const const_vertex2* v = boost::get<const_vertex2>(&(it->second)))  // on a vertex
			{
				point_outside = true;
				//std::cout << "point on vertex\n";
			}
		}

		if (point_outside)
		{
			throw partition_errors::create(point_in_unbounded ? PARTITION_ERROR_CURVE_OUT_OF_BOUNDS : PARTITION_ERROR_CURVE_COLLISION).add_message(" (cut_" + std::to_string(cut->id) + "_Cut)");
		}
	}

	//auto edgeid = ctx->fctx()->edge_id();
	//curves_seg[edgeid] = segment2(vs->point(), vd->point());

	auto last_vertex = vs;
	for (auto it = points.begin()+1; it != points.end(); it++)
	{
		vertex2 vertex = (it+1) != points.end() ? _arr.insert_in_face_interior(*it, f) : vd;
		if (vertex->data().id < 0)
			vertex->data().id = _ctx->fctx()->vertex_id();
		segment2 cut_segment(last_vertex->point(), vertex->point());// vd->point());
		try
		{
			edge2 result = _arr.insert_at_vertices(cut_segment, last_vertex, vertex);
			edges.push_back(result);

			auto edgeid = _ctx->fctx()->edge_id();
			curves_seg[edgeid] = segment2(vs->point(), vd->point());
			/* */
			result->data().id = edgeid;
			result->twin()->data().id = edgeid;
		}
		catch (...)
		{
			/*file_utils::svg_doc()
			.add(arr)
			.add(cut_segment)
			.save("c:\\dev\\tess.svg");*/

			throw std::logic_error("inserting vertices in bezier");
		}

		last_vertex = vertex;
	}

	// apply labels
	if (label >= 0 || label_opp >= 0)
		for (auto& he : edges)
		{
			if (label >= 0)
				he->data().label = label;

			if (label_opp >= 0)
				he->twin()->data().label = label_opp;
		}

	return edges;
}

edge2 partition_tesselator::insert_cut(const partition_cut* cut, const segment2& cut_segment, curves_original_seg_t& curves_seg, face2& f, vertex2& vs, vertex2& vd)
{
	edge2 result;

	if (cut->control_points.enabled)
	{
		result = insert_bezier(curves_seg, f, vs, vd, cut)[0];
	}
	else
	{
		result = _arr.insert_at_vertices(cut_segment, vs, vd);

		result->data().id = _ctx->fctx()->edge_id();
		result->twin()->data().id = result->data().id;

		if (cut->cutRight >= 0)
			result->twin()->data().label = cut->cutRight;

		if (cut->cutLeft >= 0)
			result->data().label = cut->cutLeft;
	}

	return result;
}


void partition_tesselator::add_repo(edge_map& edges, repo_segment_id segment_id, const edge2& he, const vertex2& v)
{
	if (edges.find(segment_id) == edges.end())
	{
		auto& ritem = edges[segment_id];
		ritem.edge = he;
		if (he == edge2())
			ritem.vertex = v;
	}
}

void partition_tesselator::do_cut(const partition_cut* cut, edge_map& edges, dj& dj_, curves_original_seg_t& curves_seg, tessellator_node_ref tess_node)
{
	//DebugTimer timer("do_cut");
	repo_item& repo_src = edges[cut->src];
	repo_item& repo_dst = edges[cut->dst];
	edge2 src = repo_src.edge;
	edge2 dst = repo_dst.edge;
	//edge2 src = edges[cut->src].edge;
	//edge2 dst = edges[cut->dst].edge;
	if (src == dst)
	{
		assert(false);
	}


	if (cut->repeat.kind != REPEAT_NONE)
	{
		repeat_cut(cut, repo_src, repo_dst, dj_, tess_node);
		return;
	}

	segment_info* seg_cut = _view.segment(cut->segment); assert(seg_cut);

	/*file_utils::svg_doc()
		.add(arr)
		.add(src->curve(), svg::Color::Blue)
		.add(dst->curve(), svg::Color::Green)
		.add(seg_cut->src, 0.2, svg::Color::Red)
		.add(seg_cut->tgt, 0.2, svg::Color::Red)
		.save("c:\\dev\\tess_before0.svg");*/
	//geom_utils::print(src, "src");
	//geom_utils::print(dst, "dst");

	//validate cuts
	if (src != edge2())
		src = find_correct_edge(src, seg_cut->orig_src);
	if (dst != edge2())
		dst = find_correct_edge(dst, seg_cut->orig_tgt);

	tess_node->seg = seg_cut->segment();
	tess_node->left_face_label = cut->faceLeft;
	tess_node->right_face_label = cut->faceRight;
	tess_node->left_edge_label = cut->cutLeft;
	tess_node->right_edge_label = cut->cutRight;

	tess_node->source_left_label = cut->sourceLeft;
	tess_node->source_left_label_opp = cut->sourceLeftOpp;
	tess_node->source_right_label = cut->sourceRight;
	tess_node->source_right_label_opp = cut->sourceRightOpp;
	tess_node->target_left_label = cut->targetLeft;
	tess_node->target_left_label_opp = cut->targetLeftOpp;
	tess_node->target_right_label = cut->targetRight;
	tess_node->target_right_label_opp = cut->targetRightOpp;

	dj_.add("src", src);
	dj_.add("dst", dst);
	//geom_utils::print(src, "src");
	//geom_utils::print(dst, "dst");

	//dj_.set_current();

	//prepare recursion
	edge_map left_edges = edges;
	edge_map right_edges = edges;

	int left_side_collapsed_count = 0;
	int right_side_collapsed_count = 0;
	edge2 he_source_left, he_source_right;
	face2 f;
	face2_data fd;

	// cut source
	vertex2 vs;

	if (src == edge2()) // source is collapsed
	{
		he_source_left = edge2();
		he_source_right = edge2();
		vs = edges[cut->src].vertex;
	}
	else
	{
		segment2 source_left(_view.segment(cut->segment.cut_result(SOURCE_LEFT))->segment());
		segment2 source_right(_view.segment(cut->segment.cut_result(SOURCE_RIGHT))->segment());
		point2 ps = seg_cut->orig_src;

		if (source_left.source() == ps)
			source_left = source_left.opposite();
		if (source_right.target() == ps)
			source_right = source_right.opposite();

		f = src->face();
		fd = src->face()->data();
		edge2_data ed = src->data();
		edge2_data oed = src->twin()->data();

		// snap source point to edge borders
		if (collapsable(src->source()->point(), ps))
		{
			he_source_left = source_left.is_degenerate() ? edge2() : prev_colinear(src);
		}
		else if (collapsable(src->target()->point(), ps))
		{
			he_source_left = src;
		}
		else
		{
			he_source_left = _ctx->fctx()->split_edge(_arr, src, ps);
		}

		he_source_right = source_right.is_degenerate() ? edge2() : (he_source_left == edge2() ? src : next_colinear(he_source_left));
		vs = he_source_left != edge2() ? he_source_left->target() : he_source_right->source();

		if (vs->point() != ps)
		{	// adjust source point and segments
			ps = vs->point();
			source_left = segment2(source_left.source(), ps);
			source_right = segment2(ps, source_right.target());
		}

		if (he_source_left == edge2())
			left_side_collapsed_count++;
		else
		{	// update data
			//he_source_left->data() = ed;
			//he_source_left->twin()->data() = oed;

			apply_edge_labels(he_source_left, cut->sourceLeft, cut->sourceLeftOpp, source_left);
			dj_.add("sourceLeft", he_source_left);
		}

		if (he_source_right == edge2())
			right_side_collapsed_count++;
		else
		{	// update data
			//he_source_right->data() = ed;
			//he_source_right->twin()->data() = oed;

			apply_edge_labels(he_source_right, cut->sourceRight, cut->sourceRightOpp, source_right);
			dj_.add("sourceRight", he_source_right);
		}
	}

	//update repo
	add_repo(left_edges, cut->src, he_source_left, vs);
	add_repo(left_edges, cut->segment.cut_result(SOURCE_LEFT), he_source_left, vs);
	/*if (left_edges.find(cut->src) == left_edges.end())
		left_edges[cut->src].edge = he_source_left;
	if (left_edges.find(cut->segment + SOURCE_LEFT) == left_edges.end())
		left_edges[cut->segment + SOURCE_LEFT].edge = he_source_left;
	if (he_source_left == edge2())
	{
		left_edges[cut->src].vertex = vs;
		left_edges[cut->segment + SOURCE_LEFT].vertex = vs;
	}*/

	add_repo(right_edges, cut->src, he_source_right, vs);
	add_repo(right_edges, cut->segment.cut_result(SOURCE_RIGHT), he_source_right, vs);
	/*right_edges[cut->src].edge = he_source_right;
	right_edges[cut->segment + SOURCE_RIGHT].edge = he_source_right;
	if (he_source_right == edge2())
	{
		right_edges[cut->src].vertex = vs;
		right_edges[cut->segment + SOURCE_RIGHT].vertex = vs;
	}*/

	// cut target
	vertex2 vd;
	edge2 he_target_left, he_target_right;

	if (dst == edge2()) // dest is collapsed
	{
		he_target_right = edge2();
		he_target_left = edge2();
		vd = edges[cut->dst].vertex;
	}
	else
	{
		segment2 target_left(_view.segment(cut->segment.cut_result(TARGET_LEFT))->segment());
		segment2 target_right(_view.segment(cut->segment.cut_result(TARGET_RIGHT))->segment());
		point2 pd = seg_cut->orig_tgt;

		// align segments with the edges actual orientation
		if (target_left.target() == pd)
			target_left = target_left.opposite();
		if (target_right.source() == pd)
			target_right = target_right.opposite();

		f = dst->face();
		fd = dst->face()->data();
		edge2_data ed = dst->data();
		edge2_data oed = dst->twin()->data();

		if (collapsable(dst->source()->point(), pd))
		{
			he_target_right = target_right.is_degenerate() ? edge2() : prev_colinear(dst);
		}
		else if (collapsable(dst->target()->point(), pd))
		{
			he_target_right = dst;
		}
		else
		{
			he_target_right = _ctx->fctx()->split_edge(_arr, dst, pd);
		}

		he_target_left = target_left.is_degenerate() ? edge2() : (he_target_right == edge2() ? dst : next_colinear(he_target_right));
		if (he_target_right == edge2() && he_target_left == edge2())
			throw std::logic_error("internal error");

		vd = he_target_right != edge2() ? he_target_right->target() : he_target_left->source();

		if (vd->point() != pd)
		{	// adjust target point and segments
			pd = vd->point();
			target_left = segment2(pd, target_left.target());
			target_right = segment2(target_right.source(), pd);
		}

		//update data
		if (he_target_right == edge2())
			right_side_collapsed_count++;
		else
		{
			//he_target_right->data() = ed;
			//he_target_right->twin()->data() = oed;

			apply_edge_labels(he_target_right, cut->targetRight, cut->targetRightOpp, target_right);
			dj_.add("targetRight", he_target_right);
		}

		if (he_target_left == edge2())
			left_side_collapsed_count++;
		else
		{
			//he_target_left->data() = ed;
			//he_target_left->twin()->data() = oed;

			apply_edge_labels(he_target_left, cut->targetLeft, cut->targetLeftOpp, target_left);
			dj_.add("targetLeft", he_target_left);
		}
	}

	//update repo left
	add_repo(left_edges, cut->dst, he_target_left, vd);
	add_repo(left_edges, cut->segment.cut_result(TARGET_LEFT), he_target_left, vd);
	/*if (left_edges.find(cut->dst) == left_edges.end())
		left_edges[cut->dst].edge = he_target_left;
	//left_edges[cut->segment.cut_result(TARGET_LEFT)].edge = he_target_left;
	auto itL = left_edges.find(cut->segment.cut_result(TARGET_LEFT));
	if (itL == left_edges.end())
		left_edges[cut->segment.cut_result(TARGET_LEFT)].edge = he_target_left;
	if (he_target_left == edge2())
	{
		left_edges[cut->dst].vertex = vd;
		left_edges[cut->segment.cut_result(TARGET_LEFT)].vertex = vd;
	}*/

	//update repo right
	add_repo(right_edges, cut->dst, he_target_right, vd);
	add_repo(right_edges, cut->segment.cut_result(TARGET_RIGHT), he_target_right, vd);
	/*right_edges[cut->dst].edge = he_target_right;
	auto itR = right_edges.find(cut->segment.cut_result(TARGET_RIGHT));
	if (itR == right_edges.end())
		right_edges[cut->segment.cut_result(TARGET_RIGHT)].edge = he_target_right;
	if (he_target_right == edge2())
	{
		right_edges[cut->dst].vertex = vd;
		right_edges[cut->segment.cut_result(TARGET_RIGHT)].vertex = vd;
	}*/

	bool left_side_collapsed = left_side_collapsed_count == 2;
	bool right_side_collapsed = right_side_collapsed_count == 2;
	if (left_side_collapsed && right_side_collapsed)
		throw std::logic_error("cut collapsed in the tesselator"); //td: error

	bool left_side_fully_collapsed = false;
	bool right_side_fully_collapsed = false;
	edge2 result;
	segment2 cut_segment(vs->point(), vd->point());
	if (left_side_collapsed && !cut->control_points.enabled)
	{
		result = he_source_right->prev()->twin();

		if (cut_segment.has_on(result->target()->point()))
		{
			left_side_fully_collapsed = true;
			if (cut->cutRight >= 0)
				apply_edge_labels(result->twin(), cut->cutRight, -1, cut_segment);
		}
		else
			result = insert_cut(cut, cut_segment, curves_seg, f, vs, vd);
	}
	else if (right_side_collapsed && !cut->control_points.enabled)
	{
		result = he_source_left->next();

		if (cut_segment.has_on(result->target()->point()))
		{
			right_side_fully_collapsed = true;
			if (cut->cutLeft >= 0)
				apply_edge_labels(result, cut->cutLeft, -1, cut_segment);
		}
		else
			result = insert_cut(cut, cut_segment, curves_seg, f, vs, vd);
	}
	else
	{
		if (cut->control_points.enabled)
		{
			result = insert_bezier(curves_seg, f, vs, vd, cut)[0];
		}
		else
		{
			bool inserted;
			result = insert_at_vertices(vs, vd, inserted);

			if (!inserted)
			{
				auto side = classify_edges(segment2(vs->point(), vd->point()).supporting_line(), src, dst);
				if (side == SIDE_POSITIVE)
					right_side_fully_collapsed = true;
				else if (side == SIDE_NEGATIVE)
					left_side_fully_collapsed = true;
			}

			if (!left_side_fully_collapsed && cut->cutLeft >= 0)
				result->data().label = cut->cutLeft;

			if (!right_side_fully_collapsed && cut->cutRight >= 0)
				result->twin()->data().label = cut->cutRight;
		}
	}

	// apply face labels
	if (!right_side_fully_collapsed)
	{
		result->twin()->face()->data() = fd;
		if (cut->faceRight >= 0)
			result->twin()->face()->data().label = cut->faceRight;
	}

	if (!left_side_fully_collapsed)
	{
		result->face()->data() = fd;
		if (cut->faceLeft >= 0)
			result->face()->data().label = cut->faceLeft;
	}

	if (result == edge2())
		std::cout << "bad segment result\n";
	dj_.add("cut", result);

	//recurse
	if (left_edges.find(cut->segment) == left_edges.end())
		left_edges[cut->segment].edge = result;
	if (right_edges.find(cut->segment) == right_edges.end())
		right_edges[cut->segment].edge = result->twin();

	//io_utils<GeometryKernel, geometry::arrangement2, geometry::polyhedron3>::save(arr, "c:\\dev\\repeat.svg");
	if (cut->left && !left_side_fully_collapsed)
    {
        debug_json tdj = dj_.begin("cut_left");
		tess_node->left = tessellator_node_ref(new tessellator_node());
		do_cut(cut->left, left_edges, tdj, curves_seg, tess_node->left);
    }
	//else
	//	result_faces.push_back(result->face());

	if (cut->right && !right_side_fully_collapsed)
    {
        debug_json tdj = dj_.begin("cut_right");
		tess_node->right = tessellator_node_ref(new tessellator_node());
		do_cut(cut->right, right_edges, tdj, curves_seg, tess_node->right);
    }
	//else
	//	result_faces.push_back(result->twin()->face());
}
