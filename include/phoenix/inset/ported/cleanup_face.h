#pragma once

#include "phoenix/partition/ported/geometry_types.h"
#include "phoenix/partition/ported/geometry_utils.h"

#include <boost/function.hpp>

enum cleanup_merge_operation
{
	MERGE_FIRST_SOURCE,
	MERGE_FIRST_TARGET,
	MERGED,
	PERTURBED,
	NOT_MERGED,
};

template <typename K, typename A>
struct cleanup_face2
{
	DEFAULT_UTILS();
	DECLARE_ARRANGEMENT_TYPES(A)

	typedef boost::function2<bool, edge2, edge2> merge_predicate_function;
	typedef typename K::FT number;

	struct request
	{
		request() :
			degen_epsilon(1e-6),
			degen_epsilon2(degen_epsilon * degen_epsilon),
			colinear_epsilon(1e-4),
			colinear_epsilon2(colinear_epsilon * colinear_epsilon),
			//perturb_epsilon(1e-6),
			//perturb(false),
			match_labels(true),
			remove_antennas(false)
		{
		}

		void update_epsilon()
		{
			degen_epsilon2 = degen_epsilon * degen_epsilon;
			colinear_epsilon2 = colinear_epsilon * colinear_epsilon;
	}

		number degen_epsilon, degen_epsilon2;
		//number perturb_epsilon;
		number colinear_epsilon, colinear_epsilon2;
		bool match_labels;
		//bool perturb;
		bool remove_antennas;
		merge_predicate_function merge_predicate;
	};

	struct perturb_vertex
	{
		perturb_vertex(vertex2 v_, point2 p_) : v(v_), p(p_)
		{
		}

		vertex2 v;
		point2 p;
	};

	typedef std::vector<perturb_vertex> perturb_vertex_list;

	static void run(A& arr, request& r)
	{
		auto fit = arr.faces_begin();
		auto fend = arr.faces_end();

		CGAL_For_all(fit, fend)
		{
			if (!is_unbounded_face(fit))
				run(arr, fit, r);
		}
	}

	static void run(A& arr, face2& f, request& r)
	{
		r.update_epsilon();

		perturb_vertex_list to_perturb;
		auto first = f->outer_ccb();
		auto curr = first;

		do
		{
			//bool is_first = curr == first;
			//bool is_last = curr->next() == first;
			//std::cout << "label: " << curr->data().label << " next_label: " << curr->next()->data().label << "\n";
			//std::cout << "prev: " << curr->source()->point() << " pt: " << curr->target()->point() << " next: " << curr->next()->target()->point() << "\n";

			auto merged = merge(arr, f, curr, first, r);

			if (merged == edge2())
			{
				curr = curr->next();
				if (curr == first)
					break;
			}
			else
			{
				curr = merged;
			}
		} while (true);

		if (!to_perturb.empty())
			perturb_collinear(to_perturb);
	}

private:
	static bool are_colinear(point2& p1, point2& p2, point2& p3, number epsilon2)
	{
		line2 l(p1, p2);
		return CGAL::squared_distance(l, p3) <= epsilon2;
	}

	static edge2 merge(A& arr, face2& f, edge2 he, edge2& first, request r/*, perturb_vertex_list& perturbed*/)
	{
		auto he_next = he->next();
		auto he_next_next = he_next->next();
		if (he_next == he || he_next_next == he || he_next_next->next() == he)
			return edge2(); // don't simplify triangles

		if (he->target()->degree() > 2)
			return edge2();

		//always remove degenerates (if possible)
		geometry::segment2 seg(he->source()->point(), he->target()->point());
		typename K::FT len_next = 0;
		auto len = seg.squared_length();
		bool is_degenerated = len <= r.degen_epsilon2;
		bool should_merge = is_degenerated;
		if (should_merge)
		{
			len_next = len + 1.0;
			//std::cout << "merging small segment " << std::setprecision(10) << len << "\n";
		}

		/*if (should_merge && he->target()->degree() > 2)
		{
			// not good enough
			dj* _dj = dj::current();
			std::cout << "source degree " << he->source()->degree() << "\n";
			_dj->add("arr_0", arr);
			auto he_prev = he->prev();
			auto new_he = arr.insert_at_vertices(segment2(he->next()->target()->point(), he->source()->point()), he->next(), he->prev());
			edge2 he_to_remove = new_he->prev()->twin();
			new_he->data() = he_to_remove->data();
			new_he->twin()->data() = he_to_remove->twin()->data();
			_dj->add("arr_1", arr);
			arr.remove_edge(he_to_remove);
			_dj->add("arr_2", arr);
			return he_prev;
		}*/

		if (!should_merge)
		{
			// we want to merge colinear segments
			should_merge = are_colinear(he->source()->point(), he->target()->point(), he_next->target()->point(), r.colinear_epsilon2);

			if (should_merge)
			{	// he and he_next are two colineal segments and he->target() is the center vertex
				auto can_merge = he->target()->degree() <= 2;
				//std::cout << "labels: " << he->data().label << " == " << he_next->data().label << " , " << he->twin()->data().label << " == " << he_next->twin()->data().label << "\n";
				if (can_merge && r.match_labels)
					can_merge = he->data().label == he_next->data().label && he->twin()->data().label == he_next->twin()->data().label;
				if (can_merge && r.merge_predicate)
					can_merge = r.merge_predicate(he, he_next);

				len_next = CGAL::squared_distance(he->target()->point(), he_next->target()->point());
				if (!can_merge)
				{	// segments can not be simplified because different labels or vertex degree > 2
					should_merge = false;

					if (r.remove_antennas && he->target()->degree() <= 2 && len_next > r.degen_epsilon2)
					{	// if this is an antenna, remove anyway ignoring labels
						// this detect a flexible definition of an antenna, with 3 different vertexs and he->target is the center/peak, the other 2 vertexs could had different coordinates, 
						// this works because the 3 vertexs are colineal
						auto middle_line = seg.supporting_line().perpendicular(seg.target());
						should_merge = middle_line.has_on_positive_side(he_next->target()->point());
					}

					/*if (r.perturb)
					{
						auto v = vec2(he->target()->point(), he->source()->point())
							.perpendicular(CGAL::COUNTERCLOCKWISE);

						auto sl = CGAL::to_double(v.squared_length());
						if (sl > r.perturb_epsilon*r.perturb_epsilon)
							v = v / CGAL::sqrt(sl);

						auto np = he->target()->point() + v*r.perturb_epsilon;

						//he->target()->point() = np;
						//arr.modify_vertex(he->target(), np);
						auto he_data = he->data();
						auto he_next_data = he->next()->data();
						auto face_data = f->data();

						auto vsource = he->source();
						auto vtarget = he->next()->target();
						auto vnp = arr.insert_in_face_interior(np, f);
						vnp->data() = he->target()->data();
						arr.remove_edge(he->next());
						arr.remove_edge(he);
						auto new_seg1 = arr.insert_at_vertices(segment2(vsource->point(), np), vsource, vnp);
						auto new_seg2 = arr.insert_at_vertices(segment2(np, vtarget->point()), vnp, vtarget);
						f = new_seg2->face();

						new_seg1->data() = he_data;
						new_seg2->data() = he_next_data;
						f->data() = face_data;
						he = new_seg2;
						//auto new_he = arr.split_edge(he, segment2(he->source()->point(), np), segment2(np, he->target()->point()));
						//arr.merge_edge(new_he->prev(), new_he, segment2(new_he->prev()->source()->point(), np));
						//perturbed.push_back(perturb_vertex(he->target(), np));
					}*/
				}
			}
		}

		if (should_merge)
		{
			//std::cout << "degree = " << he->prev()->target()->degree() << ", " << he->target()->degree() << ", " << he->next()->target()->degree() << "\n";
			//geom_utils::print(he, "he");

			bool is_topol_antenna = he->source() == he_next->target(); // topological antenna (same vertex)
			if (is_topol_antenna)
			{
				if (he_next == first)
					first = he_next->next();

				edge2 result = he->prev();
				arr.remove_edge(he);

				return result;
			}

			auto dir1 = he->direction();
			auto dir2 = he_next->direction();
			edge2_data data, data_twin;
			// get data of the larger edge
			auto he_larger = (len < len_next) ? he_next : he;
			data = he_larger->data();
			data_twin = he_larger->twin()->data();

			segment2 merge_seg(he->source()->point(), he_next->target()->point());

			bool flip = false;
			if (dir1 != dir2)
			{
				auto res = CGAL::compare_xy(merge_seg.source(), merge_seg.target());
				if (res == CGAL::EQUAL)
					;// TD: geometric antenna
				else if (res == CGAL::SMALLER) // segment is left to right
					flip = dir1 == CGAL::ARR_RIGHT_TO_LEFT;
				else
					flip = dir1 == CGAL::ARR_LEFT_TO_RIGHT;
			}

			if (flip)
			{
				he = he->next()->twin();
				he_next = he->next();
				merge_seg = merge_seg.opposite();
				std::swap(data, data_twin);
			}

			bool update_first = false;
			if (he_next == first)
			{
				update_first = true;
			}
			else if (he_next->twin() == first)
			{
				update_first = true;
			}

			edge2 result;
			/*
			// for a geometric antenna?
			auto merge_target = [&arr](edge2 he)
			{
				auto data = he->data();
				auto data_twin = he->twin()->data();
				edge2 result = arr.merge_edge(he, he->next(), segment2(he->source()->point(), he->next()->target()->point()));
				result->data() = data;
				result->twin()->data() = data_twin;
				return result;
			};

			// remove the antenna
			result = merge_target(he->prev());
			result = merge_target(result);*/

			result = arr.merge_edge(he, he_next, merge_seg);

			result->data() = data;
			result->twin()->data() = data_twin;
			result = flip ? result->twin() : result;

			if (update_first)
				first = result->next();

			return is_degenerated ? result->prev() : result;
		}

		// segments not merged
		return edge2();
	}

	static void perturb_collinear(perturb_vertex_list& vertices)
	{
		for (auto vertex : vertices)
		{
			//vertex.v->point() = vertex.p;
		}
	}
};

template <typename K, typename P>
struct cleanup_face3
{
	DECLARE_MESH_TYPES(P)
	DECLARE_UTILS3(K, P)


	typedef typename boost::function2<bool, edge3, edge3> merge_predicate_function;
	//typedef typename K::FT number;
	typedef double number;

	struct request
	{
		request() :
			degen_epsilon(1e-6),
			perturb_epsilon(1e-4),
			colinear_epsilon(1e-4)
			//auto_align(false),
			//perturb(false)
		{
		}

		number degen_epsilon;
		number perturb_epsilon;
		number colinear_epsilon;
		//bool auto_align;
		//bool perturb;
		merge_predicate_function merge_predicate;
	};

	struct perturb_vertex
	{
		perturb_vertex(vertex3 v_, point3 p_) : v(v_), p(p_)
		{
		}

		vertex3 v;
		point3 p;
	};

	typedef std::vector<perturb_vertex> perturb_vertex_list;

	static void run(P& p, const request& r)
	{
		auto fit = p.facets_begin();
		auto fend = p.facets_end();

		CGAL_For_all(fit, fend)
		{
			run(p, fit, r);
		}
	}

	static void run(P& p, face3 f, const request& r)
	{
		join_vertexs(p, f, r);

		join_colinear_antennas(p, f, r);

		/*edge3 first = f->facet_begin();

		edge3 curr = first;
		plane3 pl = r.perturb ? geom_utils::face_plane2(f) : plane3();

		bool finished = false;
		perturb_vertex_list to_perturb;
		while(!finished)
		{
			cleanup_merge_operation op;
			curr = merge(p, curr, r, first, op, to_perturb, pl);

			switch (op)
			{
			case MERGE_FIRST_SOURCE:
				first = curr;
				break;
			case MERGED:
				break; //so we keep trying to merge
			case MERGE_FIRST_TARGET:
				finished = true;
				break;
			default:
				curr = curr->next();
				finished = curr == first;
				break;
			}
		}

		if (!to_perturb.empty())
			perturb_collinear(p, f, to_perturb, r.perturb_epsilon);*/
	}

private:

	static void join_vertexs(P& p, face3 f, const request& r)
	{
		edge3 first = f->facet_begin();
		edge3 curr = first;
		//plane3 pl = r.perturb ? geom_utils::face_plane2(f) : plane3();

		bool finished = false;
		//perturb_vertex_list to_perturb;
		while (!finished)
		{
			cleanup_merge_operation op;
			curr = merge_degenerated(p, curr, r, first, op);// , to_perturb, pl);

			switch (op)
			{
			case MERGE_FIRST_SOURCE:
				first = curr;
				break;
			case MERGED:
				break; //so we keep trying to merge
			case MERGE_FIRST_TARGET:
				finished = true;
				break;
			default:
				curr = curr->next();
				finished = curr == first;
				break;
			}
		}

		//if (!to_perturb.empty())
		//	perturb_collinear(p, f, to_perturb, r.perturb_epsilon);
	}

	static void join_colinear_antennas(P& p, face3 f, const request& r)
	{
		edge3 first = f->facet_begin();
		edge3 curr = first;

		bool finished = false;
		while (!finished)
		{
			cleanup_merge_operation op;
			curr = merge_colinear(p, curr, r, first, op);

			switch (op)
			{
			case MERGE_FIRST_SOURCE:
				first = curr;
				break;
			case MERGED:
				break; //so we keep trying to merge
			case MERGE_FIRST_TARGET:
				finished = true;
				break;
			default:
				curr = curr->next();
				finished = curr == first;
				break;
			}
		}
	}

	/*static bool are_colinear(point3& p1, point3& p2, point3& p3, double epsilon)
	{
		line3 l(p1, p2);
		return CGAL::squared_distance(l, p3) <= epsilon*epsilon;
	}*/

	static edge3 merge_degenerated(P& p, const edge3& he, const request& r, const edge3& first, cleanup_merge_operation& op/*, perturb_vertex_list& perturbed, plane3& pl*/)
	{
		op = NOT_MERGED;

		//geom_utils::print(he, "merging");

		// remove degenerates without checking the predicate
		auto prev_vertex = he->prev()->vertex();
		auto vertex = he->vertex();
		auto should_merge = CGAL::squared_distance(prev_vertex->point(), vertex->point()) <= r.degen_epsilon*r.degen_epsilon;

		if (should_merge)
		{
			// remove he->vertex()
			if (CGAL::circulator_size(he->facet_begin()) >= typename edge3::size_type(4) && CGAL::circulator_size(he->opposite()->facet_begin()) >= typename edge3::size_type(4))
			{
				if (he == first)
					op = MERGE_FIRST_SOURCE;
				else if (he->next() == first)
					op = MERGE_FIRST_TARGET;
				else
					op = MERGED;

				auto result = p.join_vertex(he->opposite());
				return result->next();
			}
		}

		return he;
	}

	static edge3 merge_colinear(P& p, const edge3& he, const request& r, const edge3& first, cleanup_merge_operation& op/*, perturb_vertex_list& perturbed, plane3& pl*/)
	{
		op = NOT_MERGED;

		//geom_utils::print(he, "merging");

		// check for colinearity or antennas to determine if vertex can be merged
		auto prev_vertex = he->prev()->vertex();
		auto vertex = he->vertex();
		auto next_vertex = he->next()->vertex();
		//std::cout << "cleanup on prev: " << prev_vertex->point() << ", vertex: " << vertex->point() << ", next: " << next_vertex->point() << "\n";

		// the vertex to remove should be bivalent on antennas and colinears
		if (!he->vertex()->is_bivalent())
			return he;

		bool predicate_ok = false;
		bool is_antenna = false;
		// check colinearity
		bool is_colinear = geom_utils::colinear(prev_vertex, vertex, next_vertex, r.colinear_epsilon);
		if (is_colinear)
		{
			// check first for the predicate because is cheaper than antennas
			predicate_ok = r.merge_predicate ? r.merge_predicate(he, he->next()) : true;

			if (!predicate_ok)
			{
				// antennas can always be merged
				auto angle = CGAL::angle(prev_vertex->point(), vertex->point(), next_vertex->point());
				//std::cout << "\tangle: " << angle << "\n";
				if (angle == CGAL::ACUTE)
					is_antenna = true;
			}


			/*if (!can_merge)
			{
				should_merge = false;
				if (r.auto_align)
				{
					line3 line(he->prev()->vertex()->point(), he->next()->vertex()->point());
					he->vertex()->point() = line.projection(he->vertex()->point());
				}
				else if (r.perturb)
				{
					auto p1 = pl.to_2d(he->prev()->vertex()->point());
					auto p2 = pl.to_2d(he->vertex()->point());

					auto v = vec2(p1, p2).perpendicular(CGAL::CLOCKWISE);

					auto sl = CGAL::to_double(v.squared_length());
					if (sl != 0)
					{
						v = v / CGAL::sqrt(sl);
						auto np = p2 + v * r.perturb_epsilon;

						op = PERTURBED;
						perturbed.push_back(perturb_vertex(he->vertex(), pl.to_3d(np)));
					}
				}
			}*/
		}

		if (is_colinear && (predicate_ok || is_antenna))
		{
			// remove he->vertex()
			if (CGAL::circulator_size(he->facet_begin()) >= typename edge3::size_type(4) && CGAL::circulator_size(he->opposite()->facet_begin()) >= typename edge3::size_type(4))
			{
				if (he == first)
					op = MERGE_FIRST_SOURCE;
				else if (he->next() == first)
					op = MERGE_FIRST_TARGET;
				else
					op = MERGED;

				auto result = p.join_vertex(he->opposite());
				return result->next();
			}
		}

		return he;
	}

	static void perturb_collinear(P& p, face3 f, perturb_vertex_list& vertices, number epsilon)
	{
		for (auto vertex : vertices)
		{
			vertex.v->point() = vertex.p;
		}
	}
};

