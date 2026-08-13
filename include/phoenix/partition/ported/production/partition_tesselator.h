#pragma once

#include "partition_solver.h"

#include "../geometry_types.h"

enum ignore_what {
	IGNORE_NONE,
	IGNORE_SOUTH,
	IGNORE_NORTH,
};

struct compass_labels
{
	struct label
	{
		label() :_label(-1)
		{}

		label(int label1) :_label(label1)
		{}

		label(int label1, int label2) :_label(label1 >= 0 ? label1 : label2)
		{}

		label(int label1, int label2, int label3) :_label(label1 >= 0 ? label1 : (label2 >= 0 ? label2 : label3))
		{}

		label& set(int value1, int value2 = -1)
		{
			_label = value1 >= 0 ? value1 : value2;

			return *this;
		}

		int operator() () const
		{
			return _label;
		}

		bool empty() const
		{
			return _label < 0;
		}

		label& if_empty(int value)
		{
			if (_label < 0)
				_label = value;

			return *this;
		}

		void apply(geometry::edge2 he) const
		{
			if (_label >= 0)
				he->data().label = _label;
		}

		void apply(geometry::edge2 from, geometry::edge2 to) const // "to" not included
		{
			if (_label >= 0)
				for (auto he = from; he != to; )
				{
					he->data().label = _label;
					he = he->next();
					if (he == from)
						break;
				}
		}

		void apply_twin(geometry::edge2 from, geometry::edge2 to) const // "to" not included
		{
			if (_label >= 0)
				for (auto he = from; he != to; )
				{
					he->twin()->data().label = _label;
					he = he->next();
					if (he == from)
						break;
				}
		}


	private:
		int _label;
	};

	label north, south, west, east, opp_west, opp_east;

	compass_labels() :
		north(), south(), west(), east(), opp_west(), opp_east()
	{}

	void apply(geometry::edge2 he_north, geometry::edge2 he_south, ignore_what ignore = IGNORE_NONE) const;
	void apply(point2 pt_north, geometry::edge2 he_south, ignore_what ignore = IGNORE_NONE) const;
	void apply(geometry::edge2 he_north, point2 pt_south, ignore_what ignore = IGNORE_NONE) const;

};

struct tessellator_node;
typedef std::shared_ptr<tessellator_node> tessellator_node_ref;

struct tessellator_node
{
	tessellator_node() :
		seg(geometry::segment2(geometry::point2(0,0), geometry::point2(0, 0))),
		left_face_label(-1), 
		right_face_label(-1),
		left_edge_label(-1),
		right_edge_label(-1),
		source_left_label(-1), source_left_label_opp(-1),
		source_right_label(-1), source_right_label_opp(-1),
		target_left_label(-1), target_left_label_opp(-1),
		target_right_label(-1), target_right_label_opp(-1)
	{
	}

	geometry::segment2 seg;
	boost::optional<geometry::plane3> plane;
	vec3 cut_dir;

	// face labels
	int left_face_label, right_face_label;

	// edge labels
	int left_edge_label, right_edge_label;
	int source_left_label, source_left_label_opp;
	int source_right_label, source_right_label_opp;
	int target_left_label, target_left_label_opp;
	int target_right_label, target_right_label_opp;

	// edge labels for repeat only
	boost::optional<compass_labels> repeat_edge_labels;	// edge labels for the repeat face
	boost::optional<compass_labels> last_repeat_edge_labels;  // edge labels for the last repeat face (in the last cut may coexist with repeat_edge_labels)

	tessellator_node_ref left, right;

	tessellator_node_ref create_left()
	{
		left = tessellator_node_ref(new tessellator_node());
		return left;
	};

	tessellator_node_ref create_right()
	{
		right = tessellator_node_ref(new tessellator_node());
		return right;
	};
};

struct cut_slope
{
	double pri_lens, pri_lent;
	double sec_len;
	double margin_start, margin_end;

	cut_slope():
		pri_lens(-1),
		pri_lent(-1),
		sec_len(-1),
		margin_start(-1),
		margin_end(-1)
	{}

	void advance_pri_left(point2& pt, const vec2& dir)
	{
		pt = pt + dir*pri_lens;
	}

	void advance_pri_right(point2& pt, const vec2& dir)
	{
		pt = pt + dir*pri_lent;
	}

	void advance_sec(point2& pt, const vec2& dir)
	{
		pt = pt + dir*sec_len;
	}

	void advance_margin_start(point2& pt, const vec2& dir)
	{
		pt = pt + dir*margin_start;
	}
};

struct partition_tesselator
{
	DEFAULT_UTILS();

	vm::const_icontext_ref _ctx;
	geometry::arrangement2& _arr;
	geometry::face2 _f;
	const partition_model& _model;
	partition_view& _view;
	randomizer_ref _rand;
	tessellator_node_ref _root_node;

	partition_tesselator(vm::const_icontext_ref ctx, geometry::arrangement2& arr, geometry::face2 f, const partition_model& model, partition_view& view, randomizer_ref rand):
		_ctx(ctx),
		_arr(arr),
		_f(f),
		_model(model),
		_view(view),
		_rand(rand)
	{

	}

	void run(face2_list& result_faces);

	//static void run(vm::const_icontext_ref ctx, geometry::arrangement2& arr, geometry::face2 f, const partition_model& model, partition_view& view, randomizer_ref rand, face2_list& result_faces);

	tessellator_node_ref nodes()
	{
		return _root_node;
	}

	private:
		struct repo_item
		{
			geometry::edge2 edge;
			geometry::vertex2 vertex; // used only if edge == edge2()
		};

		typedef std::map<repo_segment_id, repo_item> edge_map;
		void do_cut(const partition_cut* cut, edge_map& edges, dj& dj_, curves_original_seg_t& curves_seg, tessellator_node_ref tess_node);

		//static void fix_limits(const line2& middle_line, point2& pt_left, point2& pt_right, const line2& line_left, const line2& line_right, bool is_target);
		//void repeat_cut(const partition_cut* cut, geometry::edge2 src, geometry::edge2 dst, dj& dj_, tessellator_node_ref tess_node);
		void repeat_cut(const partition_cut* cut, repo_item& src, repo_item& dst, dj& dj_, tessellator_node_ref tess_node);

		unsigned distribute_by_length(const partition_repeat& repeat, double dist_left, double dist_right, cut_slope& slope);
		unsigned distribute_by_count(const partition_repeat& repeat, double dist_left, double dist_right, cut_slope& slope);
		geometry::edge2 cut_repeat_face(geometry::face2 f, const line2& l);
		static void apply_labels(geometry::edge2 he_north, geometry::edge2 he_south, int face_label, const compass_labels& edge_labels, ignore_what ignore = IGNORE_NONE);
		static void apply_labels(point2 pt_north, geometry::edge2 he_south, int face_label, const compass_labels& edge_labels, ignore_what ignore = IGNORE_NONE);
		static void apply_labels(geometry::edge2 he_north, point2 pt_south, int face_label, const compass_labels& edge_labels, ignore_what ignore = IGNORE_NONE);
		static void add_repo(edge_map& edges, repo_segment_id segment_id, const edge2& he, const vertex2& v);
		edge2 insert_cut(const partition_cut* cut, const segment2& cut_segment, curves_original_seg_t& curves_seg, face2& f, vertex2& vs, vertex2& vd);
		edge2_list insert_bezier(curves_original_seg_t& curves_seg, face2& f, vertex2& vs, vertex2& vd, const partition_cut* cut);
		edge2 insert_at_vertices(vertex2& vs, vertex2& vd, bool& inserted);

};
