
#include "stdafx.h"
#include "partition_solver.h"
#include "partition_errors.h"
#include "phoenix/partition/ported/null_diagnostics.hpp"

DEFAULT_CGAL_TYPES()
DEFAULT_ARRANGEMENT_TYPES()
DEFAULT_UTILS()

int encode_key(int instruction, int error_id)
{
	return instruction | (error_id << 16);
}

void decode_key(int error_key, int& instruction, int& error_id)
{
	instruction = error_key & 0xffff;
	error_id = error_key >> 16;
}

// evaluator to discard views that have base segments with incorrect orientation
struct select_edge_evaluator
{
	bool is_left;
	cut_segment_id segment1, segment2;

	select_edge_evaluator(bool is_left_, cut_segment_id segment1_, cut_segment_id segment2_) :
		is_left(is_left_),
		segment1(segment1_),
		segment2(segment2_)
	{
	}

	bool operator ()(partition_view& view, const partition_model& model)
	{
		partition_view_list result;
		bool has_seg1 = !segment1.empty() && view.has_segment(segment1);
		bool has_seg2 = !segment2.empty() && view.has_segment(segment2);
		if ((has_seg1 && has_seg2) || (has_seg1 && segment2.empty()) || (has_seg2 && segment1.empty()))
			return true;

		// get the list of candidates for _segment1 and _segment2
		if (!has_seg1 && (has_seg2 || segment2.empty())) {
			model.branch(view, segment1, result, true);
		}
		else if (!has_seg2 && (has_seg1 || segment1.empty())) {
			model.branch(view, segment2, result, true);
		}
		else {
			model.branch(view, segment1, segment2, result, true);
		}

		if (/*false &&*/ result.size() > 0 && !view.cut_middle_line.identical(line2()))
		{
			// tests the results against the cut middle line
			line2 line = is_left ? view.cut_middle_line : view.cut_middle_line.opposite();

			//std::remove_if(result.begin(), result.end(), );
			for (int i = 0; i< result.size(); i++)
			{
				auto& result_view = result[i];
				if (segment1.valid())
				{
					auto seg_info = result_view.segment(segment1);
					if (line.has_on_negative_side(seg_info->src) || line.has_on_negative_side(seg_info->tgt))
					{	// discard the view
						result.erase(result.begin() + i);
						i--;
						continue;
					}
				}

				if (segment2.valid())
				{
					auto seg_info = result_view.segment(segment2);
					if (line.has_on_negative_side(seg_info->src) || line.has_on_negative_side(seg_info->tgt))
					{	// discard the view
						result.erase(result.begin() + i);
						i--;
						continue;
					}
				}
			}
		}

		//if (result.size() == 0)
		//	std::cout << "view rejected by evaluator" << std::endl;
		// return true if there is at least one view correct
		return result.size() > 0;
	}
};

struct apply_cut
{
	partition_cut* cut;

	apply_cut(partition_cut* cut_) : cut(cut_) {}

	void operator ()(partition_view& view, const partition_model& model, partition_view_list& result)
	{
		model.inst_dj().add_type("apply_cut");
		cut_segment_id cut_segment = model.cut_segment(cut);
		if (view.has_segment(cut_segment))
		{
			view.reset();
			result.push_back(view);
			return; //someone has created the cut already
		}

		view.cut_index++;
		model.branch(view, cut, result);

		//auto kk = model.inst_dj().begin_array("views_reseted");
		for (auto& it : result)
		{
			it.reset();
			//kk.add_item(it, &model);
		}
	}
};

void segment_info::print(const std::string& message)
{
#ifdef DEBUG
	std::cout << message << " [segment_info] = " << std::endl;
	geom_utils::print(src, "\tsource: ");
	geom_utils::print(tgt, "\ttarget: ");
	geom_utils::print(orig_src, "\torg_source: ");
	geom_utils::print(orig_tgt, "\torg_target: ");
#endif
}

//partition_notify
void partition_notify::add_error(int instruction_idx, int error_id)
{
	bool less_important_error = false;
	for (auto it_error : _errors)
	{
		int inst_idx_, error_id_;
		decode_key(it_error.first, inst_idx_, error_id_);
		if (inst_idx_ > instruction_idx)
			return; // there is an error more important, dont add anything
		if (inst_idx_ < instruction_idx)
			less_important_error = true;
	}

	if (less_important_error)
		_errors.clear(); // remove old errors

	auto error_key = encode_key(instruction_idx, error_id);
	_errors[error_key] = 1;

	/*// old way
	auto it = _errors.find(error_key);
	if (it != _errors.end())
		it->second++;
	else
		_errors[error_key] = 1;*/
}

void partition_notify::add_error(partition_notify_ref notify_)
{
	for (auto it_error : notify_->_errors)
	{
		auto it = _errors.find(it_error.first);
		if (it != _errors.end())
			it->second += it_error.second;
		else
			_errors[it_error.first] = it_error.second;
	}
}

const std::map<int, int>& partition_notify::errors() const
{
	return _errors;
}

property_tree& partition_notify::debug_data()
{
	return _debug_data;
}

//partition_plan
void partition_plan::build(const partition_model& model, partition_cut* c/*, constraint_map& cache*/)
{
	if (!c)
		return;

	cuts++;

	select_edges se(c);
	partition_plan_brancher ppb_edges(se);
	auto iidx = add_instruction(c->id, ppb_edges, PRIORITY_SELECT_EDGES);

	// add select_edge evaluators
	auto cut = c;
	auto base_segment1 = model.is_base_segment(se.src) ? se.src : cut_segment_id();
	auto base_segment2 = model.is_base_segment(se.tgt) ? se.tgt : cut_segment_id();
	if (!base_segment1.empty() || !base_segment2.empty())
	{
		while (cut->parent)
		{
			bool is_left = cut->parent->left == cut;
			//std::cout << c->id << " : add_evaluator" << std::endl;
			add_evaluator(cut->parent->id, select_edge_evaluator(is_left, base_segment1, base_segment2), PRIORITY_SELECT_EDGES);
			cut = cut->parent;
		}
	}

	property_tree error_data;
	if (model.select_edge_error(c->src, error_data))
	{
		register_error(iidx, c->src.value, error_data);
		error_data.clear();
	}

	if (model.select_edge_error(c->dst, error_data))
		register_error(iidx, c->dst.value, error_data);

	/*constraint_map::iterator it = cache.find(c->id);
	if (it != cache.end())
	{
	constraint_list& constraints = it->second;
	for (constraint_list::iterator vit = constraints.begin(); vit != constraints.end(); vit++)
	{
	auto before_instruction = result.instruction_count();
	(*vit)->build(*this, result);
	auto after_instruction = result.instruction_count();

	auto cerr_it = _constraints_errors.find((*vit)->id());
	if (cerr_it != _constraints_errors.end())
	{
	std::cout << "asigning constraint error to instructions: " << before_instruction << " ... " << (after_instruction-1) << std::endl;
	for (int iidx = before_instruction; iidx < after_instruction; iidx++)
	{
	auto err_it = _errors.find(iidx);
	if (err_it == _errors.end())
	register_error(iidx, 0, cerr_it->second);
	}
	}
	}
	}*/

	apply_cut ac(c);
	partition_plan_brancher ppb_cut(ac);
	auto iid = add_instruction(c->id, ppb_cut, PRIORITY_APPLY_CUT);
	error_data.clear();
	model.cut_error(*c, error_data);
	register_error(iid, 0, error_data);

	//recurse
	if (c->right)
		build(model, c->right/*, cache, plan*/);

	if (c->left)
		build(model, c->left/*, cache, plan*/);
}

SolverPlanResult partition_plan::advance(const partition_model& model, partition_view& view, partition_view_list& branches) const
{
	auto dj_ = dj(false);// model.inst_dj();
	//auto dj_.add("view", view);
	if (view.instruction_index >= instructions.size())
	{
		dj_.add("cuts", model.cut_root(), partition_model_view(&model, &view));
		return SUCCEED;
	}

	dj_.add("cut_index", view.cut_index);
	int instruction_priority;
	auto instruction = get(view.instruction_index++, &instruction_priority);
	auto brancher = boost::get<partition_plan_brancher>(instruction);
	if (brancher)
	{
		auto evaluators = get_evaluators(view.cut_index + 1, instruction_priority);
		partition_view_list proposals;
		(*brancher)(view, model, proposals);

		if (proposals.empty())
		{
			dj_.add("result", "FAIL");
			return FAIL;
		}

		size_t old_branch_count = branches.size();

		if (evaluators.first != evaluators.second)
		{
			for (auto proposal : proposals)
			{
				bool valid = true;
				for (auto it = evaluators.first; valid && it != evaluators.second; it++)
				{
					if (!it->second(proposal, model))
						valid = false;
				}

				if (valid)
					branches.push_back(proposal);
			}
		}
		else
			branches.insert(branches.end(), proposals.begin(), proposals.end());

		size_t new_branches = branches.size() - old_branch_count;
		if (new_branches == 0) {
			//std::cout << "evaluators rejected all views (" << proposals.size() << ")\n";
			dj_.text("evaluators rejected all views");
		}

		//std::cout << "proposals (" << proposals.size() << ") accepted (" << new_branches << ")\n";
		dj_.add("result", new_branches == 0 ? "FAIL" : "BRANCH");
		return new_branches == 0 ? FAIL : BRANCH;
	}

	const partition_plan_worker* worker = boost::get<const partition_plan_worker>(instruction);
	if (worker)
	{
		bool result = (*worker)(view, model);
		dj_.add("result", result ? "CONTINUE" : "FAIL");
		return result ? CONTINUE : FAIL;
	}

	assert(false);
	return FAIL;
}

bool partition_plan::get_error(int inst_index, int error_id, property_tree& data) const
{
	size_t current = 0;
	for (auto it = instructions.cbegin(); it != instructions.cend(); it++)
	{
		if (current == inst_index)
		{
			auto ite = it->second.errors.find(error_id);
			if (ite != it->second.errors.end())
				data = ite->second;
			else
			{
				data.add("id", PARTITION_ERROR_INVALID_CUT);
				data.add("message", "cut failed");
			}

			return true;
		}

		current++;
	}

	assert(false);
	return false;
}

//partition_model
partition_model::partition_model(partition_cut* root, int base_segments) :
	_root(root),
	_base_segments(base_segments),
	_efn(),
	_dj(NULL)
{
}

void partition_model::reset()
{
	_cuts.clear();
	_plan.reset();
}

void partition_model::fill_cuts_cache(partition_cut* cut)
{
	_cuts[cut->id] = cut;

	if (cut->right)
		fill_cuts_cache(cut->right);

	if (cut->left)
		fill_cuts_cache(cut->left);
}

partition_plan& partition_model::create_plan(vm::const_icontext_ref ctx)
{
	if (_plan.empty())
	{
		// fill the cut map
		if (_root && _cuts.empty())
			fill_cuts_cache(_root);

		constraint_map  constraint_by_cut;
		int instruction = 0;
		for (constraint_list::iterator it = _constraints.begin(); it != _constraints.end(); it++)
		{
			partition_solver_constraint_ref constraint = *it;
			constraint->build(ctx, *this, _plan);
		}

		_plan.build(*this, _root/*, constraint_by_cut, plan*/);
		//assert(!_plan.empty());
	}

	return _plan;
}

//engine branchers
void select_edges::operator ()(partition_view& view, const partition_model& model, partition_view_list& result)
{
	auto dj_ = model.inst_dj();
	dj_.add_type("select_edges");

	bool has_src = view.has_segment(src);
	bool has_tgt = view.has_segment(tgt);
	//bool branch_src = src.valid() && !view.has_segment(src);
	//bool branch_tgt = tgt.valid() && !view.has_segment(tgt);

	auto size_prev = result.size();

	if ((has_src && has_tgt) || (has_src && tgt.empty()) || (has_tgt && src.empty()))
	{	// the original view already has all the needed segments
		result.push_back(view);

		if (randomize_source && randomize_target)
		{
			model.branch(view, src, result, true); // randomize source with the original target
			model.branch(view, tgt, result, true); // randomize target with the original source

			model.branch(view, src, tgt, result, true); // randomize source and target
		}
		else if (randomize_source || randomize_target) // randomize source or target
			model.branch(view, randomize_source ? src : cut_segment_id(), randomize_target ? tgt : cut_segment_id(), result, true);
	}
	else
	{	// has to branch source or target
		if (has_src && !has_tgt)
		{
			//model.branch(view, tgt, result, check_orientation);
			model.branch(view, randomize_source ? src : cut_segment_id(), tgt, result, check_orientation || randomize_source);

			if (result.size() == 0)
				view.notify_error(tgt);
		}
		else if (!has_src && has_tgt)
		{
			//model.branch(view, src, result, check_orientation);
			model.branch(view, src, randomize_target ? tgt : cut_segment_id(), result, check_orientation || randomize_target);

			if (result.size() == 0)
				view.notify_error(src);
		}
		else// if (!has_src && !has_tgt)
		{
			auto res = model.branch(view, src, tgt, result, check_orientation);
			if (result.size() == 0)
				view.notify_error(res == BRANCH_FAIL_FIRST ? src : tgt);
		}
	}

	/*if (!branch_src && !branch_tgt)
		result.push_back(view);

	if ((branch_src && !branch_tgt) || randomize_source) // branch source
	{
		model.branch(view, src, result, check_orientation || randomize_source);
		if (result.size() == 0)
			view.notify_error(src);
	}

	if ((!branch_src && branch_tgt) || randomize_target) // branch target
	{
		model.branch(view, tgt, result, check_orientation || randomize_target);
		if (result.size() == 0)
			view.notify_error(tgt);
	}

	if ((branch_src && branch_tgt) || (randomize_source && randomize_target)) // branch source and target
	{
		auto res = model.branch(view, src, tgt, result, check_orientation || (randomize_source && randomize_target));
		if (result.size() == 0)
			view.notify_error(res == BRANCH_FAIL_FIRST ? src : tgt);
	}*/


	/*auto delta = result.size() - size_prev;
	if (delta > 0)
		std::cout << "extra bases for randomized segment: " << delta << "\n";
*/		

	if (result.size() == 0)
	{
		dj_.add("src", model.segment_name(src));
		if (tgt.valid())
			dj_.add("tgt", model.segment_name(tgt));
		dj_.error("segment(s) not found");
	}
	else
	{
		if (dj_.enabled())
		{	// sort for debug purposes only
			struct compare_views
			{
				bool operator() (const partition_view& view1, const partition_view& view2)
				{
					return view1.quality > view2.quality;
				}
			};

			std::stable_sort(result.begin(), result.end(), compare_views());

			auto dj_solutions = dj_.begin_array("solutions");
			for (auto it : result)
			{
				auto dj_solution = dj_solutions.begin();
				dj_solution.add("quality", it.quality * 1000);

				auto seg_info = it.segment(src);
				if (src.valid())
					dj_solution.add("src", seg_info, &model);

				if (tgt.valid())
				{
					seg_info = it.segment(tgt);
					dj_solution.add("tgt", seg_info, &model);
				}
			}
		}
	}
}

partition_cut* partition_model::find_cut_recursive(partition_cut* c, int cut_id) const
{
	if (c->id == cut_id)
		return c;

	//recurse
	if (c->right)
	{
		auto result = find_cut_recursive(c->right, cut_id);
		if (result)
			return result;
	}

	if (c->left)
		return find_cut_recursive(c->left, cut_id);

	return nullptr;
}


int partition_model::search(edge2_list* edges, int from, int to) const
{
	assert(false); //td:
	return -1;
}

/*bool partition_model::segment_used(partition_view& view, cut_segment_id const edge2& he)
{
	for (auto it : view.segments)
	{
		if (it.second.hedge == he)
			return true;
	}

	return false;
}*/

void partition_model::add_constraint(partition_solver_constraint_ref constraint)
{
	_constraints.push_back(constraint);
}

void partition_model::add_segment_info(cut_segment_id segment, const std::string& name, const std::string& display)
{
	property_tree info;
	info.add("message", display);

	auto it = _segment_info.find(segment);
	if (it == _segment_info.end())
	{
		property_tree data;
		data.add("id", segment.value);
		data.add("name", name);

		property_tree info_array;
		info_array.push_back(std::make_pair("", info));

		data.add_child("info", info_array);
		_segment_info[segment] = data;
	}
	else
	{
		auto&& info_array = it->second.find("info");
		info_array->second.push_back(std::make_pair("", info));
	}
}

segment_info* partition_model::branch_simple(partition_view& view, cut_segment_id segment, bool check_orientation) const
{
	if (!segment.valid())
		return nullptr;

	//branch possibilities from the repo.
	auto candidates = view.repo->get(segment);
	if (candidates == nullptr || candidates->size() != 1)
		return nullptr;

	auto it = candidates->begin();
	if (view.has_edge(*it) || (check_orientation && !is_candidate(view, it->seg)))
		return nullptr;

	//segment_info seg_info(segment, (*it)->source()->point(), (*it)->target()->point(), *it);
	segment_info seg_info(segment, *it);

	// ignore filters?

	return view.add_segment(seg_info);

	/*for (edge2_list::iterator it = candidates->begin(); it != candidates->end(); it++)
	{
	}*/
}

void partition_model::branch(partition_view& view, cut_segment_id segment, partition_view_list& result, bool check_orientation) const
{
	if (segment.empty())
		return;

	std::vector<partition_model_filter*> filter_errors;
	//branch possibilities from the repo.
	//the problem here is that the segments to be chosen
	//on the correct side of the last cut.
	bool is_base = is_base_segment(segment);
	const repo_edge2_list* candidates;
	if (is_base)
		candidates = view.repo->get(segment);
	else
	{
		auto root = get_root_segment(segment);
		if (root.valid())
			candidates = view.repo->get(root);
		else // when the segment is derived from a cut (and not from a base)
			candidates = view.repo->all_edges();
	}

	bool ok = false;
	if (candidates != nullptr)
	{
		auto segment_filters = get_filter(segment);
		auto sf_nd = segment_filters.end();

		for (auto it = candidates->begin(); it != candidates->end(); it++)
		{
			if (!view.has_edge(*it) && (!check_orientation || is_candidate(view, it->seg)))
			{
				bool valid_seg = true;
				auto sf_it = segment_filters.begin();
				for (; sf_it != sf_nd && valid_seg; sf_it++)
				{
					auto seg2 = view.segment(sf_it->segment2);
					if (seg2)
					{
						valid_seg = (*sf_it)(view.ctx, *it, seg2->redge);
						/*if (!valid_seg)
						{
							if (std::find(filter_errors.begin(), filter_errors.end(), &(*sf_it)) == filter_errors.end())
								filter_errors.push_back(&(*sf_it));
						}*/
					}
				}

				if (valid_seg)
					result.push_back(view_for_segment(view, segment, *it));
			}
		}
	}
}

branch_return_type partition_model::branch(partition_view& view, cut_segment_id segment1, cut_segment_id segment2, partition_view_list& result, bool check_orientation) const
{
	// note: it only get here when segment1 and segment2 are base segments
	if (segment1.empty())
	{
		branch(view, segment2, result, check_orientation);
		return result.empty() ? BRANCH_FAIL_SECOND: BRANCH_OK;
	}

	if (segment2.empty())
	{
		branch(view, segment1, result, check_orientation);
		return result.empty() ? BRANCH_FAIL_FIRST: BRANCH_OK;
	}

	//here we need to do the same, but both segments are unknown
	//we need to use strategies to deal with the amount of possibilities
	//and use the evaluation of the generated views as well
	//i.e. if you want to generate 5 branches, make sure that the
	//ones you do are valid, or above certain threshold.

	partition_view_list s1, s2;
	branch(view, segment1, s1, check_orientation);
	if (s1.empty())
		return BRANCH_FAIL_FIRST;

	branch(view, segment2, s2, check_orientation);
	if (s2.empty())
		return BRANCH_FAIL_SECOND;

	auto filters = get_filter(segment1, segment2);
	auto cf_nd = filters.end();

	for (partition_view_list::iterator it1 = s1.begin(); it1 != s1.end(); it1++)
	{
		segment_info* s1i = it1->segment(segment1);
		for (partition_view_list::iterator it2 = s2.begin(); it2 != s2.end(); it2++)
		{
			segment_info* s2i = it2->segment(segment2);
			if (s1i->redge.he_start != edge2() && s1i->redge == s2i->redge)
				continue; //avoid same edge

			line2 line_1(s1i->src, s1i->tgt);
			if (line_1.has_on(s2i->src) && line_1.has_on(s2i->tgt))
				continue; //avoid colineal edges

			// check the proposed pair with the filters
			bool valid_cut = true;
			for (auto cf_it = filters.begin(); valid_cut && cf_it != cf_nd; cf_it++)
			{
				valid_cut = (*cf_it)(view.ctx, s1i->redge, s2i->redge);
			}

			if (valid_cut)
			{
				result.push_back(view_for_segments(view, *(it1->segment(segment1)), *(it2->segment(segment2))));
			}
		}
	}

	return result.empty() ? BRANCH_FAIL_BOTH : BRANCH_OK;
}

void partition_model::branch(partition_view& view, partition_cut* cut, partition_view_list& result) const
{
	//here we actually do the cutting of 2 existing segments
	//again, we could eval to choose good alternatives
	//inst_dj().add("view", view, this);
	//auto dj_ = inst_dj().begin("angle solver");

	if (view.angles.size() == 0)
	{
		//dj_.error("NO ANGLES AVAILABLES");
		return;
	}

	segment_info* seg_src = view.segment(cut->src);
	if (!seg_src) return;
	//source
	point2* p1 = &seg_src->src;
	point2* p2 = &seg_src->tgt;
	segment2 source = segment2(*p1, *p2);
	//dj_.add("src", seg_src, this);

	segment_info* seg_dst = view.segment(cut->dst);
	if (!seg_dst) return;
	//target
	point2* p3 = &seg_dst->src;
	point2* p4 = &seg_dst->tgt;
	segment2 target = segment2(*p3, *p4);
	//dj_.add("dst", seg_dst, this);

	//dj_.add("angles_restriction", view.angles);

	//generate variations
	double dSource = geom_utils::distance(*p1, *p2);
	double dTarget = geom_utils::distance(*p3, *p4);
	vec2 ds = *p2 - *p1; geom_utils::normalize(ds);
	vec2 dt = *p4 - *p3; geom_utils::normalize(dt);

	int variations = 4; //td: !!

	//auto dj_solutions = dj_.begin_array("solutions");
	dj dj_solutions(false);
	bool angle_restriction = view.is_angle_restricted();

	bool srcCollapsed = dSource <= 0.01;//*p1 == *p2;
	bool targetCollapsed = dTarget <= 0.01;//*p3 == *p4;
	bool angleCollapsed = angle_restriction && view.angles.size() == 1 && view.angles[0].min_angle == view.angles[0].max_angle;
	if ((int)srcCollapsed + (int)targetCollapsed + (int)angleCollapsed >= 2)
	{
		variations = 1;
		//dj_.text("ONLY ONE SOLUTION");
	}

	/*double left_children = cut->left_children_count();
	double right_children = cut->right_children_count();
	double total_children = left_children + right_children;*/
	auto angle_ranges = view.angles;
	if (angle_restriction)
		view.rand->shuffle(angle_ranges);

	//static int kk = 0;
	//std::cout << "kk = " << kk << "\n";
	//kk++;
	// source and target line with correct orientations (only usable when src or dst are base segments)
	line2 oriented_source_line(0, 0, 0), oriented_target_line(0, 0, 0);

	if (seg_src->redge.has_edge())
		oriented_source_line = seg_src->redge.seg.supporting_line();// line2(seg_src->redge.seg() hedge->source()->point(), seg_src->hedge->target()->point());
	if (seg_dst->redge.has_edge())
		oriented_target_line = seg_dst->redge.seg.supporting_line();// line2(seg_dst->hedge->source()->point(), seg_dst->hedge->target()->point());

	auto has_interceptions = [&view, &seg_src, &seg_dst, &source, &target](point2& cut_sourcept, point2& cut_targetpt)
	{
		if (!view.repo->is_face_concave())
			return false;
		//std::cout << "checking intersection\n";

		auto all_edges = view.repo->all_edges();
		segment2 cut_segment(cut_sourcept, cut_targetpt);
		//geom_utils::print(source, "source");
		//geom_utils::print(target, "target");
		//geom_utils::print(cut_segment, "cut_segment");

		for (auto& edge : *all_edges)
		{
			//geom_utils::print(edge.seg, "edge");
			if (edge  == seg_src->redge || edge == seg_dst->redge) // dont check against source or target
			{
				//std::cout << "dont check against source or target\n";
				continue;
			}

			if (edge.seg.source() == cut_sourcept || edge.seg.target() == cut_sourcept || edge.seg.source() == cut_targetpt || edge.seg.target() == cut_targetpt)
			{
				//std::cout << "on borders\n";
				continue;
			}

			if (edge.seg.has_on(cut_sourcept) || edge.seg.has_on(cut_targetpt))
			{
				//std::cout << "on segment\n";
				continue;
			}

			CGAL::Object ipoint = CGAL::intersection(cut_segment, edge.seg);
			const point2* pp = CGAL::object_cast<point2>(&ipoint);
			if (pp)
			{
				//std::cout << "intersection found\n";
				/*geom_utils::print(source, "source");
				geom_utils::print(target, "target");
				geom_utils::print(cut_segment, "cut_segment");
				geom_utils::print(edge.seg, "edge.seg");
				std::cout << *pp << "\n";*/
				return true;
			}
		}

		//std::cout << "segment found\n";
		return false;
	};

	auto is_valid_solution = [&oriented_source_line, &oriented_target_line, &source, &target](point2& cut_sourcept, point2& cut_targetpt)
	{
		//if ((!source.is_degenerate() && source.supporting_line().has_on(targetpt)) ||
		//	(!target.is_degenerate() && target.supporting_line().has_on(sourcept)))
		// avoid solutions close to the source or target line
		if ((!source.is_degenerate() && CGAL::squared_distance(source.supporting_line(), cut_targetpt) < 1e-8) ||
			(!target.is_degenerate() && CGAL::squared_distance(target.supporting_line(), cut_sourcept) < 1e-8))
			return false;

		// avoid solution on the back of source and target segments
		//geom_utils::print(source, "source");
		//geom_utils::print(seg_dst->segment(), "target");
		//geom_utils::print(cut_sourcept, "cut_sourcept");
		
		if (!oriented_target_line.is_degenerate() && oriented_target_line.has_on_negative_side(cut_sourcept))
			return false;
		if (!oriented_source_line.is_degenerate() && oriented_source_line.has_on_negative_side(cut_targetpt))
			return false;

		return true;
	};

	for (int i = 0; i < variations; i++)
	{
		bool solution_found = true;
		auto dj_solution = dj_solutions.begin();
		point2 cut_source_pt = *p1 + ds*(view.rand->random()*dSource);
		point2 cut_target_pt = *p3 + dt*(view.rand->random()*dTarget);
		// variant to avoid extreme points
		/*double margin = 0.05;
		point2 rs = *p1 + ds*((margin + _ctx->random()*(1.0-2*margin))*dSource);
		point2 rt = *p3 + dt*((margin + _ctx->random()*(1.0-2*margin))*dTarget);*/

		//angle_range range;
		if (angle_restriction)
		{
			solution_found = false;
			// choose a random angle range
			for (auto& range : angle_ranges)
			{
				point2 solution = cut_target_pt;
				if (angle_solution(view.rand, cut_source_pt, range.min_angle, range.max_angle, target, solution) &&
					solution != source.source() && solution != source.target() &&
					is_valid_solution(cut_source_pt, solution))	// avoid degenerated solutions
				{
					if (has_interceptions(cut_source_pt, solution))
						continue;

					cut_target_pt = solution; // ok
					solution_found = true;
					//break;
				}
				else
				{
					solution = cut_source_pt;
					if (angle_solution(view.rand, cut_target_pt, range.min_angle + M_PI, range.max_angle + M_PI, source, solution) &&
						solution != target.source() && solution != target.target() &&
						is_valid_solution(solution, cut_target_pt))	// avoid degenerated solutions
					{
						if (has_interceptions(solution, cut_target_pt))
							continue;

						cut_source_pt = solution; // ok
						solution_found = true;
						//break;
					}
					else
					{	// solution failed
						dj_solution.add("source point", cut_source_pt);
						dj_solution.add("target point", cut_target_pt);
						dj_solution.add("result", "FAIL");

						continue; // keep searching in another angle range
					}
				}

				if (solution_found)
					break;
			}
		}
		else
			solution_found = is_valid_solution(cut_source_pt, cut_target_pt) && !has_interceptions(cut_source_pt, cut_target_pt);
			
		if (!solution_found)
			continue;

		dj_solution.add("source point", cut_source_pt);
		dj_solution.add("target point", cut_target_pt);
		dj_solution.add("result", "SUCCESS");

		double quality = 0;
		/*if (total_children > 0) // if not children, balance does not matter
		{
			line2 cut_line(rs, rt);
			double min, max;
			point2* points[4] = { p1, p2, p3, p4 };
			for (int i = 0; i < 4; i++)
			{
				double v = geom_utils::line_eval(cut_line, *points[i]);
				if (i == 0)
					min = max = v;
				else
				{
					if (v < min) min = v;
					if (v > max) max = v;
				}
			}

			// balance meaning: -1: total right, 1: total left, 0: perfect balance
			double len = max - min;
			if (len > 0)
			{
				double points_balance = (min + max) / len;
				double children_balance = (left_children - right_children) / total_children;
				quality = 1 - fabs(points_balance - children_balance) / 2;
			}
		}*/

		result.push_back(view_for_cut(view, cut, cut_source_pt, cut_target_pt, quality, dj_solution));
	}
}

// p: the fixed point of the solution
// segment: the segment in front of p where the other point of the solution is to be found
// solution: the proposed solution (a point in 'segment'), return the real solution
// return true if the solution is found, false otherwise
bool partition_model::angle_solution(randomizer_ref rand, point2& p, double min_angle, double max_angle, segment2& segment, point2& solution) const
{
	/* not needed any more?
	if (min_angle > max_angle)
	{
		auto temp = min_angle;
		min_angle = max_angle;
		max_angle = temp;
		//assert(false); //shouldn't happen
		//return false;
	}*/

	if (fabs(max_angle - min_angle) <= 1e-5)
	{	// special case: the cone collapsed to a ray, this is the case of parallel and perpendicular conditions,
		// the case of the opposite angle is automatically handled (since we intersect a line instead of a ray).
		// this is handled separately for its simplicity and eficiency
		auto dir = geom_utils::rotate_unit(min_angle);
		line2 line(p, dir);
		if (segment.is_degenerate())
		{	// segment is a point
			bool result = CGAL::squared_distance(line, segment.source()) < 1e-6;
			if (result)
				solution = segment.source();
			return result;
		}
		else
		{
			// first check the distance to the line of the extremes of the segment since intersection may fail
			// or give imprecise results due to the non exact nature of the angles
			for (int i = 0; i < 2; i++)
			{
				point2 point = segment.point(i);
				if (CGAL::squared_distance(line, point) < 1e-6)
				{
					solution = point;
					return true;
				}
			}

			// intersect the line with the segment
			CGAL::Object ipoint;
			ipoint = CGAL::intersection(line, segment);
			const point2* pp = CGAL::object_cast<point2>(&ipoint);
			if (pp)
				solution = *pp;

			return pp != nullptr;
		}
	}

	auto p1 = segment.source();
	auto p2 = segment.target();
	auto min_dir = geom_utils::rotate_unit(min_angle);
	auto max_dir = geom_utils::rotate_unit(max_angle);

	//a cone from p, segments points in the cone are valid solutions
	ray2 min_ray(p, min_dir);
	ray2 max_ray(p, max_dir);

	for (int pass = 0; pass < 2; pass++) {

		if (pass == 1)
		{	// in the second pass, try with the opposite cone (assuming that "angle" and "angle + PI" are equivalent)
			min_ray = min_ray.opposite();
			max_ray = max_ray.opposite();
		}

		line2 min_ln = min_ray.supporting_line();
		line2 max_ln = max_ray.supporting_line().opposite();

		if (!min_ln.has_on_negative_side(solution) && !max_ln.has_on_negative_side(solution))
		{
			// "solution" is already in the cone, so its a valid solution
			return true;
		}

		// outside tests
		if (min_ln.has_on_negative_side(p1) && min_ln.has_on_negative_side(p2))
		{
			// p1,p2 is outside the valid cone but is considered valid if it is very close to he edge
			// (perhaps due to the inexact nature of angles)
			if (CGAL::squared_distance(p1, min_ln) < 1e-6)
			{
				solution = p1;
				return true;
			}

			if (CGAL::squared_distance(p2, min_ln) < 1e-6)
			{
				solution = p2;
				return true;
			}

			continue; // next pass
		}

		if (max_ln.has_on_negative_side(p1) && max_ln.has_on_negative_side(p2))
		{
			// p1,p2 is outside the valid cone but is considered valid if it is very close to he edge
			// (perhaps due to the inexact nature of angles)
			if (CGAL::squared_distance(p1, max_ln) < 1e-6)
			{
				solution = p1;
				return true;
			}

			if (CGAL::squared_distance(p2, max_ln) < 1e-6)
			{
				solution = p2;
				return true;
			}

			continue; // next pass
		}

		/*if (segment.is_degenerate())
		{
			solution = segment.source();
			return true;
		}*/

		// intersection is not guaranteed, segment may be fully o partially inside the cone
		CGAL::Object imin = CGAL::intersection(min_ray, segment);
		CGAL::Object imax = CGAL::intersection(max_ray, segment);
		const point2* pmin = CGAL::object_cast<point2>(&imin);
		const point2* pmax = CGAL::object_cast<point2>(&imax);

		if (pmin && pmax)
		{
			p1 = *pmin;
			p2 = *pmax;
		}
		else
		{
			bool p1_inside_cone = !min_ln.has_on_negative_side(p1) && !max_ln.has_on_negative_side(p1);
			bool p2_inside_cone = !min_ln.has_on_negative_side(p2) && !max_ln.has_on_negative_side(p2);

			if (!pmin && !pmax)
			{	// no intersection
				if (p1_inside_cone && p2_inside_cone)
					; // p1 and p2 are valid
				else
					continue; // the segment is outside the cone
			}
			else
			{	// only one intersection
				if (p1_inside_cone)
					p2 = pmin ? *pmin : *pmax;
				else
				{
					assert(p2_inside_cone);
					p1 = pmin ? *pmin : *pmax;
				}
			}
		}

		vec2 sdir(p1, p2); geom_utils::normalize(sdir);
		double len = CGAL::sqrt(CGAL::to_double(CGAL::squared_distance(p1, p2)));
		solution = p1 + sdir*len*rand->random();
		return true;
	}

	return false;
}

//the segment layout is now:
//user segments|cut, src left, src right, tgt left, tgt right| ...
cut_segment_id partition_model::cut_segment(partition_cut* cut) const
{
	return cut_segment_id(_base_segments + 5 * cut->id);
}

cut_segment_id partition_model::cut_segment(int cut_id) const
{
	return cut_segment_id(_base_segments + 5 * cut_id);
}

const partition_cut* partition_model::cut_root() const
{
	return _root;
}

const partition_cut* partition_model::get_cut(int cut_id) const
{
	auto it = _cuts.find(cut_id);
	return it == _cuts.end() ? nullptr : it->second;
}

const partition_cut* partition_model::get_cut_by_segment(cut_segment_id segment) const
{
	for (auto it = _cuts.begin(); it != _cuts.end(); it++)
	{
		if (it->second->segment == segment)
			return it->second;
	}

	return nullptr;
}

cut_segment_id partition_model::get_caused_segment(const partition_cut* c, cut_segment_id segment_id) const
{
	if (is_base_segment(segment_id) || is_cut(segment_id))
		return segment_id;

	repo_segment_id src_left(_base_segments + c->id + SOURCE_LEFT);
	repo_segment_id src_right(_base_segments + c->id + SOURCE_RIGHT);
	repo_segment_id tgt_left(_base_segments + c->id + TARGET_LEFT);
	repo_segment_id tgt_right(_base_segments + c->id + TARGET_RIGHT);

	if (src_left == segment_id || src_right == segment_id)
		return get_caused_segment(c->parent, c->src);
	if (tgt_left == segment_id || tgt_right == segment_id)
		return get_caused_segment(c->parent, c->dst);

	//recurse
	cut_segment_id segment_res;
	if (c->right)
		segment_res = get_caused_segment(c->right, segment_id);
	if (segment_res.empty() && c->left)
		segment_res = get_caused_segment(c->left, segment_id);

	return segment_res;
}

// 'cut' is optional, only used for optimization
cut_segment_id partition_model::get_parent_segment(cut_segment_id segment_id, const partition_cut* cut) const
{
	int cut_idx;
	cut_segment_type segment_type;
	if (is_cut_segment(segment_id, cut_idx, segment_type))
	{
		if (segment_type == CUT_SEGMENT)
			return cut_segment_id(); // cut segments have no parent
		else
		{
			if (!cut)
				cut = get_cut(cut_idx);
			return segment_type.is_source() ? cut->src : cut->dst;
		}
	}
	else
		return cut_segment_id(); // base segments have no parent
}

// return the ultimate parent of segment segment_id (a cut or a base segment)
cut_segment_id partition_model::get_root_segment(cut_segment_id segment_id) const
{
	int cut_idx;
	cut_segment_type segment_type;
	if (is_cut_segment(segment_id, cut_idx, segment_type))
	{
		if (segment_type == CUT_SEGMENT)
			return cut_segment_id(); // cut segments have no parent
		else
		{
			auto cut = get_cut(cut_idx);
			for (auto parent_segment = segment_id; true; segment_id = parent_segment, cut = cut->parent)
			{
				parent_segment = get_parent_segment(segment_id, cut);
				if (parent_segment.empty())
					break;
			}

			return segment_id;
		}
	}
	else
		return cut_segment_id(); // base segments have no parent

	/*for (int parent_segment = segment_id; parent_segment >= 0; segment_id = parent_segment)
		parent_segment = get_parent_segment(segment_id);*/

	return segment_id;
}

bool partition_model::is_cut_segment(cut_segment_id segment, int& cut_idx, cut_segment_type& segment_type) const
{
	int idx = segment.value - _base_segments;
	if (idx < 0)
	{
		cut_idx = -1;
		segment_type = BASE_SEGMENT;
		return false; // is base
	}

	cut_idx = idx / 5;
	segment_type.set(idx % 5);
	return true;
}

bool partition_model::is_cut(cut_segment_id segment) const
{
	int idx = segment.value - _base_segments;
	int segment_idx = idx % 5;
	if (idx < 0 || segment_idx != 0)
		return false;
	return true;
}

bool partition_model::is_base_segment(cut_segment_id segment) const
{
	return segment.value < _base_segments;
}

std::string partition_model::segment_name(cut_segment_id segment) const
{
	std::ostringstream name;
	int cut_idx;
	cut_segment_type segment_type;
	is_cut_segment(segment, cut_idx, segment_type);
	if (cut_idx < 0)
		name << "base_" << segment.value;
	else
	{
		const char* seg_names[] = { "_Cut", "_SourceL", "_SourceR", "_TargetL", "_TargetR" };
		name << "cut_" << cut_idx << seg_names[segment_type];
	}

	return name.str();
}

std::string partition_model::cut_name(int cut_id) const
{
	std::ostringstream name;
	name << "cut_" << cut_id;
	return name.str();
}

void partition_model::add_base_label(cut_segment_id id, int labelid)
{
	auto it = _base_label.find(id);
	if (it != _base_label.end())
		it->second.label = labelid;
	else
		_base_label[id] = base_label(labelid);

	//_base_label[id] = labelid;
}

const base_label_map& partition_model::get_base_labels() const
{
	return _base_label;
}

void partition_model::add_base_opp_label(cut_segment_id id, int labelid)
{
	auto it = _base_label.find(id);
	if (it != _base_label.end())
		it->second.opp_label = labelid;
	else
		_base_label[id] = base_label(-1, labelid);
}

int partition_model::get_segment_label(cut_segment_id id) const
{
	auto root = get_root_segment(id);
	if (is_base_segment(root))
	{
		auto it = _base_label.find(root);
		if (it != _base_label.end())
			return it->second.label;
	}

	return -1;
}

/*void partition_model::register_error(int instruction_id, int error_id, property_tree& data)
{
	auto key = encode_key(instruction_id, error_id);
	_errors[key] = data;

}*/

bool partition_model::select_edge_error(cut_segment_id segment, property_tree& data) const
{
	auto seg = _segment_info.find(segment);
	if (seg == _segment_info.end())
		return false;

	auto name = seg->second.get<std::string>("name");
	auto info = seg->second.get_child("info");

	data.add("id", PARTITION_ERROR_NO_MATCHING_SEGMENT);
	data.add("segment", name);

	property_tree conditions;
	BOOST_FOREACH(property_tree::value_type &v, info)
	{
		auto display = v.second.get_optional<std::string>("message");
		if (!display || display.value().empty())
			continue;

		conditions.push_back(std::make_pair("", v.second));
	}

	data.add_child("conditions", conditions);
	return true;
}

bool partition_model::cut_error(partition_cut& cut, property_tree& data) const
{
	std::string name;
	auto seg = _segment_info.find(cut.segment);
	if (seg != _segment_info.end())
		name = seg->second.get<std::string>("name");

	data.add("id", PARTITION_ERROR_INVALID_CUT);
	data.add("segment", cut.segment.value);
	data.add("message", "cut '" + name + "' failed");
	return true;
}

bool partition_model::get_error(int error_key, property_tree& data) const
{
	int inst_index, error_id;

	decode_key(error_key, inst_index, error_id);

	bool res = _plan.get_error(inst_index, error_id, data);
	return res;
}

partition_view partition_model::view_for_segment(partition_view& view, cut_segment_id segment, const repo_edge2& edge) const
{
	partition_view result(view.ctx, view.repo, view.rand);
	result.copy(view);
	segment_info seg_info(segment, edge);// he->source()->point(), he->target()->point(), he);

	result.add_segment(seg_info);
	return result;
}

partition_view partition_model::view_for_segment(partition_view& view, cut_segment_id segment, segment_info& info) const
{
	partition_view result(view.ctx, view.repo, view.rand);
	result.copy(view);
	segment_info seg_info(segment, info.src, info.tgt);

	result.add_segment(seg_info);
	return result;
}

partition_view partition_model::view_for_segments(partition_view& view, segment_info& s1, segment_info& s2) const
{
	partition_view result(view.ctx, view.repo, view.rand);
	result.copy(view);

	typedef CGAL::Triangle_2<GeometryKernel> triangle2;
	double total_area = 0;
	if (s1.src == s2.src || s1.tgt == s2.src)
	{
		triangle2 tri(s1.src, s1.tgt, s2.tgt);
		total_area = fabs(CGAL::to_double(tri.area()));
	}
	else if (s1.src == s2.tgt || s1.tgt == s2.tgt)
	{
		triangle2 tri(s1.src, s1.tgt, s2.src);
		total_area = fabs(CGAL::to_double(tri.area()));
	}
	else
	{
		line2 li(s1.src, vec2(s1.src, s1.tgt).perpendicular(CGAL::COUNTERCLOCKWISE));
		auto d1 = li.a()*s2.src.x() + li.b()*s2.src.y() + li.c();
		auto d2 = li.a()*s2.tgt.x() + li.b()*s2.tgt.y() + li.c();
		point2* p2_src = &s2.src;
		point2* p2_tgt = &s2.tgt;
		if (d1 < d2) // use s2.tgt
			std::swap(p2_src, p2_tgt);

		triangle2 tri1(s1.src, s1.tgt, *p2_src);
		triangle2 tri2(s1.tgt, *p2_src, *p2_tgt);
		total_area = fabs(CGAL::to_double(tri1.area())) + fabs(CGAL::to_double(tri2.area()));
	}

	const double max_length = 5000.0; // maximum asumed length of a partition input face
	result.quality = total_area / (max_length*max_length);
	if (result.quality > 1.0)
		result.quality = 1.0;

	/*point2 p1 = s1.src + vec2(s1.src, *p2_src) / 2;
	point2 p2 = s1.tgt + vec2(s1.tgt, *p2_tgt) / 2;
	double distance = CGAL::to_double(CGAL::squared_distance(p1, p2));
	const double max_distance = 10000;
	result.quality = (max_distance - distance) / max_distance;*/

	/*vec2 v1(s1.orig_src, s1.orig_tgt);
	vec2 v2(s2.orig_src, s2.orig_tgt);
	// angle is -180 ... 180, 0 is the best case
	double angle = geom_utils::angle_between(v1, v2, true) - M_PI;
	result.quality = 1 - fabs(angle / M_PI);*/

	result.cut_middle_line = line2(s1.src + vec2(s1.src, s1.tgt) / 2, s2.src + vec2(s2.src, s2.tgt) / 2);
	result.add_segment(s1);
	result.add_segment(s2);
	return result;
}

/*point2& point(partition_view& v, int cut_id, bool is_source_segment, bool is_source_point)
{
	assert(false); // td !!!
	return point2(0, 0);
}

void point(partition_view& v, int cut_id, bool is_source_segment, bool is_source_point, const point2 value)
{
	assert(false); // td !!!
}

point2& original_point(partition_view& v, int cut_id, bool is_source_segment, bool is_source_point)
{
	assert(false); // td !!!
	return point2(0, 0);
}*/

partition_view partition_model::view_for_cut(partition_view& view, partition_cut* cut, point2& sp, point2& tp, double quality, dj& dj_) const
{
	partition_view result(view.ctx, view.repo, view.rand);
	result.copy(view);

	cut_segment_id cut_seg_id = cut_segment(cut);

	segment_info cut_info(cut_seg_id, sp, tp);
	segment_info* source = view.segment(cut->src);
	segment_info* target = view.segment(cut->dst);

	line2 cutLine(sp, tp);

	auto cutLine1 = cutLine;
	point2 source_left(source->orig_src), source_right(source->orig_tgt);
	if (cutLine1.has_on(source_left) && cutLine.has_on(source_right))
		cutLine1 = line2(sp, target->orig_src == tp ? target->orig_tgt : target->orig_src);
	if (cutLine1.has_on_negative_side(source_left) || cutLine1.has_on_positive_side(source_right))
		std::swap(source_left, source_right);

	point2 target_left(target->orig_src), target_right(target->orig_tgt);
	if (cutLine.has_on(target_left) && cutLine.has_on(target_right))
		cutLine = line2(source->orig_src == sp ? source->orig_tgt  : source->orig_src, tp);
	if (cutLine.has_on_negative_side(target_left) || cutLine.has_on_positive_side(target_right))
		std::swap(target_left, target_right);

	segment_info src_left(cut_seg_id.cut_result(SOURCE_LEFT), source_left, sp);
	segment_info src_right(cut_seg_id.cut_result(SOURCE_RIGHT), sp, source_right);
	segment_info tgt_left(cut_seg_id.cut_result(TARGET_LEFT), target_left, tp);
	segment_info tgt_right(cut_seg_id.cut_result(TARGET_RIGHT), tp, target_right);

	dj_.begin("cut result")
		.add("cut", cut_info, this)
		.add("cut_SourceL", src_left, this)
		.add("cut_SourceR", src_right, this)
		.add("cut_TargetL", tgt_left, this)
		.add("cut_TargetR", tgt_right, this);

	result.add_segment(cut_info);
	result.add_segment(src_left);
	result.add_segment(src_right);
	result.add_segment(tgt_left);
	result.add_segment(tgt_right);

	result.quality = quality;
	return result;
}

void partition_model::to_json(dj& dj_, const partition_view& view) const
{
	dj_.add_type("partition_model");
	dj_.add("quality", view.quality*1000);
	//dj_.add("repo", view.repo, _segment_info);
	dj_.add("constraints", _constraints.size());
	dj_.add("root", _root, partition_model_view(this, &view));
}

void partition_model::print(partition_view& view)
{
	#ifdef DEBUG
	std::cout << "===================== START VIEW ===================================" << std::endl;
	std::cout << "At cut " << view.cut_index << std::endl;
	std::cout << "At instruction " << view.instruction_index << std::endl;

	//the edged segments
	int sz = view.repo->size();
	for (int i = 0; i <= sz; i++)
	{
		print_segment(view, cut_segment_id(i));
	}

	//cut segments
	for (int i = 0; i < _plan.cuts; i++)
	{
		cut_segment_id cut_seg_id = cut_segment(i);
		if (!view.has_segment(cut_seg_id))
			continue;

		print_segment(view, cut_seg_id.cut_result(CUT_SEGMENT), i);
		print_segment(view, cut_seg_id.cut_result(SOURCE_LEFT), i);
		print_segment(view, cut_seg_id.cut_result(SOURCE_RIGHT), i);
		print_segment(view, cut_seg_id.cut_result(TARGET_LEFT), i);
		print_segment(view, cut_seg_id.cut_result(TARGET_RIGHT), i);
	}

	std::cout << "===================== END VIEW ===================================" << std::endl;
	#endif
}

void partition_model::print_segment(partition_view& view, cut_segment_id id, int cutid)
{
	#ifdef DEBUG
	segment_info* si = view.segment(id);
	if (!si)
		return;

	std::cout << ">> SEGMENT: " << id.value << " " << std::endl;
	if (cutid >= 0)
	{
		int diff = id.value - cut_segment(cutid).value;
		switch (diff)
		{
		case 0:
			std::cout << "\tcut" << cutid << std::endl;
			break;
		case 1:
			std::cout << "\tcut" << cutid << " source left" << std::endl;
			break;
		case 2:
			std::cout << "\tcut" << cutid << " source right" << std::endl;
			break;
		case 3:
			std::cout << "\tcut" << cutid << " target left" << std::endl;
			break;
		case 4:
			std::cout << "\tcut" << cutid << " target right" << std::endl;
			break;
		}
	}

	std::cout << "\tp1: " << si->src << std::endl;
	std::cout << "\tp2: " << si->tgt << std::endl;
	std::cout << "\torig_p1: " << si->orig_src << std::endl;
	std::cout << "\torig_p2: " << si->orig_tgt << std::endl;

	if (si->redge.has_edge())
		geom_utils::print(si->redge.seg, "edge");
	#endif
}

double partition_model::eval(partition_view& view) const
{
	return (double)(_plan.instructions.size() - view.instruction_index) + 0.9*(1 - view.quality);
}

// ensures edge 'he' is bounded by the hierarchy of cuts
//bool partition_model::is_candidate(partition_view& view, edge2 he)
bool partition_model::is_candidate(partition_view& view, const segment2& seg) const
{
	if (view.cut_index < 0)
		return true; //no constraints

	// the current cut is not tested, is only used to determine its left/right side respect to the parent cut
	assert(view.cut_index + 1 < _cuts.size());
	auto it = _cuts.find(view.cut_index + 1);
	partition_cut* child_cut = it->second;	// this is the current, not yet performed cut
	//partition_cut* child_cut = _cuts[view.cut_index + 1];	// this is the current, not yet performed cut
	partition_cut* cut = child_cut->parent;	// this is the parent of the current cut

	while (cut)
	{
		segment_info* segment = view.segment(cut->segment);
		line2 l(segment->src, segment->tgt);

		bool valid = true;
		if (cut->right == child_cut)
		{
			valid = !l.has_on_positive_side(seg.source())
				&& !l.has_on_positive_side(seg.target());
		}
		else
		{
			assert(cut->left == child_cut);
			valid = !l.has_on_negative_side(seg.source())
				&& !l.has_on_negative_side(seg.target());
		}

		if (!valid)
			return false;

		partition_cut* tmp = cut;
		cut = cut->parent;
		child_cut = tmp;
	}

	return true;
}
