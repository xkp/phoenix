#pragma once

#include "../../geometry.h"

struct extrude_plan_builder
{
	CGAL_TYPES(ExactKernel)
	CGAL_ARRANGEMENT(ExactKernel, edge2_data, face2_data, vertex2_data)
	DEFAULT_UTILS()

	class insertion_observer : public CGAL::Arr_observer<arrangement2>
	{
	public:
		int current_corner;
		vertex2 collision;

		insertion_observer(arrangement2& arr) :
			CGAL::Arr_observer<arrangement2>(arr),
			current_corner(-1)
		{
		}

		void clear_collision()
		{
			collision = vertex2();
		}

		virtual void after_create_edge(Halfedge_handle he)
		{
			he->data().id = current_corner;
			he->twin()->data().id = current_corner;
		}

		virtual void before_split_edge(Halfedge_handle /* e */,
			Vertex_handle v,
			const X_monotone_curve_2& c1,
			const X_monotone_curve_2& c2)
		{
			collision = v;
		}

		virtual void after_split_edge(Halfedge_handle e1, Halfedge_handle e2)
		{
			if (e1->data().id >= 0)
			{
				e2->data().id = e1->data().id;
				e2->twin()->data().id = e1->data().id;
			}
			else
			{
				assert(e2->data().id >= 0);
				e1->data().id = e2->data().id;
				e1->twin()->data().id = e2->data().id;
			}
		}
	};

	typedef std::map<int, int> collision_map;
	collision_map collisions;

	struct segment_item
	{
		segment_item() : prev(-1)
		{
		}

		segment_item(point2 _p1, point2 _p2, int _prev) :
			p1(_p1),
			p2(_p2),
			prev(_prev)
		{
		}

		point2 p1;
		point2 p2;
		int prev;
	};
	typedef std::map<int, segment_item> segments_cache;

	arrangement2 _arr;
	insertion_observer _observer;
	segments_cache _segments;

	bool _debug;

	extrude_plan_builder(bool debug_):
		_observer(_arr),
		_debug(debug_)
	{
	}
		
	void add(point2 p1, point2 p2, int index, int prev)
	{
		_segments[index] = segment_item(p1, p2, prev);

		_observer.current_corner = index;

		//if (_debug)
		//	std::cout << "adding: " << p1 << ", " << p2 << ", index: " << index << std::endl;
		
		CGAL::insert(_arr, curve2(p1, p2));

		if (_observer.collision != vertex2())
		{
			auto p = _observer.collision->point();
			auto it = _observer.collision->incident_halfedges();
			auto nd = it;

			CGAL_For_all(it, nd)
			{
				auto d = CGAL::squared_distance(it->source()->point(), it->target()->point());
				if (d < 1e-10 && it->data().id != index)
				{
					int source = -1, target = -1;

					auto dt = CGAL::squared_distance(p, p2);
					if (dt < 1e-10)
					{
						source = index;
					}
					else
					{
						auto ds = CGAL::squared_distance(p, p1);
						if (ds < 1e-10)
						{
							source = prev;
						}
					}

					auto s = _segments[it->data().id];
					if (CGAL::squared_distance(p, s.p2) < 1e-10)
						target = it->data().id;
					else if (CGAL::squared_distance(p, s.p1) < 1e-10)
						target = s.prev;

					if (source >= 0 && target >= 0)
						collisions[source] = target;
					
					break;
				}
			}

			_observer.clear_collision();
		}
	}

	void clear_collisions()
	{
		_arr.clear();
		collisions.clear();
		_segments.clear();
	}

	typedef std::vector<int> plan;
	typedef std::vector<plan> plan_container;

	void rebuild_plan()
	{
		_arr.clear();

		std::map<int, vertex2> vertices;
		for (auto s : _segments)
		{
			if (collisions.find(s.first) == collisions.end())
			{
				vertices[s.first] = _arr.insert_in_face_interior(s.second.p2, _arr.unbounded_face());
			}
		}

		for (auto c : collisions)
		{
			vertices[c.first] = vertices[c.second];
		}

		for (auto s : _segments)
		{
			if (vertices.find(s.second.prev) == vertices.end())
				continue;

			auto v1 = vertices[s.second.prev];
			auto v2 = vertices[s.first];
			auto he = _arr.insert_at_vertices(segment2(v1->point(), v2->point()), v1, v2);
			he->data().id = s.first;
			he->twin()->data().id = s.first;
		}
	}

	void get_plan(plan_container& results, std::set<int>& complexes)
	{
		if (!collisions.empty())
			rebuild_plan();

		remove_inside_edges();

#ifdef DEBUG_EXTRUDE
		if (_debug)
			io_utils<GeometryKernel, geometry::arrangement2, geometry::polyhedron3>::save(_arr, "c:\\dev\\nep.svg");
#endif // DEBUG

		face2_iterator it = _arr.faces_begin();
		face2_iterator nd = _arr.faces_end();
		for (; it != nd; it++)
		{
			face2 f = it;
			if (is_unbounded_face(f))
				continue;

			auto eit = f->outer_ccb();
			auto end = eit;

			std::vector<int> p;
			CGAL_For_all(eit, end)
			{
				if (eit->target()->degree() > 2)
				{
					//check the multiple incoming edges are legit
					auto it = eit->target()->incident_halfedges();
					auto nd = it;
					auto count = 0;
					CGAL_For_all(it, nd)
					{
						if (!is_unbounded_face((it->face())) || !is_unbounded_face(it->twin()->face()))
						{
							count++;
						}
					}

					if (count > 2)
					{
						complexes.insert(eit->data().id);
					}
				}

				p.push_back(eit->data().id);
			}

			results.push_back(p);
		}
	}

	void remove_inside_edges()
	{
		edge2_list to_remove;
		for (auto eit = _arr.edges_begin(); eit != _arr.edges_end(); eit++)
		{
			if (!is_unbounded_face(eit->face()) && !is_unbounded_face(eit->twin()->face()))
			{
				to_remove.push_back(eit);
			}
		}

		for (auto tr : to_remove)
		{
			_arr.remove_edge(tr);
		}
	}

	void check_hole(face2 f, bool is_hole, plan_container& results)
	{
		for (auto h = f->holes_begin(); h != f->holes_end(); h++) 
		{
			assert(false); //td: add holes
			//add_hole(h, !is_hole, results);
			//check_hole(h->face(), !is_hole, results);
		}
	}
};
