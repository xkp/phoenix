#pragma once

#include "phoenix/partition/ported/null_diagnostics.hpp"
#include "../solver.h"
#include "../geometry_types.h"
#include "../geometry_utils.h"
#include "../segment_repository.h"
#include "phoenix/partition/ported/vm_compat.hpp"
#include "../error.h"

DEFAULT_CGAL_TYPES()
DEFAULT_ARRANGEMENT_TYPES()

// priority of instructions
#define PRIORITY_SELECT_EDGES			1
#define PRIORITY_SELECT_EDGES_SECONDARY 2
#define PRIORITY_DISTANCE_CONSTRAINT	3
#define PRIORITY_LENGTH_CONSTRAINT		4
#define PRIORITY_ANGLE_CONSTRAINT		5
#define PRIORITY_PRECUT_CONSTRAINT      8
#define PRIORITY_APPLY_CUT				9

//forwards
struct partition_solver_constraint;
struct partition_model;


typedef std::shared_ptr<partition_solver_constraint> partition_solver_constraint_ref;
typedef std::shared_ptr<partition_model> partition_model_ref;
typedef std::shared_ptr<const partition_model> const_partition_model_ref;

//cut info
enum partition_repeat_kind
{
	REPEAT_NONE,
	REPEAT_N_TIMES,
	REPEAT_BY_LENGTH,
};

enum cut_direction
{
	CUT_DIR_INTERPOLATE,
	CUT_DIR_SOURCE,		// parallel to source
	CUT_DIR_TARGET,		// parallel to target
	CUT_DIR_CUT,		// parallel to the main cut
};

enum cut_adjust_mode
{
	CUT_ADJ_PRIMARY,
	CUT_ADJ_SECONDARY,
	CUT_ADJ_EXTREMES,
	CUT_ADJ_FIRST,
	CUT_ADJ_LAST
};

enum cut_extent
{
	CUT_EXT_BASIC,
	CUT_EXT_SOURCE,
	CUT_EXT_TARGET,
	CUT_EXT_SOURCE_TARGET
};

struct partition_repeat
{
	partition_repeat() :
		kind(REPEAT_NONE),
		direction(CUT_DIR_INTERPOLATE),
		adjust_mode(CUT_ADJ_PRIMARY),
		extent(CUT_EXT_BASIC),
		count(),
		count_range(),
		length(),
		length_range(),
		maximun_cuts(),
		maximun_cuts_range(),
		secondary(),
		secondary_range(),
		faceLabel(-1),
		secondaryLabel(-1),
		marginStartFaceLabel(-1),
		marginEndFaceLabel(-1),
		secondaryEdgeLabel(-1),
		marginStartEdgeLabel(-1),
		marginEndEdgeLabel(-1),
		faceBottomLabel(-1),
		faceTopLabel(-1),
		faceRightLabel(-1),
		faceLeftLabel(-1),
		faceRightLabelOpp(-1),
		faceLeftLabelOpp(-1),
		secondaryBottomLabel(-1),
		secondaryTopLabel(-1),
		secondaryRightLabel(-1),
		secondaryLeftLabel(-1),
		secondaryRightLabelOpp(-1),
		secondaryLeftLabelOpp(-1),
		marginStartBottomLabel(-1),
		marginStartTopLabel(-1),
		marginStartRightLabel(-1),
		marginStartLeftLabel(-1),
		marginEndBottomLabel(-1),
		marginEndTopLabel(-1),
		marginEndRightLabel(-1),
		marginEndLeftLabel(-1),
		marginStartRightLabelOpp(-1),
		marginStartLeftLabelOpp(-1),
		marginEndRightLabelOpp(-1),
		marginEndLeftLabelOpp(-1)
	{
	}

	partition_repeat_kind kind;
	cut_direction direction;
	cut_adjust_mode adjust_mode;
	cut_extent extent;
	vm::variable_value count; //when kind == REPEAT_N_TIMES
	vm::variable_value count_range;
	vm::variable_value count_step;

	//accomodate as many cuts of length as possible
	vm::variable_value length;
	vm::variable_value length_range;
	vm::variable_value length_step;
	vm::variable_value maximun_cuts;
	vm::variable_value maximun_cuts_range;

	//potentially with some space in between
	vm::variable_value secondary;
	vm::variable_value secondary_range;
	vm::variable_value secondary_step;	

	vm::variable_value margin_start;
	vm::variable_value margin_end;

	// faces labels
	int faceLabel;
	int secondaryLabel;
	int marginStartFaceLabel;
	int marginEndFaceLabel;

	// edges labels
	int primaryEdgeLabel;
	int secondaryEdgeLabel;
	//int marginEdgeLabel;
	int marginStartEdgeLabel;
	int marginEndEdgeLabel;

	int faceBottomLabel;
	int faceTopLabel;
	int faceRightLabel;
	int faceLeftLabel;
	int faceRightLabelOpp;
	int faceLeftLabelOpp;

	int secondaryBottomLabel;
	int secondaryTopLabel;
	int secondaryRightLabel;
	int secondaryLeftLabel;
	int secondaryRightLabelOpp;
	int secondaryLeftLabelOpp;

	int marginStartBottomLabel;
	int marginStartTopLabel;
	int marginStartRightLabel;
	int marginStartLeftLabel;

	int marginEndBottomLabel;
	int marginEndTopLabel;
	int marginEndRightLabel;
	int marginEndLeftLabel;

	int marginStartRightLabelOpp;
	int marginStartLeftLabelOpp;
	int marginEndRightLabelOpp;
	int marginEndLeftLabelOpp;
};

typedef std::map<uniqueid, segment2> curves_original_seg_t;

struct bezier_cp
{
	bool enabled;
	double cp1_pct_x;
	double cp1_pct_y;
	double cp2_pct_x;
	double cp2_pct_y;
	var_bezier_options bez_options;

	bezier_cp():
		enabled(false),
		cp1_pct_x(0),
		cp1_pct_y(0),
		cp2_pct_x(0),
		cp2_pct_y(0),
		bez_options()
	{}

	void load(property_tree& data)
	{
		auto cp1 = data.get_optional<double>("Cp1_x");
		if (cp1)
		{
			cp1_pct_x = cp1.value();
			cp1_pct_y = data.get<double>("Cp1_y", 0);

			cp2_pct_x = data.get<double>("Cp2_x", 0);
			cp2_pct_y = data.get<double>("Cp2_y", 0);

			vm::variable_value step_length(data, "curveStepLength", -1);
			if (step_length)
			{
				/*bezier_margin margin_position = BEZIER_MARGIN_END;
				std::string smargin_position = data.get<std::string>("curveMargin", "");
				if (!smargin_position.empty() && smargin_position == "begin")
					margin_position = BEZIER_MARGIN_BEGIN;*/
				bezier_adjust adjust = BEZIER_ADJUST_END;
				std::string smargin_position = data.get<std::string>("curveMargin", "");
				std::string sadjust = data.get<std::string>("curveAdjustMode", "");
				if (smargin_position == "" && sadjust == "")
					;
				else if ((smargin_position == "begin" && sadjust == "") || sadjust == "begin")
					adjust = BEZIER_ADJUST_BEGIN;
				else if (sadjust == "beginend")
					adjust = BEZIER_ADJUST_BEGIN_END;
				else if (sadjust == "segments+")
					adjust = BEZIER_ADJUST_SEGMENT_EXCESS;
				else if (sadjust == "segments-")
					adjust = BEZIER_ADJUST_SEGMENT_DEFECT;

				bez_options = var_bezier_options(step_length, adjust);
			}

			enabled = true;
		}
	}

#if 0  // Phoenix boundary: legacy reader/model-loading integration is not ported.
	void load(reader_ref data)
	{
		auto cp1 = data->get_optional_double("Cp1_x");
		if (cp1)
		{
			cp1_pct_x = cp1.value();
			cp1_pct_y = data->get("Cp1_y", 0.0);

			cp2_pct_x = data->get("Cp2_x", 0.0);
			cp2_pct_y = data->get("Cp2_y", 0.0);

			vm::variable_value step_length(data, "curveStepLength", -1);
			if (step_length)
			{
				/*bezier_margin margin_position = BEZIER_MARGIN_END;
				std::string smargin_position = data.get<std::string>("curveMargin", "");
				if (!smargin_position.empty() && smargin_position == "begin")
					margin_position = BEZIER_MARGIN_BEGIN;*/
				bezier_adjust adjust = BEZIER_ADJUST_END;
				std::string smargin_position = data->get("curveMargin", std::string());
				std::string sadjust = data->get("curveAdjustMode", std::string());
				if (smargin_position == "" && sadjust == "")
					;
				else if ((smargin_position == "begin" && sadjust == "") || sadjust == "begin")
					adjust = BEZIER_ADJUST_BEGIN;
				else if (sadjust == "beginend")
					adjust = BEZIER_ADJUST_BEGIN_END;
				else if (sadjust == "segments+")
					adjust = BEZIER_ADJUST_SEGMENT_EXCESS;
				else if (sadjust == "segments-")
					adjust = BEZIER_ADJUST_SEGMENT_DEFECT;

				bez_options = var_bezier_options(step_length, adjust);
			}

			enabled = true;
		}
	}
#endif

	// calculate the control points for a particular face
	void compute(face2& f, curves_original_seg_t& curves_seg, const vertex2& vs, const vertex2& vd, point2& cp1, point2& cp2) const
	{
		DEFAULT_UTILS();

		auto dir_x = vd->point() - vs->point();
		auto line_x = line2(vs->point(), vd->point());
		auto dir_y = dir_x.perpendicular(CGAL::COUNTERCLOCKWISE);
		geom_utils::normalize(dir_y);
		double y_min = 1e20, y_max = -1e20;

		// find y_min, y_max of face
		auto eit = f->outer_ccb();
		//uniqueid last_curve_seg_id = -1000;
		auto end = eit;
		CGAL_For_all(eit, end)
		{
			boost::optional<point2> p;

			//if (last_curve_seg_id >= 0 && last_curve_seg_id == eit->data().id)
			//	continue; // ignore segments that belong to the last curve

			//last_curve_seg_id = -1000;
			if (eit->data().id >= 0)
			{
				// ignore segments that belong to a previous curve
				auto it = curves_seg.find(eit->data().id);
				if (it != curves_seg.end())
				{
					//last_curve_seg_id = eit->data().id;
					p = it->second.target(); // get the original target of the curve
				}
			}

			if (!p)
				p = eit->target()->point();

			double distance = geom_utils::line_signed_distance(line_x, *p);
			y_min = std::min(distance, y_min);
			y_max = std::max(distance, y_max);
		}

		//std::cout << cp1_pct_y << ", " << cp2_pct_y << ", " << y_min << ", " << y_max << "\n";
		cp1 = vs->point() + dir_x*cp1_pct_x + dir_y*cp1_pct_y*(cp1_pct_y >= 0 ? y_max : -y_min);
		cp2 = vs->point() + dir_x*cp2_pct_x + dir_y*cp2_pct_y*(cp2_pct_y >= 0 ? y_max : -y_min);
	}
};

enum cut_segment_type_
{
	BASE_SEGMENT = -1,
	CUT_SEGMENT = 0,
	SOURCE_LEFT = 1,
	SOURCE_RIGHT = 2,
	TARGET_LEFT = 3,
	TARGET_RIGHT = 4,
};

struct cut_segment_type
{
	cut_segment_type() :
		value(BASE_SEGMENT)
	{}

	cut_segment_type(cut_segment_type_ value_) :
		value(value_)
	{}

	operator cut_segment_type_() const
	{
		return value;
	}

	/*bool operator== (const cut_segment_type& it) const
	{
	return value == it.value;
	}*/

	cut_segment_type& operator= (const cut_segment_type_& it)
	{
		value = it;
		return *this;
	}

	void set(int v)
	{
		value = (cut_segment_type_)(v % 5);
	}

	void set(cut_segment_type_ v)
	{
		value = v;
	}

	bool is_base() const
	{
		return value == BASE_SEGMENT;
	}

	bool is_cut() const
	{
		return value == CUT_SEGMENT;
	}

	bool is_result() const
	{
		return value >= SOURCE_LEFT && value <= TARGET_RIGHT;
	}

	bool is_source() const
	{
		return value == SOURCE_LEFT || value == SOURCE_RIGHT;
	}

	bool is_left() const
	{
		return value == SOURCE_LEFT || value == TARGET_LEFT;
	}

	cut_segment_type_ value;
};

struct cut_segment_id : public repo_segment_id
{
	cut_segment_id() :
		repo_segment_id()
	{}

	explicit cut_segment_id(int segment_id) :
		repo_segment_id(segment_id)
	{}

	cut_segment_id cut_result(cut_segment_type type) const
	{
		return cut_segment_id(value + type);
	}
};

struct partition_cut
{
	partition_cut* parent;
	partition_cut(partition_cut* parent_) :
		parent(parent_),
		left(nullptr),
		right(nullptr),
		id(-1),
		segment(),
		src(),
		dst(),
		randomize_source(false),
		randomize_target(false),
		faceLeft(-1),
		faceRight(-1),
		cutLeft(-1),
		cutRight(-1),
		sourceLeft(-1),
		sourceRight(-1),
		targetLeft(-1),
		targetRight(-1),
		sourceLeftOpp(-1),
		sourceRightOpp(-1),
		targetLeftOpp(-1),
		targetRightOpp(-1),
		control_points()
	{
	}

	int id;
	cut_segment_id segment;
	cut_segment_id src;
	cut_segment_id dst;
	bool randomize_source, randomize_target;

	//labels
	int faceLeft;
	int faceRight;
	int cutLeft;
	int cutRight;
	int sourceLeft;
	int sourceRight;
	int targetLeft;
	int targetRight;
	int sourceLeftOpp;
	int sourceRightOpp;
	int targetLeftOpp;
	int targetRightOpp;

	//constraints
	std::vector<partition_solver_constraint_ref> constraints;

	//children
	partition_cut* left;
	partition_cut* right;

	partition_repeat repeat;

	// control points
	bezier_cp control_points;

	int children_count() const
	{
		return left_children_count() + right_children_count();
	}

	int left_children_count() const
	{
		return left ? 1 + left->children_count() : 0;
	}

	int right_children_count() const
	{
		return right ? 1 + right->children_count() : 0;
	}
};

//segment info
struct segment_info
{
	cut_segment_id id;
	point2 src;
	point2 tgt;
	point2 orig_src;
	point2 orig_tgt;
	repo_edge2 redge;

	segment_info(cut_segment_id id_, const repo_edge2& edge) :
		id(id_),
		src(edge.seg.source()),
		tgt(edge.seg.target()),
		orig_src(edge.seg.source()),
		orig_tgt(edge.seg.target()),
		redge(edge)
	{
	}

	segment_info(cut_segment_id id_, point2& src_, point2& tgt_/*, edge2 hedge_*/) :
		id(id_),
		src(src_),
		tgt(tgt_),
		orig_src(src_),
		orig_tgt(tgt_),
		redge()
	{
	}

	void reset()
	{
		src = orig_src;
		tgt = orig_tgt;
	}

	segment2 segment()
	{
		return segment2(orig_src, orig_tgt);
	}

	void print(const std::string& message);

	bool collapsed()
	{
		return src == tgt;
	}

	bool restrict_line(line2& l)
	{
		//segments must remain on the positive side of the line
		segment2 s(src, tgt);
		bool src_negative = l.has_on_negative_side(src);
		bool tgt_negative = l.has_on_negative_side(tgt);

		CGAL::Object iobj = CGAL::intersection(s, l);
		const point2* ipoint = CGAL::object_cast<point2>(&iobj);
		if (!ipoint)
			return !src_negative;

		if (src_negative)
			src = *ipoint;

		if (tgt_negative)
			tgt = *ipoint;

		return true;
	}

};

typedef std::vector<segment_info> seg_info_list;
//typedef std::map<int, segment_info> seg_info_map;
typedef std::map<repo_segment_id, segment_info> seg_info_map;

struct angle_range
{
	double min_angle;
	double max_angle;

	angle_range() :
		min_angle(INT_MIN),
		max_angle(INT_MAX)
	{
	}

	angle_range(double mina, double maxa) :
		min_angle(wrap(mina)),
		max_angle(wrap(maxa))
	{
	}

	// ensures angle is in the range [-M_PI, M_PI]
	static inline double wrap(double angle)
	{
		angle = fmod(angle, 2 * M_PI);
		return angle > M_PI ? angle - 2 * M_PI : (angle <= -M_PI ? angle + 2 * M_PI : angle);
	}

	void reset()
	{
		min_angle = INT_MIN;
		max_angle = INT_MAX;
	}

	bool is_restricted() const
	{
		return min_angle > INT_MIN;
	}

	bool inside(double angle) const
	{
		if (min_angle <= max_angle)
			return (min_angle - 1e-5) <= angle && angle <= (max_angle + 1e-5);
		else
			return (min_angle - 1e-5) <= angle || angle <= (max_angle + 1e-5);
	}

	angle_range opposite() const
	{
		return angle_range(min_angle + M_PI, max_angle + M_PI);
	}

	bool is_opposite(const angle_range& other) const
	{
		angle_range opposite(other.min_angle + M_PI, other.max_angle + M_PI);
		return fabs(min_angle - opposite.min_angle) <= 1e-5 && fabs(max_angle - opposite.max_angle) <= 1e-5;
	}

	// return the intersection of this range and 'other'
	bool intersect(const angle_range& other, angle_range& result) const
	{
		result.reset();
		if (inside(other.min_angle))
			result.min_angle = other.min_angle;
		else if (other.inside(min_angle))
			result.min_angle = min_angle;
		else
			return false; // no intersection

		result.max_angle = inside(other.max_angle) ? other.max_angle : max_angle;
		return true;
	}
};

struct partition_notify;
typedef std::shared_ptr<partition_notify> partition_notify_ref;

//notifier
struct partition_notify
{
	const std::map<int, int>& errors() const;

	void add_error(int instruction_index, int error_index = 0); //if there can be more than one error on an instruction
	void add_error(partition_notify_ref notify_);

	property_tree& debug_data();

private:
	std::map<int, int> _errors;
	property_tree _debug_data;
};

//typedef std::set<cut_segment_id> segment_set;

struct partition_view
{
	//geometric types
	DEFAULT_CGAL_TYPES()

	vm::const_icontext_ref ctx;
	const_segment_repository_ref repo;
	randomizer_ref rand;
	int cut_index;
	int instruction_index;
	seg_info_map segments;
	//segment_set used_segments;
	double error;
	double quality;
	std::vector<angle_range> angles;
	partition_notify_ref notify;
	bool has_errors;
	line2 cut_middle_line;
	//std::map<int, int> remap;

	partition_view(vm::const_icontext_ref ctx_, const_segment_repository_ref repo_, randomizer_ref rand_) :
		ctx(ctx_),
		repo(repo_),
		rand(rand_),
		instruction_index(0),
		cut_index(-1),
		error(1e10),
		angles(),
		has_errors(false),
		quality(0),
		cut_middle_line()
		//remap()
	{
		angles.push_back(angle_range());
	}

	partition_view(const partition_view& other) :
		ctx(other.ctx),
		repo(other.repo),
		rand(other.rand),
		instruction_index(other.instruction_index),
		cut_index(other.cut_index),
		segments(other.segments),
		error(other.error),
		angles(other.angles),
		notify(other.notify),
		has_errors(false),
		quality(other.quality),
		cut_middle_line(other.cut_middle_line)
		//remap(other.remap)
	{
	}

	partition_view(vm::const_icontext_ref ctx_, randomizer_ref rand_, int cut_index_, seg_info_map& segments_, double error_ = -1) :
		ctx(ctx_),
		rand(rand_),
		instruction_index(0),
		cut_index(cut_index_),
		segments(segments_),
		error(error_),
		angles(),
		quality(0),
		cut_middle_line()
		//remap()
	{
		angles.push_back(angle_range());
	}

	void copy(partition_view& other)
	{
		cut_index = other.cut_index;
		instruction_index = other.instruction_index;
		error = other.error;
		segments = other.segments;
		angles = other.angles;
		notify = other.notify;
		quality = 0;
		cut_middle_line = line2();
		//remap = other.remap;
	}

	/*cut_segment_id mapped(cut_segment_id id)
	{
		auto it = remap.find(id);
		return it == remap.end() ? id : cut_segment_id(it->second);
	}*/

	/*void add_mapping(cut_segment_id id1, cut_segment_id id2)
	{
		remap[id1] = id2;
	}*/

	const segment_info* segment(cut_segment_id id) const
	{
		auto it = segments.find(id);
		return it == segments.end() ? nullptr : &it->second;
	}

	segment_info* segment(cut_segment_id id)
	{
		auto it = segments.find(id);
		return it == segments.end() ? nullptr : &it->second;
	}

	bool has_segment(cut_segment_id id)
	{
		return segment(id) != nullptr;
	}

	bool has_edge(const repo_edge2& he)
	{
		for (auto it : segments)
		{
			if (it.second.redge == he)
				return true;
		}

		return false;
	}

	/*segment_info* add_segment(const segment_info& seg_info)
	{
		auto pr = segments.insert(seg_info_map::value_type(seg_info.id, seg_info));
		return &(pr.first->second);
	}*/

	segment_info* add_segment(const segment_info& seg_info)
	{
		segments.erase(seg_info.id);

		auto pr = segments.insert(seg_info_map::value_type(seg_info.id, seg_info));
		return &(pr.first->second);
	}

	bool is_angle_restricted()
	{
		return angles.size() != 1 || angles[0].is_restricted();
	}

	void reset()
	{
		reset_angle_restriction();

		for (auto it = segments.begin(); it != segments.end(); it++)
		{
			it->second.reset();
		}
	}

	void reset_angle_restriction() //called every cut
	{
		angles.clear();
		angles.push_back(angle_range());
	}

	void restrict_angles(double ref_angle, double mina, double maxa)
	{
		angle_range range1(ref_angle + mina, ref_angle + maxa);
		angle_range range2(ref_angle - maxa, ref_angle - mina);
		bool second_range = true;

		if (range1.inside(range2.max_angle))
		{	// join the 2 ranges into one
			range1.min_angle = range2.min_angle;
			second_range = false;
		}
		else if (range1.is_opposite(range2))
		{	// opposites angles are handled automatically later
			second_range = false;
		}

		auto range1_opp = range1.opposite();
		auto range2_opp = range2.opposite();
		std::vector<angle_range> new_angles;
		for (auto it = angles.begin(); it != angles.end(); it++)
		{
			angle_range result;
			if (it->intersect(range1, result) || it->intersect(range1_opp, result))
				new_angles.push_back(result);

			if (second_range)
				if (it->intersect(range2, result) || it->intersect(range2_opp, result))
					new_angles.push_back(result);
		}

		angles.swap(new_angles);
	}

	void notify_error(cut_segment_id error_id = cut_segment_id(0))
	{
		if (!has_errors)
		{
			has_errors = true;
			if (notify)
				notify->add_error(instruction_index - 1, error_id.value);
		}
	}
};

//plan
typedef std::vector<partition_view> partition_view_list;
typedef boost::function<void(partition_view&, const partition_model&, partition_view_list&)> partition_plan_brancher;
typedef boost::function<bool(partition_view&, const partition_model&)> partition_plan_worker;
typedef boost::function<bool(partition_view&, const partition_model&)> partition_plan_evaluator;
typedef std::map<int, property_tree> error_map;

typedef boost::variant<partition_plan_brancher, partition_plan_worker> partition_plan_instruction;
struct partition_plan_instruction_error
{
	error_map errors;
	partition_plan_instruction instruction;

	template<typename T>
	partition_plan_instruction_error(const T& instruction_) :
		instruction(instruction_),
		errors()
	{}

	template<typename T>
	partition_plan_instruction_error(const T& instruction_, const error_map& errors_) :
		instruction(instruction_),
		errors(errors_)
	{}
};

typedef std::multimap<int, partition_plan_instruction_error> partition_plan_instructions;
typedef std::multimap<const int, const partition_plan_evaluator> partition_plan_evaluators;
typedef std::pair<partition_plan_evaluators::const_iterator, partition_plan_evaluators::const_iterator> evaluator_range;
typedef partition_plan_instructions::iterator instruction_iterator;

//typedef boost::function<int(partition_view&, partition_model&)> partition_view_error;
typedef boost::function<void(property_tree&)> partition_export_error;

typedef std::vector<partition_view> partition_view_list;

struct partition_plan
{
	partition_plan() : cuts(0) {}

	int                         cuts;
	partition_plan_instructions instructions;
	partition_plan_evaluators   evaluators;

	void reset()
	{
		cuts = 0;
		instructions.clear();
		evaluators.clear();
	}

	bool empty() const
	{
		return instructions.empty();
	}

	size_t instruction_count() const
	{
		return instructions.size();
	}

	void build(const partition_model& model, partition_cut* c/*, constraint_map& cache*/);

	SolverPlanResult advance(const partition_model& model, partition_view& view, partition_view_list& branches) const;

	template <typename T>
	instruction_iterator add_instruction(int cut_id, T& instruction, int priority = 5, const error_map& errors = error_map())
	{
		assert(priority > 0 && priority < 10);
		int idx = cut_id * 10 + priority;

		auto iterator = instructions.insert(std::pair<int, partition_plan_instruction_error>(idx, partition_plan_instruction_error(instruction, errors)));
		return iterator;
	}

	void add_evaluator(int cut_id, partition_plan_evaluator eval, int priority)
	{
		evaluators.insert(std::pair<int, partition_plan_evaluator>(cut_id*10 + priority, eval));
	}

	evaluator_range get_evaluators(int cut_id, int priority) const
	{
		int key = cut_id * 10 + priority;
		return evaluators.equal_range(key);
	}

	const partition_plan_instruction* get(size_t index, int* priority = nullptr) const
	{
		size_t current = 0;
		for (auto it = instructions.begin(); it != instructions.end(); it++)
		{
			if (current == index)
			{
				if (priority)
					*priority = it->first % 10;
				return &(it->second.instruction);
			}

			current++;
		}

		assert(false);
		return nullptr;
	}

	void register_error(instruction_iterator it, int error_code, property_tree& data)
	{
		it->second.errors[error_code] = data;
	}

	bool get_error(int inst_index, int error_id, property_tree& data) const;
};

//model
typedef boost::function<bool(vm::const_icontext_ref ctx, const repo_edge2&, const repo_edge2&)> partition_filter;

struct partition_model_filter
{
	cut_segment_id segment1;
	cut_segment_id segment2;
	partition_filter filter;
	bool reversed;

	partition_model_filter(cut_segment_id segment1_, cut_segment_id segment2_, const partition_filter& filter_, bool reversed_ = false) :
		segment1(segment1_),
		segment2(segment2_),
		filter(filter_),
		reversed(reversed_)
	{}

	const partition_model_filter reverse() const
	{
		return partition_model_filter(segment2, segment1, filter, !reversed);
	}

	bool operator ()(vm::const_icontext_ref ctx, const repo_edge2& he1, const repo_edge2& he2) const
	{
		return reversed ? filter(ctx, he2, he1) : filter(ctx, he1, he2);
	}
};

enum branch_return_type
{
	BRANCH_OK = 0,
	BRANCH_FAIL_FIRST = 1,
	BRANCH_FAIL_SECOND = 2,
	BRANCH_FAIL_BOTH = 3
};

typedef std::vector<partition_solver_constraint_ref>	constraint_list;
typedef std::vector<partition_model_filter>				partition_model_filters;
//typedef std::map<cut_segment_id, int>					base_label_map;
struct base_label
{
	int label;
	int opp_label;

	base_label(int label_ = -1, int opp_label_ = -1):
		label(label_),
		opp_label(opp_label_)
	{}
};

typedef std::map<cut_segment_id, base_label> base_label_map;

struct partition_model
{
	DEFAULT_CGAL_TYPES()
	DEFAULT_ARRANGEMENT_TYPES()

	partition_model_filters     filters;
	//partition_model_cut_filters cut_filters;
	error_function	_efn;
	partition_model(partition_cut* root, int base_segments);
	void reset();

	//solver interface
	partition_plan& create_plan(vm::const_icontext_ref ctx);

	//accessors
	//point2& original_point(partition_view& v, int cut_id, bool is_source_segment, bool is_source_point) const;
	//point2& point(partition_view& v, int cut_id, bool is_source_segment, bool is_source_point) const;
	//void    point(partition_view& v, int cut_id, bool is_source_segment, bool is_source_point, const point2 value) const;

	//constraints
	int constraint_index() const
	{
		return (int)_constraints.size();
	}

	const partition_plan& plan() const
	{
		return _plan;
	}

	void add_constraint(partition_solver_constraint_ref constraint);
	void add_segment_info(cut_segment_id segment, const std::string& name, const std::string& info);
	const constraint_list& constraints() const { return _constraints; };

	//branching
	segment_info* branch_simple(partition_view& view, cut_segment_id segment, bool check_orientation) const;
	void branch(partition_view& view, cut_segment_id segment, partition_view_list& result, bool check_orientation) const;
	branch_return_type branch(partition_view& view, cut_segment_id segment1, cut_segment_id segment2, partition_view_list& result, bool check_orientation) const;
	void branch(partition_view& view, partition_cut* cut, partition_view_list& result) const;
	bool angle_solution(randomizer_ref rand, point2& p, double min_angle, double max_angle, segment2& segment, point2& solution) const;

	//access
	cut_segment_id cut_segment(partition_cut* cut) const;
	cut_segment_id cut_segment(int cut_id) const;
	const partition_cut* cut_root() const;
	const partition_cut* get_cut(int cut_id) const;
	const partition_cut* get_cut_by_segment(cut_segment_id segment) const;

	//filters
	void add_filter(cut_segment_id segment1, cut_segment_id segment2, partition_filter& i)
	{
		filters.push_back(partition_model_filter(segment1, segment2, i));
	}

	partition_model_filters get_filter(cut_segment_id segment_id) const
	{
		partition_model_filters segment_filters;

		for (auto it : filters)
		{
			if (segment_id == it.segment1)
				segment_filters.push_back(it);
			else if(segment_id == it.segment2)
				segment_filters.push_back(it.reverse());
		}

		return segment_filters;
	}

	partition_model_filters get_filter(cut_segment_id segment1, cut_segment_id segment2) const
	{
		partition_model_filters segment_filters;

		for (auto it : filters)
		{
			if (segment1 == it.segment1 && segment2 == it.segment2)
				segment_filters.push_back(it);
			else if (segment1 == it.segment2 && segment2 == it.segment1)
				segment_filters.push_back(it.reverse());
		}

		return segment_filters;
	}

	/*std::vector<partition_model_cut_filter> get_cut_filter(int segment1, int segment2)
	{
		std::vector<partition_model_cut_filter> return_filters;

		std::ostringstream buff;
		buff << segment1 << "_" << segment2;
		std::string str_id = buff.str();
		partition_model_cut_filters::iterator pos;
		for (pos = cut_filters.lower_bound(str_id); pos != cut_filters.upper_bound(str_id); ++pos)
		{
			return_filters.push_back(pos->second);
		}*/

		/*
		// -- Uncomment this to include the (segment2, segment1) filters too
		std::ostringstream buff1;
		buff1 << segment2 << "_" << segment1;
		std::string str_id1 = buff1.str();
		partition_model_cut_filters::iterator pos1;
		for (pos1 = cut_filters.lower_bound(str_id1); pos1 != cut_filters.upper_bound(str_id1); ++pos1)
		{
		   return_filters.push_back(pos1->second);
		}
		*/
	/*
		return return_filters;
	}*/

	//caused segment
	cut_segment_id get_caused_segment(const partition_cut* c, cut_segment_id segment_id) const;
	cut_segment_id get_parent_segment(cut_segment_id segment_id, const partition_cut* cut = nullptr) const;
	cut_segment_id get_root_segment(cut_segment_id segment_id) const;

	//segment identity
	bool is_cut_segment(cut_segment_id segment, int& cut_idx, cut_segment_type& segment_type) const;
	bool is_cut(cut_segment_id segment) const;
	bool is_base_segment(cut_segment_id segment) const;
	void set_base_segments(int base_segments)
	{
		_base_segments = base_segments;
	}
	std::string segment_name(cut_segment_id segment) const;
	std::string cut_name(int cut_id) const;

	//segment data
	const base_label_map& get_base_labels() const;
	void add_base_label(cut_segment_id id, int labelid);
	void add_base_opp_label(cut_segment_id id, int labelid);

	int get_segment_label(cut_segment_id id) const;

	//errors
	//void register_error(int instruction_id, int error_id, property_tree& data);
	bool select_edge_error(cut_segment_id segment, property_tree& data) const;
	bool cut_error(partition_cut& cut, property_tree& data) const;
	bool get_error(int error_key, property_tree& data) const;


	void set_dj(dj* dj_) const
	{
		//_dj = dj_;
	}

	dj& get_dj() const
	{
		static dj dj_(false);
		return dj_;
		//assert(_dj);
		//return *_dj;
	}

	void set_inst_dj(dj& dj_) const
	{
		//_dj_inst = &dj_;
	}

	dj& inst_dj() const
	{
		static dj dj_(false);
		return dj_;
		//assert(_dj_inst);
		//return *_dj_inst;
	}

private:
	typedef std::map<int, constraint_list>          constraint_map;
	typedef std::map<int, partition_cut*>           cut_map;
	typedef std::map<repo_segment_id, property_tree> segment_info_map;

	partition_cut* _root;
	cut_map _cuts;
	partition_plan _plan;
	constraint_list	_constraints;
	int _base_segments;
	segment_info_map _segment_info;
	base_label_map _base_label;
	dj* _dj;
	dj* _dj_inst;

	void fill_cuts_cache(partition_cut* cut);
public:
	//internal interface
	int search(edge2_list* edges, int from, int to) const;
public:
	void print(partition_view& view);
	void to_json(dj& dj_, const partition_view& view) const;
	double eval(partition_view& view) const;
	bool is_candidate(partition_view& view, const segment2& seg) const;
	partition_cut* find_cut_recursive(partition_cut* c, int cut_id) const;
private:
	//partition_view view_for_segment(partition_view& view, cut_segment_id segment, edge2 he);
	partition_view view_for_segment(partition_view& view, cut_segment_id segment, const repo_edge2& edge) const;
	partition_view view_for_segment(partition_view& view, cut_segment_id segment, segment_info& info) const;
	partition_view view_for_segments(partition_view& view, segment_info& s1, segment_info& s2) const;
	partition_view view_for_cut(partition_view& view, partition_cut* cut, point2& sp, point2& tp, double quality, dj& dj_) const;

	void print_segment(partition_view& view, cut_segment_id id, int cutid = -1);
};

struct partition_solver_constraint
{
	DEFAULT_CGAL_TYPES()
	DEFAULT_ARRANGEMENT_TYPES()
	DEFAULT_UTILS()

	virtual int  id()  { return _id; }
	virtual int  cut_idx() { return _cut_idx; }
	virtual void build(vm::const_icontext_ref ctx, const partition_model& model, partition_plan& plan) = 0;

	void register_error(int error_code, const property_tree& data)
	{
		_errors[error_code] = data;
	}

	void register_error(const property_tree& data)
	{
		_errors[0] = data;
	}

	bool get_error(int error_code, property_tree& data) const
	{
		auto it = _errors.find(error_code);
		if (it == _errors.end())
			return false;

		data = it->second;
		return true;
	}

	const error_map& errors()
	{
		return _errors;
	}

	partition_solver_constraint(int id, int cut_id = -1) :
		_id(id),
		_priority(0),
		_cut_idx(cut_id),
		_errors()
	{}

protected:
	int _id;
	int _priority;
	int _cut_idx;
	error_map _errors;
};

//some branchers
struct select_edges
{
	cut_segment_id src, tgt;
	bool check_orientation;
	// used when source or target are cut-result and can be remapped to base segments
	bool randomize_source, randomize_target;

	select_edges(partition_cut* cut_) :
		src(cut_->src),
		tgt(cut_->dst),
		randomize_source(cut_->randomize_source),
		randomize_target(cut_->randomize_target),
		check_orientation(true)
	{}

	/*select_edges(int src_, int dst_) :
		src(src_),
		dst(dst_),
		check_orientation(true)
	{}*/

	select_edges(cut_segment_id src_) :
		src(src_),
		tgt(),
		randomize_source(false),
		randomize_target(false),
		check_orientation(false)
	{}

	void operator ()(partition_view& view, const partition_model& model, partition_view_list& result);
};
