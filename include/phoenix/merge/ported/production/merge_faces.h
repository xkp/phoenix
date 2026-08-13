#pragma once

#include "phoenix/partition/geometry.h"
#include "phoenix/partition/ported/geometry_utils.h"
#include "simplify_face.h"

//#define DEBUG_MERGE_FACES

template<typename K, typename A, typename P>
struct merge_faces
{
	DECLARE_TYPES(K)
	DECLARE_ARRANGEMENT_TYPES(A)
	DECLARE_MESH_TYPES(P)
	DECLARE_UTILS(K, A, P)

	typedef typename P::Vertex_iterator vertex3_iterator;

	// determine if the faces adjacent to 'he' are also adjacent in another edge
	// Face with double adjacency are fine but they can not be simplified further without producing invalid geometry 
	static bool double_adjacency(geometry::edge2 he)
	{
		auto f1 = he->face();
		auto f2 = he->twin()->face();
		//if (f1-> facet_degree() < f2->facet_degree())
		//	std::swap(f1, f2);

		auto eit = f1->outer_ccb();
		auto end = eit;
		CGAL_For_all(eit, end)
		{
			edge2 h = eit;
			if (h != he && h->twin() != he && eit->twin()->face() == f2)
			{
				//std::cout << "double adyacency\n";
				return true;
			}
		}

		return false;
	}


	static void run(A& arr, bool match_labels = false)
	{
		typedef simplify_faces2<geometry::Kernel, geometry::arrangement2> simplify;
		simplify::run(arr, nullptr, match_labels);

		/*edge2_list to_remove;
		std::set<edge2> removed;
		for (auto eit = arr.halfedges_begin(); eit != arr.halfedges_end(); eit++)
		{
			auto f1 = eit->face();
			auto f2 = eit->twin()->face();
			if (is_unbounded_face(f1) || is_unbounded_face(f2))
				continue;

			if (f1 == f2) // no holes
				continue;

			if (match_labels && f1->data().label != f2->data().label)
				continue;

			auto rit = removed.find(eit);
			if (rit != removed.end())
				continue;

			to_remove.push_back(eit->twin());
			removed.insert(eit->twin());
		}

		for (auto eit : to_remove)
		{
			auto f1 = eit->face();
			auto f2 = eit->twin()->face();
			if (is_unbounded_face(f1) || is_unbounded_face(f2))
				continue;

			if (f1 != f2 && !double_adjacency(eit))
				arr.remove_edge(eit);
		}*/

		//file_utils::svg_doc doc;
		//doc.add(arr).save("c:\\dev\\merge_output.svg");
	}

	// ************************************************** 3d version **************************************************

	typedef std::function<bool(edge3 he)> can_join_faces_fn;

	struct request_t
	{
		request_t(bool match_labels_ = false, double tolerance_ = 1e-5) :
			match_labels(match_labels_),
			tolerance(tolerance_),
			can_join_faces([](edge3 he) { return true;  })
		{}

		bool match_labels;
		double tolerance;
		can_join_faces_fn can_join_faces;
	};

	static bool is_at_border(const vertex3& vertex)
	{
		auto eit = vertex->vertex_begin();
		auto end = eit;
		CGAL_For_all(eit, end)
		{
			if (eit->is_border_edge())
				return true;
		}

		return false;
	}

	static void delete_vertexs(P& mesh, vertex3_list& vertices)
	{
		for (auto& v : vertices)
		{
			// obey preconditions (no triangles)
			//auto f = v->halfedge()->face();
			//auto f_opp = v->halfedge()->opposite()->face();
			//if (circulator_size(v->halfedge()->facet_begin()) >= 4 && circulator_size(v->halfedge()->opposite()->facet_begin()) >= 4)
			if (!v->halfedge()->is_triangle() && !v->halfedge()->opposite()->is_triangle())
			{
				mesh.join_vertex(v->halfedge()->opposite());
			}
		}

#ifdef DEBUG_MERGE_FACES
		std::cout << "colinear vertices deleted: " << vertices.size() << "\n";
#endif
	}

	static bool can_delete_colinear_vertex(vertex3 v, double tsquare)
	{
		if (v->vertex_degree() == 2)
		{
			auto he = v->halfedge();
			auto he_opp = v->halfedge()->opposite();
			if (he->is_border_edge())
			{
				if (he->data.label == he->next()->data.label
					//&& he_opp->data.label == he_opp->prev()->data.label
					&& geom_utils::colinear(he->prev()->vertex(), he->vertex(), he->next()->vertex(), tsquare))
					return true;
			}
			else if (he->face()->data.label == he_opp->face()->data.label
				&& he->data.label == he->next()->data.label
				&& he_opp->data.label == he_opp->prev()->data.label
				&& geom_utils::colinear(he->prev()->vertex(), he->vertex(), he->next()->vertex(), tsquare))
				return true;
		}

		return false;
	}

	static void delete_colinear_vertexs(P& mesh, face3 f, double tsquare)
	{
		vertex3_list vertices;
		auto eit = f->facet_begin();
		auto end = eit;

		CGAL_For_all(eit, end)
		{
			if (can_delete_colinear_vertex(eit->vertex(), tsquare))
				vertices.push_back(eit->vertex());
		}

		delete_vertexs(mesh, vertices);
	}

	static void delete_colinear_vertexs(P& mesh, double tsquare)
	{
		vertex3_list vertices;

		auto vit = mesh.vertices_begin();
		auto vend = mesh.vertices_end();

		CGAL_For_all(vit, vend)
		{
			if (can_delete_colinear_vertex(vit, tsquare))
				vertices.push_back(vit);
		}

		delete_vertexs(mesh, vertices);
	}

	// detect the expanded edges starting from 'he' that belong to the same faces as 'he' and 'he->opposite()'
	static void get_expanded_edge(edge3 he, edge3& he_start, edge3& he_end)
	{
		auto f1 = he->face();
		auto f2 = he->opposite()->face();

		he_end = he->next();
		while (he_end->face() == f1 && he_end->opposite()->face() == f2)
		{
			he_end = he_end->next();
			if (he_end == he)
				break;
		}
		he_end = he_end->prev();

		he_start = he->prev();
		while (he_start->face() == f1 && he_start->opposite()->face() == f2)
		{
			he_start = he_start->prev();
			if (he_start == he_end)
				break;
		}
		he_start = he_start->next();
	}

	// determine if the faces adjacent to 'he' are also adjacent in another edge
	/*static bool double_adjacency(edge3 he)
	{
		auto f1 = he->face();
		auto f2 = he->opposite()->face();

		if (f1->facet_degree() < f2->facet_degree())
			std::swap(f1, f2);

		auto eit = f1->facet_begin();
		auto end = eit;
		CGAL_For_all(eit, end)
		{
			edge3 h = eit;
			if (h != he && h->opposite() != he && eit->opposite()->face() == f2)
			{
				//std::cout << "double adyacency\n";
				return true;
			}
		}

		return false;
	}*/

	static bool vertex_has_face(vertex3 v, face3 f)
	{
		if (v->vertex_degree() < 2)
			return false;

		typename P::Halfedge_around_vertex_circulator eit = v->vertex_begin();
		typename P::Halfedge_around_vertex_circulator end = eit;

		CGAL_For_all(eit, end)
		{
			if (eit->face() == f)
				return true;
		}

		return false;
	}

	// determine if the faces adjacent to 'he' are also adjacent in another edge (improved version, ignore the expanded edge)
	// Face with double adjacency are fine but they can not be simplified further without producing invalid geometry 
	static bool double_adjacency2(edge3 he)
	{
		auto f1 = he->face();
		auto f2 = he->opposite()->face();

		if (f1->facet_degree() < f2->facet_degree())
		{
			std::swap(f1, f2);
			he = he->opposite();
		}

		edge3 he_start, he_end;
		get_expanded_edge(he, he_start, he_end);

		for(edge3 h = he_end->next(); h != he_start; h = h->next())
		{
			if (h->opposite()->face() == f2)
			{
				//std::cout << "double adjacency2\n";
				return true;
			}

			// check by vertex, this is more agressive than just checking the edges
			if (h->next() != he_start && vertex_has_face(h->vertex(), f2))
			{
				//std::cout << "double adjacency2 vertex\n";
				return true;
			}
		}

		return false;
	}

	static void remove_expanded_edge(P& mesh, edge3 he)// , std::vector<vertex3>& vertices)
	{
		edge3 he_start, he_end;
		get_expanded_edge(he, he_start, he_end);

		// remove all edges of the range without creating antennas
		for (edge3 h = he_start; true; )
		{
			//auto it = std::find(vertices.begin(), vertices.end(), h->vertex());
			auto h_next = h->next();
			if (h_next == he_end) // the final segment
			{
				mesh.erase_center_vertex(h);
				//if (it != vertices.end())
				//	*it = vertex3();
				break;
			}
			else if (h == he_end)
			{
				std::cout << "-**************************************************\n";
				mesh.join_facet(h); //xxxx hacer esto eliminando vertices
				break;
			}

			//if (it != vertices.end())
			//	*it = vertex3();

			if (h->facet()->is_triangle() || h->opposite()->facet()->is_triangle())
			{	// special case, one of the faces is triangular so no more vertex can be joined, instead remove the center vertex and exit
				face3_data fd = h->face()->data;
				auto hh = mesh.erase_center_vertex(h);
				hh->face()->data = fd;
				break;
			}
			else
				mesh.join_vertex(h->opposite());

			h = h_next;

			//if (it != vertices.end())
			//	*it = vertex3();
		}
	}

	/*static edge3 get_larger_edge(face3 f)
	{
		edge3 he_larger;
		auto eit = f->facet_begin();
		auto d1 = CGAL::squared_distance(eit->prev()->vertex()->point(), eit->vertex()->point());
		auto d2 = CGAL::squared_distance(eit->vertex()->point(), eit->next()->vertex()->point());
		auto d3 = CGAL::squared_distance(eit->next()->vertex()->point(), eit->prev()->vertex()->point());
		if (d1 > d2)
			he_larger = (d1 > d3) ? eit : eit->prev();
		else
			he_larger = (d2 > d3) ? eit->next() : eit->prev();

		return he_larger;
	}*/

	/*static bool is_thin_tri(geometry::face3 f, edge3& he_larger)
	{
		if (!f->is_triangle())
			return false;

		auto eit = f->facet_begin();
		auto d1 = CGAL::squared_distance(eit->prev()->vertex()->point(), eit->vertex()->point());
		auto d2 = CGAL::squared_distance(eit->vertex()->point(), eit->next()->vertex()->point());
		auto d3 = CGAL::squared_distance(eit->next()->vertex()->point(), eit->prev()->vertex()->point());
		if (d1 > d2)
			he_larger = (d1 > d3) ? eit : eit->prev();
		else
			he_larger = (d2 > d3) ? eit->next() : eit->prev();

		auto tolerance = 1e-5*1e-5;
		//auto tolerance = 1e-2*1e-2;
		line3 l(he_larger->prev()->vertex()->point(), he_larger->vertex()->point());
		auto d = CGAL::squared_distance(l, he_larger->next()->vertex()->point());
		return d < tolerance;
	}*/

	static bool is_thin_hole(geometry::edge3 he_start)
	{
		auto eit = he_start;
		double max_len2 = -1; // maximus length of the face segments
		edge3 he_larger;
		std::vector<double> lengths2;
		do
		{
			auto d = CGAL::to_double(CGAL::squared_distance(eit->prev()->vertex()->point(), eit->vertex()->point()));
			lengths2.push_back(d);
			if (d > max_len2)
			{
				max_len2 = d;
				he_larger = eit;
			}

			eit->data.tag = d <= 1e-4 ? TAG_SMALL_EDGE : TAG_NORMAL_EDGE;

			eit = eit->next();
		} while (eit != he_start);

		/*if (debug)
		{
			geom_utils::print(he_larger, "larger");
			std::cout << max_len2 << std::endl;
		}*/

		if (max_len2 == 0)
			return true;

		double tolerance2 = 1e-5*1e-5;
		if (max_len2 <= tolerance2) // face is point-like
			return true;

		// use the larger edge to create the axis line
		line3 larger_line(he_larger->prev()->vertex()->point(), he_larger->vertex()->point());
		eit = he_start;
		do
		{
			if ((edge3)eit != he_larger && (edge3)eit != he_larger->prev())
			{
				auto d = CGAL::squared_distance(larger_line, eit->vertex()->point());
				/*if (debug)
					std::cout << d << ", ";*/
				if (d > tolerance2)
					return false; // hole is not thin
			}

			eit = eit->next();
		} while (eit != he_start);

		return true;
	}

	static bool is_thin(geometry::face3 f, bool debug = false)
	{
		auto eit = f->facet_begin();
		auto end = eit;
		double max_len2 = -1; // maximus length of the face segments
		edge3 he_larger;
		std::vector<double> lengths2;
		CGAL_For_all(eit, end)
		{
			auto d = CGAL::to_double(CGAL::squared_distance(eit->prev()->vertex()->point(), eit->vertex()->point()));
			lengths2.push_back(d);
			if (d > max_len2)
			{
				max_len2 = d;
				he_larger = eit;
			}

			eit->data.tag = d <= 1e-4 ? TAG_SMALL_EDGE : TAG_NORMAL_EDGE;
		}

		if (debug)
		{
			geom_utils::print(he_larger, "larger");
			std::cout << max_len2 << std::endl;
		}

		if (max_len2 == 0)
			return true;

		double tolerance2 = 1e-5*1e-5;
		if (max_len2 <= tolerance2) // face is point-like
			return true;

		line3 larger_line(he_larger->prev()->vertex()->point(), he_larger->vertex()->point());
		eit = f->facet_begin();
		CGAL_For_all(eit, end)
		{
			if ((edge3)eit != he_larger && (edge3)eit != he_larger->prev())
			{
				auto d = CGAL::squared_distance(larger_line, eit->vertex()->point());
				if (debug)
					std::cout << d << ", ";
				if (d > tolerance2)
					return false; // face is not thin
			}
		}

		if (debug)
			std::cout << std::endl;

		// face is thin at this point, calculate face length on its larger edge, on triangles its just max_len2
		double face_length2 = 0;

		if (lengths2.size() <= 3)
		{
			face_length2 = max_len2;
		}
		else
		{	// non triangles requiere special handling
			double max_d2 = -1e10;

			// find the vertex farther from p and use it to find the length of the face
			auto p = he_larger->vertex()->point();
			auto max_p = p;
			CGAL_For_all(eit, end)
			{
				auto d2 = CGAL::to_double(CGAL::squared_distance(p, eit->vertex()->point()));
				if (max_d2 < d2)
				{
					max_d2 = d2;
					max_p = eit->vertex()->point();
				}
			}

			plane3 perpendicular_plane(larger_line.perpendicular_plane(max_p));
			CGAL_For_all(eit, end)
			{
				auto d2 = CGAL::to_double(CGAL::squared_distance(perpendicular_plane, eit->vertex()->point()));
				if (face_length2 < d2)
					face_length2 = d2;
			}
		}

		// mark the larger edges as joinable
		int i = 0;
		CGAL_For_all(eit, end)
		{
			if (lengths2[i] > face_length2*0.999) // necesary??
				eit->data.tag = TAG_JOINABLE_EDGE;
			i++;
		}

		return true;
	}

	static bool coplanar(const plane3& pl, face3& f2, double tolerance)
	{
		if (pl.is_degenerate())
			return false;

		auto eit = f2->facet_begin();
		auto end = eit;
		CGAL_For_all(eit, end)
		{
			if (fabs(geom_utils::eval(pl, eit->vertex()->point())) > tolerance)
			//if (CGAL::squared_distance(eit->vertex()->point(), pl) > tsquare)
				return false;
		}

		return true;
	}

	static bool coplanar2(const plane3& pl, face3& f2, double tolerance)
	{
		if (pl.is_degenerate())
			return false;

		//auto tsquared = tolerance * tolerance;
		//auto n = geom_utils::face_normal(f2);
		//return geom_utils::coplanar_n(pl, f2, tolerance);

		auto pl2 = geom_utils::face_plane2(f2);
		auto n = pl2.orthogonal_vector();

		if (pl.has_on_negative_side(pl.point() + n))
		{
			//std::cout << "bad orientation\n";
			return false;
		}

		auto eit = f2->facet_begin();
		auto end = eit;
		CGAL_For_all(eit, end)
		{
			if (fabs(geom_utils::eval(pl, eit->vertex()->point())) > tolerance)
			//if (CGAL::squared_distance(eit->vertex()->point(), pl) > tsquare)
				return false;
		}

		return true;
	}

	static bool coplanar(face3& f1, face3& f2, double tolerance)
	{
		auto pl = geom_utils::face_plane2(f1);
		return coplanar(pl, f2, tolerance);
	}

	static bool edge_collapsable(edge3& he, bool match_labels, const can_join_faces_fn& can_join_faces)
	{
		// he->vertex() is the center vertex
		face3 f1 = he->face();
		face3 f2 = he->opposite()->face();

		// border edges can not be collapsed
		if (f1 == face3() || f2 == face3())
			return false;

		// same face can not be join, comment this to allow hole creation
		if (f1 == f2)// && he->opposite()->vertex()->degree() >= 2)
			return false;

		// check labels
		if (match_labels && f1->data.label != f2->data.label /*&& f1->data.label != -1 && f2->data.label != -1*/)
			return false;

		return can_join_faces(he);

		// commented, the plane is checked after for eficiency
		//return coplanar(he->face(), he->opposite()->face(), tolerance);
		return true;
	}

	/*static void delete_center_vertexs(P& mesh, const request_t& request)
	{
		const int TAG_NORMAL = 1;
		const int TAG_THIN = 2;
		const int TAG_PROCESSED = (1 << 8);

		auto is_tag_thin = [TAG_THIN](int tag) { return (tag & 0xff) == TAG_THIN; };
		auto is_tag_normal = [TAG_NORMAL](int tag) { return (tag & 0xff) == TAG_NORMAL; };

		//vertex3_list removable_vertices;
		vertex3_list vertices;
		typename P::Vertex_iterator vit = mesh.vertices_begin();
		typename P::Vertex_iterator vend = mesh.vertices_end();

		CGAL_For_all(vit, vend)
		{
			if (vit->vertex_degree() < 2)
				continue;

			vertices.push_back(vit);
		}

		//CGAL_For_all(vit, vend)
		for(auto& vit: vertices)
		{
			if (vit->vertex_degree() < 2)
				continue;

			typename P::Halfedge_around_vertex_circulator eit = vit->vertex_begin();
			typename P::Halfedge_around_vertex_circulator end = eit;

			bool removable = true;
			bool antenna_creation = false;
			CGAL_For_all(eit, end)
			{
				face3 f1 = eit->face();
				face3 f2 = eit->opposite()->face();
				if (f1 == face3() || f2 == face3())
				{
					removable = false;
					break;
				}

				// surrounding vertexs should have at least degree 3
				auto v_source = eit->opposite()->vertex();
				if (v_source->degree() < 3)
				{
					removable = false;
					break;
				}

				bool antenna;
				bool match_labels = request.match_labels && !is_tag_thin(f1->data.tag) && !is_tag_thin(f2->data.tag);

				if (!edge_collapsable(eit, match_labels, request.tolerance, antenna))
				{
					removable = false;
					break;
				}
			}

			if (!removable)
				continue;

			// find a valid plane
			plane3 pl;
			typename P::Facet_handle f_ref;
			eit = end = vit->vertex_begin();
			CGAL_For_all(eit, end)
			{
				pl = geom_utils::face_plane2(eit->face());
				if (!pl.is_degenerate())
				{
					f_ref = eit->face();
					break;
				}
			}

			if (pl.is_degenerate())
				// no normal face was found around this vertex
				continue;

			// check the rest of faces against that plane
			eit = end = vit->vertex_begin();
			CGAL_For_all(eit, end)
			{
				typename P::Facet_handle f(eit->face());

				if (f != f_ref && !coplanar(pl, f, request.tolerance))
				{
					removable = false;
					break;
				}
			}

			if (removable)
			{
				edge3 he = vit->vertex_begin();
				face3_data fd = he->face()->data;
				//std::cout << "removing center vertex\n";
#ifdef DEBUG_MERGE_FACES
				std::cout << "removing center vertex = " << kk << "\n";
#endif
				he = mesh.erase_center_vertex(he);
				he->face()->data = fd;

				//removable_vertices.push_back(vit);
			}
		}

	}*/

	// edge tags
	static const int TAG_NORMAL_EDGE = 1;
	static const int TAG_JOINABLE_EDGE = 2;
	static const int TAG_SMALL_EDGE = 3;
	//static const int TAG_POTENTIAL_JOIN = 4;
	static const int TAG_COPLANAR = 8;

	// face tags
	static const int TAG_NORMAL = 1;
	static const int TAG_THIN = 2;
	static const int TAG_PROCESSED = (1 << 8);

	static bool is_tag_thin(int tag) { return (tag & 0xff) == TAG_THIN; };
	static bool is_tag_normal(int tag) { return (tag & 0xff) == TAG_NORMAL; };
	static bool is_tag_processed(int tag) { return (tag & 0xff00) == TAG_PROCESSED; };
	static void set_tag_processed(int& tag) { tag = tag | TAG_PROCESSED; };
	static void clear_tag_processed(int& tag) { tag = tag & ~TAG_PROCESSED; };

	static void set_tag_coplanar(edge3 he)
	{
		he->data.tag |= TAG_COPLANAR;
		he->opposite()->data.tag |= TAG_COPLANAR;
	}

	static bool is_tag_coplanar(edge3 he)
	{
		return (he->data.tag & TAG_COPLANAR) != 0;
	}

	// classify faces
	static void classify_faces(P& mesh)
	{
		//face3_list thin_faces;
		for (auto fit = mesh.facets_begin(); fit != mesh.facets_end(); fit++)
		{
			//fit->data.tag = is_thin(fit, he_larger) ? TAG_THIN : TAG_NORMAL;
			if (is_thin(fit))
			{
				fit->data.tag = TAG_THIN;
				//thin_faces.push_back(fit);
			}
			else
				fit->data.tag = TAG_NORMAL;
		}
	}

	static bool remove_complex_thin_face(P& mesh, face3 f, bool join_small_edges = false)
	{
		auto eit = f->facet_begin();
		auto end = eit;
		double max_len = -1, min_len = 1e10;
		std::vector<double> lenghts2;
		line3 axis_line;
		plane3 perpendicular_plane;
		bool found = false;
		CGAL_For_all(eit, end)
		{
			if (eit->data.tag != TAG_SMALL_EDGE)
			{
				axis_line = line3(eit->prev()->vertex()->point(), eit->vertex()->point());
				perpendicular_plane = axis_line.perpendicular_plane(eit->vertex()->point());
				found = true;
				break;
			}
		}
		if (!found) return false;
		eit = f->facet_begin();
		end = eit;
		edge3 he_start, he_end;
		vertex3 v_start, v_end;
		double max_d2 = -1e10, min_d2 = 1e10;
		std::vector<double> distances;
		CGAL_For_all(eit, end)
		{
			double d2 = geom_utils::eval(perpendicular_plane, eit->vertex()->point());
			distances.push_back(d2);
			eit->vertex()->data.tag = (int)distances.size() - 1;
			if (max_d2 < d2) { max_d2 = d2; v_end = eit->vertex(); }
			if (min_d2 > d2) { min_d2 = d2; he_start = eit; v_start = eit->vertex(); }
		}
		edge3 he_right = he_start->next();
		edge3 he_left = he_start->prev();
		int i = 0;
		while (i < 100)
		{
			i++;
			double d_right = distances[he_right->vertex()->data.tag];
			double d_left = distances[he_left->vertex()->data.tag];
			if (he_left->vertex() == v_end && he_right->vertex() == v_end) break;
			if (he_right->next() == he_left)
			{
				if (d_right > d_left)
				{
					edge3 to_remove = he_right->opposite();
					he_left->next()->data = to_remove->data;
					he_left->data = to_remove->data;
					edge3 to_check = he_left->next()->opposite();
					mesh.join_facet(to_remove);
					if (join_small_edges)
					{
						double d = geom_utils::distance2(to_check);
						if (d < 1e-10 && !to_check->is_triangle() && !to_check->opposite()->is_triangle())
							mesh.join_vertex(to_check);
					}
				}
				else
				{
					edge3 to_remove = he_left->next()->opposite();
					he_right->data = to_remove->data;
					he_right->next()->data = to_remove->data;
					edge3 to_check = he_right->opposite();
					mesh.join_facet(to_remove);
					if (join_small_edges)
					{
						double d = geom_utils::distance2(to_check);
						if (d < 1e-10 && !to_check->is_triangle() && !to_check->opposite()->is_triangle())
						{
							//std::cout << "final join left\n";
							mesh.join_vertex(to_check);
						}
					}
				}

				break; // normal termination
			}

			// the old face after the split is the face on the pending area, new_diagonal is on the pending area
			edge3 new_diagonal = mesh.split_facet(he_left, he_right);

			if (d_right > d_left)
			{	// remove the right edge
				//std::cout << "remove right\n";
				edge3 to_remove = new_diagonal->opposite()->prev()->opposite();
				new_diagonal->opposite()->next()->data = to_remove->data;
				new_diagonal->opposite()->data = to_remove->data;

				mesh.join_facet(to_remove); // keep the big face

				if (join_small_edges)
				{
					double d = geom_utils::distance2(new_diagonal->opposite()->next());
					if (d < 1e-10 && !new_diagonal->is_triangle() && !new_diagonal->opposite()->is_triangle())
					{
						//std::cout << "** removing small right edge********\n";

						edge3 hh = mesh.join_vertex(new_diagonal->opposite());

						he_right = hh->next();
						he_left = hh->prev();

						continue;
					}
				}

				he_right = new_diagonal;
				he_left = new_diagonal->prev()->prev();
			}
			else
			{	// remove the left edge
				//std::cout << "remove left\n";
				edge3 to_remove = new_diagonal->opposite()->next()->opposite();
				new_diagonal->opposite()->prev()->data = to_remove->data;
				new_diagonal->opposite()->data = to_remove->data;

				mesh.join_facet(to_remove); // keep the big face

				if (join_small_edges)
				{
					double d = geom_utils::distance2(new_diagonal);
					if (d < 1e-10 && !new_diagonal->is_triangle() && !new_diagonal->opposite()->is_triangle())
					{
						//std::cout << "** removing small left edge********\n";

						edge3 hh = mesh.join_vertex(new_diagonal->opposite());

						he_right = hh->next();
						he_left = hh->prev();

						continue;
					}
				}

				he_right = new_diagonal->next();
				he_left = new_diagonal->prev();
			}

		}

		//std::cout << "number of iterations: " << i << "\n";
		return true;
	}

	// mesh faces should have been classified before calling this function
	static void remove_thin_faces(P& mesh, bool join_small_edges = false)
	{
		std::list<face3> thin_faces;
		for (auto fit = mesh.facets_begin(); fit != mesh.facets_end(); fit++)
		{
			if (is_tag_thin(fit->data.tag))
				thin_faces.push_back(fit);
		}

		if (thin_faces.empty())
			return;

		// delete the thin faces that are adjacent to a normal face in its joinable edge (the larger one)
		for(int i=0; i<20; i++)
		{
			//std::cout << "new iteration ***************************\n";
			int removed_faces = 0;
			for (auto fit = thin_faces.begin(); fit != thin_faces.end(); )
			{
				bool face_joined = false;
				auto eit = (*fit)->facet_begin();
				auto end = eit;
				CGAL_For_all(eit, end)
				{
					if (eit->data.tag == TAG_JOINABLE_EDGE)
					{
						if (eit->is_border_edge())
						{
							//std::cout << "erasing thin face at border\n";
							// if is a border, propagate the edges ids and labels to the inner edges
							if (eit->data.id >= 0)
							{
								for (auto xeit = eit->next(); xeit != eit; xeit = xeit->next())
								{
									xeit->data.id = eit->data.id;
									xeit->data.label = eit->opposite()->data.label;
									xeit->opposite()->data.id = eit->data.id;
								}
							}

							mesh.erase_facet(eit);

							face_joined = true;
							removed_faces++;
							fit = thin_faces.erase(fit);
							break;
						}
						else if (is_tag_normal(eit->opposite()->face()->data.tag))
						{
							//std::cout << "joinning thin face\n";
							// transfer edge labels of the big face to the small face
							auto eit2 = (*fit)->facet_begin();
							auto end2 = eit2;
							CGAL_For_all(eit2, end2)
							{
								eit2->data.label = eit->opposite()->data.label;
							}

							mesh.join_facet(eit->opposite());

							face_joined = true;
							removed_faces++;
							fit = thin_faces.erase(fit);
							break;
						}
					}
				}

				if (!face_joined)
					fit++;
			}

			//std::cout << "removed faces: " << removed_faces << "\n";

			if (thin_faces.empty())
				return;

			if (removed_faces == 0)
				break;
		}

		//std::cout << "kk pending thin faces: " << thin_faces.size() << "\n";

		// handle the more complex cases
		std::set<face3> pending_faces;
		for (auto fit = thin_faces.begin(); fit != thin_faces.end(); fit++)
			pending_faces.insert(*fit);

		int removed_faces = 0;

		// join the thin faces that are adjacent to another thin face through its joinable edge (the larger one)
		for (auto fit = pending_faces.begin(); fit != pending_faces.end(); )
		{
			bool face_joined = false;
			auto eit = (*fit)->facet_begin();
			auto end = eit;
			CGAL_For_all(eit, end)
			{
				if (eit->data.tag == TAG_JOINABLE_EDGE && !eit->is_border_edge())
				{
					// the opposite face is also thin
					if (/*eit->opposite()->data.tag == TAG_JOINABLE_EDGE &&*/ is_tag_thin(eit->opposite()->face()->data.tag))
					{
						// mark the opposite face as removed
						pending_faces.erase(eit->opposite()->face());

						// join the two thin faces and simplify it as one
						auto eit_prev = mesh.join_facet(eit);

						remove_complex_thin_face(mesh, eit_prev->face(), join_small_edges);

						face_joined = true;
						removed_faces++;
						fit = pending_faces.erase(fit);

						break;
					}
				}
			}

			if (!face_joined)
				fit++;
		}

		if (!pending_faces.empty())
		{
			//std::cout << "pending thin faces: " << pending_faces.size() << "\n";
			for (auto fit = pending_faces.begin(); fit != pending_faces.end(); )
			{
				if (!remove_complex_thin_face(mesh, *fit, join_small_edges))
					fit++;
				else
					fit = pending_faces.erase(fit);

				//mesh.erase_facet((*(fit++))->facet_begin());
			}

			//file_utils::save(mesh, "c:\\dev\\extrude_pending_thin.off");
		}

		if (!pending_faces.empty())
			std::cout << "pending thin faces: " << pending_faces.size() << "\n";
	}

	static void remove_interior_vertexs(P& mesh, request_t& request)
	{
		static int kk = 0;
		const int TAG_VERTEX_NORMAL = 1;
		const int TAG_VERTEX_ERASABLE = 2;

		// collect the vertices that can be removed
		std::list<vertex3> removable_vertices;
		typename P::Vertex_iterator vit = mesh.vertices_begin();
		typename P::Vertex_iterator vend = mesh.vertices_end();
		CGAL_For_all(vit, vend)
		{
			vit->data.tag = TAG_VERTEX_NORMAL;
			if (vit->vertex_degree() < 2)
				continue; // ignore antennas or isolated vertexs

			//kk++;
			//std::cout << kk << ":  " << vit->point() << "\n";

			typename P::Halfedge_around_vertex_circulator eit = vit->vertex_begin();
			typename P::Halfedge_around_vertex_circulator end = eit;

			bool removable = true;
			bool antenna_creation = false;
			CGAL_For_all(eit, end)
			{
				//if (kk == 323)
				//	std::cout << eit->opposite()->vertex()->point() << "\n";

				if (!edge_collapsable(eit, request.match_labels, request.can_join_faces))
				{
					removable = false;
					break;
				}
			}

			if (removable)
			{
				// check the planes
				bool one_plane_valid = false;
				CGAL_For_all(eit, end)
				{
					typename P::Facet_handle f(eit->face());
					typename P::Facet_handle f_opp(eit->opposite()->face());

					auto pl = geom_utils::face_plane2(f);
					if (pl.is_degenerate())
					{
						continue;
						//pl = geom_utils::face_plane2(f_opp);
						//f_opp = f;
					}
					else
						one_plane_valid = true;

					if (!coplanar(pl, f_opp, request.tolerance))
					{
						removable = false;
						break;
					}
				}

				if (one_plane_valid && removable)
				{
					removable_vertices.push_back(vit);
					vit->data.tag = TAG_VERTEX_ERASABLE;
				}
			}
		}

		// remove vertex-in-line (degree 2) by collapsing the edge
		auto remove_d2_vertex = [&](vertex3& v, vertex3_list& pending)
		{
			if (v == vertex3() || v->vertex_degree() != 2)
				return false;

			// bordes can not be removed this way
			auto he = v->halfedge();
			if (he->is_border_edge())
				return false;

			if (he->prev()->vertex_degree() > 2 && he->next()->vertex_degree() > 2 && !double_adjacency2(he))
			{
				//std::cout << kk << ": " << he->vertex()->point() << " ********************************************************\n";

				if (he->prev()->vertex_degree() == 3)
					pending.push_back(he->prev()->vertex());
				if (he->next()->vertex_degree() == 3)
					pending.push_back(he->next()->vertex());

				//if (kk == 380)
				//	file_utils::save(mesh, "c:\\dev\\kk___1.off");
				mesh.erase_center_vertex(he);
				//if (kk == 380)
				//	file_utils::save(mesh, "c:\\dev\\kk___2.off");
			}
			else if (!he->face()->is_triangle() && !he->opposite()->face()->is_triangle()) // triangles can not be removed this way
			{
				mesh.join_vertex(he->opposite());
			}
			else
				return false;

			return true;
		};

		auto remove_all_d2_vertex = [&](std::list<vertex3>& vertexs)
		{
			vertex3_list pending;

			size_t size_before = vertexs.size();
			vertexs.remove_if([&](vertex3& v)
			{
				return remove_d2_vertex(v, pending);
			});

			//std::cout << "################################# pending " << pending.size() << "\n";
			return size_before != vertexs.size();
		};

		// first pass, remove interior vertices
		for (int i = 0; i<4/*3*/; i++)
		{
			/*std::cout << "removable_vertices:\n";
			for (auto v : removable_vertices)
			{
				std::cout << v->point() << ", d:" << v->vertex_degree() << "\n";
			}*/

			if (i > 0)
			{
				std::cout << "################################# interior vertex iteration\n";
			}

			bool some_vertex_erased = remove_all_d2_vertex(removable_vertices);
			//file_utils::save(mesh, "c:\\dev\\merge_remove_interior_vert.off");

			bool double_adjacency_ = false;

			for (auto itv = removable_vertices.begin(); itv != removable_vertices.end(); )
			{
				auto& v = *itv;
				if (v == vertex3() || v->vertex_degree() == 0)
					continue;
				auto degree = v->vertex_degree();
				auto eit = v->vertex_begin();
				auto end = eit;
				kk++;
				//std::cout << kk << ":  " << v->point() << "\n";

				bool vertex_erased = false;
				bool collapsable = true;
				// surrounding_vertexs are the vertexs that are going to have degree 2 after center removal,
				// they have to be removed so futures erase_center_vertex will not create antennas
				vertex3_list surrounding_vertexs;
				CGAL_For_all(eit, end)
				{
					auto v_source = eit->opposite()->vertex();
					if (v_source->degree() <= 2)
					{
						collapsable = false; // dont create antennas
						break;
					}

					if (v_source->degree() == 3 && v_source->data.tag == TAG_VERTEX_ERASABLE)
						surrounding_vertexs.push_back(v_source);

					if (!edge_collapsable(eit, request.match_labels, request.can_join_faces))
					{
						collapsable = false;
						break;
					}
					else if (double_adjacency2(eit))
					{
						//file_utils::save(mesh, "c:\\dev\\merge_double_adj.off");
						double_adjacency_ = true;
						collapsable = false;
						break;
					}
				}

				if (collapsable)
				{
					// case: all incident faces can be collapsed, remove center vertex
					edge3 he = v->vertex_begin();
					//face3_data fd = he->face()->data;
#ifdef DEBUG_MERGE_FACES
					std::cout << "removing center vertex = " << kk << "\n";
#endif
					//std::cout << "removing center vertex = " << kk << "\n";
					he = mesh.erase_center_vertex(he);
					//he->face()->data = fd;
					itv = removable_vertices.erase(itv);

					vertex_erased = true;

					while (!surrounding_vertexs.empty())
					{
						vertex3_list pending;
						// atempt to remove surrounding vertexs if they are degree 2 and are pending for removal
						for (auto& sv : surrounding_vertexs)
						{
							auto it = std::find(removable_vertices.begin(), removable_vertices.end(), sv);
							if (it != removable_vertices.end())
							{
								if (remove_d2_vertex(sv, pending))
								{
									//std::cout << "removing surroudind vertex\n";
									bool update_itv	= (it == itv);
									auto new_it = removable_vertices.erase(it);
									if (update_itv)
										itv = new_it;
								}
							}
						}

						surrounding_vertexs = pending;
						pending.clear();
					}

#ifdef DEBUG_MERGE_FACES
					// Production debug file output intentionally omitted in Phoenix.

					if (!mesh.is_valid())
					{
						std::cout << "merge_faces: removing vertex mesh not valid\n";
						// Production debug file output intentionally omitted in Phoenix.
						break;
					}
#endif
				}

				if (vertex_erased)
					some_vertex_erased = true;
				else
					itv++;
			}

			//std::cout << "removable_vertices: " << removable_vertices.size() << "\n";
			if (removable_vertices.empty() || (!some_vertex_erased && !double_adjacency_))
				break;
		}

		/*std::cout << "***************************************\n";
		for (auto v : removable_vertices)
		{
			std::cout << v->point() << ", d:" << v->vertex_degree() << "\n";
		}*/

	}

	static void remove_interior_edges(P& mesh, request_t& request)
	{
		auto can_join_faces = [](edge3 he)
		{
			return he->vertex()->vertex_degree() >= 3 && he->opposite()->vertex()->vertex_degree() >= 3
				&& he->face() != he->opposite()->face()
				&& !double_adjacency2(he);
		};

		auto check_labels = [&request](const face3& f1, const face3& f2)
		{
			return !request.match_labels || f1->data.label == f2->data.label;
		};

		std::function<int(const plane3& pl, face3 f)> join_connected_coplaner_edges2;
		join_connected_coplaner_edges2 = [&](const plane3& pl, face3 f)
		{
			int faces_joined = 0;
			set_tag_processed(f->data.tag);

			auto eit = (edge3)f->facet_begin();
			auto end = eit;
			for (; true; )
			{
				// determine if we can expand the search to the face connected through eit
				auto opp_face = eit->opposite()->face();
				if (opp_face != face3() && !is_tag_processed(opp_face->data.tag))
				{
					bool track = false;
					//if (opp_face != face3() && is_tag_thin(opp_face->data.tag) && eit->data.label == 4)
					//{
					//	std::cout << "is thin\n";
					//	track = true;
					//}
					// dealing with thin faces sould not be necessary here anymore
					bool opp_is_thin = is_tag_thin(opp_face->data.tag);
					// if the face is thin try to join no matter what the request says
					if (opp_is_thin || (check_labels(f, opp_face) && request.can_join_faces(eit)))
					{
						if (track)
							std::cout << "first pass\n";
						if (coplanar2(pl, opp_face, request.tolerance))
						{
							set_tag_coplanar(eit);
							if (track)
								std::cout << "second pass\n";
							if (can_join_faces(eit))
							{
								if (track)
									std::cout << "third pass\n";
								//if (opp_is_thin && eit->opposite()->data.tag != TAG_JOINABLE_EDGE)
								//	eit->opposite()->data.tag = TAG_POTENTIAL_JOIN;

								// only join thin faces if the join edge is the larger of the thin face (to avoid creating antennas in normal faces)
								if (is_tag_normal(opp_face->data.tag) || (opp_is_thin && eit->opposite()->data.tag == TAG_JOINABLE_EDGE))
								{
									//std::cout << "\tjoinning\n";
									if (opp_is_thin)
									{
										// priorize edge labels of the big face
										auto eit2 = ((geometry::face3)opp_face)->facet_begin();
										auto end2 = eit2;
										CGAL_For_all(eit2, end2)
										{
											eit2->data.label = eit->data.label;
										}
									}

									edge3 eit_prev = eit->prev();

									mesh.join_facet(eit);

									faces_joined++;
									if (eit == end)
										end = eit_prev->next(); // adjust the new end

									eit = eit_prev->next();
									continue;	// force next iteration, dont check vs end
								}
							}
						}
					}
				}

				eit = eit->next();
				if (eit == end)
					break;
			}

			return faces_joined;
		};

		int degenerated_faces = 0;
		auto join_connected_coplaner_edges = [&](face3 f)
		{
			auto pl = geom_utils::face_plane2(f);
			if (pl.is_degenerate())
			{
				degenerated_faces++;
				return 0;
			}

			return join_connected_coplaner_edges2(pl, f);
		};

		for (auto eit = mesh.halfedges_begin(); eit != mesh.halfedges_end(); eit++)
			eit->data.tag = TAG_NORMAL_EDGE;

		// create groups, (list of connected, coplanar edges)
		int faces_joined = 0;
		bool loop_again = false;
		int i = 0;
		do
		{
			loop_again = false;
			for (auto fit = mesh.facets_begin(); fit != mesh.facets_end(); fit++)
			{
				i++;

				if (is_tag_processed(fit->data.tag))
					continue;
				if (!is_tag_normal(fit->data.tag))
					continue;

				auto new_faces_joined = join_connected_coplaner_edges(fit);
				faces_joined += new_faces_joined;
				if (new_faces_joined > 0)
					loop_again = true;
				//fit = mesh.facets_begin();
			}
		} while (loop_again);

		if (degenerated_faces > 0)
			std::cout << "degenerated_faces: " << degenerated_faces << "\n";
	}

	static void remove_interior_big_edges(P& mesh, request_t& request)
	{
		for (auto eit = mesh.halfedges_begin(); eit != mesh.halfedges_end(); eit++)
		{
			//std::cout << "attempt to removing big edges\n";
			if (!is_tag_coplanar(eit))
				continue;

			//std::cout << "removing big edges *****************************\n";
			remove_expanded_edge(mesh, eit);

			eit = mesh.halfedges_begin();
		}
	}

	static void run(P& mesh, bool match_labels = false)
	{
		request_t req(match_labels);
		run(mesh, req);
	}

	static void run(P& mesh, request_t& request)
	{
		//std::cout << "merge_faces: start\n";
		/*if (!mesh.is_valid())
		{
			//file_utils::saveOBJ(mesh, "c:\\dev\\merge_faces_input.obj");
			//file_utils::save(mesh, "c:\\dev\\merge_faces_input.off");
			//std::cout << "merge_faces: input mesh not valid\n";
		}*/
#ifdef DEBUG_MERGE_FACES
		if (!mesh.is_valid())
			std::cout << "merge_faces: input mesh not valid\n";
		// Production debug file output intentionally omitted in Phoenix.
#endif

		double tsquare = request.tolerance*request.tolerance;

		delete_colinear_vertexs(mesh, tsquare);

#ifdef DEBUG_MERGE_FACES
		// Production debug file output intentionally omitted in Phoenix.
#endif

		classify_faces(mesh);

		remove_thin_faces(mesh);

		remove_interior_vertexs(mesh, request);

#ifdef DEBUG_MERGE_FACES
		// Production debug file output intentionally omitted in Phoenix.
		if (!mesh.is_valid())
			std::cout << "merge_faces: after remove interior vertex, mesh not valid\n";
#endif
	
		//delete_colinear_vertexs(mesh, tsquare);
		//file_utils::saveOBJ(mesh, "c:\\dev\\merge_delete_colinear2.obj");

		remove_interior_edges(mesh, request);

		//remove_interior_big_edges(mesh, request);

		/*int pending_thin = 0;
		for (auto fit = mesh.facets_begin(); fit != mesh.facets_end(); fit++)
		{
			if (is_thin(fit))
				pending_thin++;
		}
		if (pending_thin > 0)
			std::cout << "pending_thin_faces: " << pending_thin << std::endl;*/

		//if (faces_joined > 0)
		//	std::cout << "joined_faces: " << faces_joined << std::endl;
			
		//delete_center_vertexs(mesh, request);

		//delete_colinear_vertexs(mesh, tsquare);
		//if (!mesh.is_valid())
		//	std::cout << "merge_faces: result mesh not valid\n";
#ifdef DEBUG_MERGE_FACES
		if (!mesh.is_valid())
			std::cout << "merge_faces: result mesh not valid\n";

		// Production debug file output intentionally omitted in Phoenix.
#endif
	}
};
