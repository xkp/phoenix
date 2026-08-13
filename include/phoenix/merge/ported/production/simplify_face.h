#pragma once

#include "phoenix/partition/geometry.h"
#include "phoenix/partition/ported/geometry_utils.h"


template <typename K, typename A>
struct simplify_faces2
{
	DECLARE_TYPES(K)
	DECLARE_ARRANGEMENT_TYPES(A)

	typedef A arrangement2;

	static edge2 erase_center_vertex(arrangement2& arr, edge2 he)
	{
		edge2 side_edge = he->prev();
		vertex2 center = he->target();
		auto eit = center->incident_halfedges();
		auto end = eit;
		edge2_list edges;
		CGAL_For_all(eit, end)
		{
			edges.push_back(eit);
		}

		for (auto edge : edges)
		{
			arr.remove_edge(edge, false, true);
		}

		return side_edge;
	}

	static void run(arrangement2& arr, face2_list* result, bool check_labels = true)
	{
		vertex2_list vertices;
		auto vit = arr.vertices_begin();
		auto vend = arr.vertices_end();
		CGAL_For_all(vit, vend)
		{
			vertices.push_back(vit);
		}

		auto fit = arr.faces_begin();
		auto fend = arr.faces_end();
		CGAL_For_all(fit, fend)
		{
			if (!is_unbounded_face(fit))
				fit->data().tag = TAG;
		}

		run(arr, vertices, result, check_labels);
	}

	static void run(arrangement2& arr, face2_list& faces, face2_list* result, bool check_labels = true)
	{
		if (faces.empty())
			return;

		vertex2_list vertices;

		for (typename face2_list::iterator it = faces.begin(); it != faces.end(); it++)
		{
			face2 f = *it;
			f->data().tag = TAG;

			auto eit = f->outer_ccb();
			auto end = eit;
			CGAL_For_all(eit, end)
			{
				vertex2 v = eit->target();
				if (v->data().index != TAG)
				{
					v->data().index = TAG;
					vertices.push_back(v);
				}
			}
		}

		run(arr, vertices, result, check_labels);
	}

	static void run(arrangement2& arr, vertex2_list& vertices, face2_list* result, bool check_labels = true)
	{
		bool need_results = result != nullptr;
		vertex2_list result_vertices;
		//file_utils::svg_doc doc;
		//doc.add(arr).save("c:\\dev\\simplify_face2d_input.svg");

		for (auto it = vertices.begin(); it != vertices.end(); it++)
		{
			vertex2 v = *it;
			//std::cout << "v->point() = " << v->point() << "\n";

			if (v->is_isolated())
			{
				//std::cout << "remove isolated\n";
				arr.remove_isolated_vertex(v);
				continue;
			}

			auto eit = v->incident_halfedges();
			auto end = eit;

			edge2_list collapsable_edges;
			int degree = 0;
			CGAL_For_all(eit, end)
			{
				degree++;
				if (are_collapsable(eit, check_labels))
					collapsable_edges.push_back(eit);
			}

			//std::cout << "degree = " << degree << "\n";
			face2 face;
			// indicate if the vertex is going to be in the results (not deleted)
			bool is_vertex_deleted = false;
			if (degree == 1)
			{
				//case: an isolated edge (antenna?)
				//std::cout << "collapsed isolated edge" << std::endl;
				is_vertex_deleted = true;
				face = arr.remove_edge(v->incident_halfedges(), false, true);
			}
			else if (degree == collapsable_edges.size())
			{
				//case: all incident faces can be collapsed
				//std::cout << "collapsed all edges" << std::endl;
				is_vertex_deleted = true;
				edge2 he = collapsable_edges[0];
				face2_data fd = he->face()->data();
				he = erase_center_vertex(arr, he);
				face = he->face();
				face->data() = fd;
			}
			else if (!collapsable_edges.empty())
			{
				//case: can remove some edges
				//std::cout << "collapsed various edges" << std::endl;
				for (typename edge2_list::iterator cit = collapsable_edges.begin(); cit != collapsable_edges.end(); cit++)
				{
					edge2 he = *cit;
					if (are_collapsable(he, check_labels) && !double_adjacency(he))
					{
						face2_data fd = he->face()->data();
						face = arr.remove_edge(he, false, false);
						face->data() = fd;
					}
				}
			}

			if (face != face2() && face->number_of_holes() > 0)
				throw std::logic_error("Can't simplify face due to hole creation");

			//td: when stable
			//if (degree - collapsable.size() == 2)
			//{
			//  //case: an intermediate vertex, if can be removed
			//  edge3 he = v->halfedge()->next();
			//  if ( circulator_size(he->facet_begin()) >= 4
			//    && circulator_size(he->opposite()->facet_begin()) >= 4)
			//  {
			//    result_vertex = false;
			//    p.join_vertex(he);
			//  }
			//}

			if (!is_vertex_deleted /*&& need_results*/)
				result_vertices.push_back(v);
		}

		// remove isolated vertexs and antennas
		auto new_end = std::remove_if(result_vertices.begin(), result_vertices.end(), [&arr](vertex2 v)
		{
			bool deleted = false;
			int degree = (int)v->degree();
			if (degree == 0)
			{
				arr.remove_isolated_vertex(v);
				deleted = true;
			}
			else if (degree == 1)
			{
				arr.remove_edge(v->incident_halfedges(), false, true);
				deleted = true;
			}

			return deleted;
		});

		result_vertices.erase(new_end, result_vertices.end());

		/*std::cout << "result_vertices\n";
		for (auto v : result_vertices)
		{
			std::cout << v->point() << ", degree = " << v->degree() << "\n";
		}
		std::cout << "end\n\n";*/

		//file_utils::svg_doc doc2;
		//doc2.add(arr).save("c:\\dev\\simplify_face2d_output.svg");

		if (need_results)
		{
			for (typename vertex2_list::iterator it = result_vertices.begin(); it != result_vertices.end(); it++)
			{
				vertex2 v = *it;

				auto eit = v->incident_halfedges();
				auto end = eit;
				CGAL_For_all(eit, end)
				{
					face2 f = eit->face();
					if (f != face2() && f->data().tag == TAG)
					{
						result->push_back(f);
						f->data().tag = -TAG; //avoid dupes
					}
				}
			}
		}
	}

private:
	static const int TAG = 234980738; //td: randomize from context
	static bool are_collapsable(edge2 he, bool check_labels)
	{
		//return false;
		face2 f1 = he->face(),
			f2 = he->twin()->face();

		//check nulls
		if (f1 == face2() || f2 == face2())
			return false;

		if (is_unbounded_face(f1) || is_unbounded_face(f2))
			return false;

		if (check_labels && f1->data().label != f2->data().label)
			return false;

		// is antenna, antenna can always be removed
		if (he->target()->degree() == 1 || he->source()->degree() == 1)
			return true;

		//same face, comment this to allow hole creation
		if (f1 == f2)
			return false;

		//check identity
		if (f1->data().tag != TAG || f2->data().tag != TAG)
			return false;

		return true;
		//return !check_labels || f1->data().label == f2->data().label;
	}

	// determine if the faces adjacent to 'he' are also adjacent in another edge
	static bool double_adjacency(edge2 he)
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

};


template <typename K, typename P>
struct simplify_faces
{
	DECLARE_TYPES(K)
	DECLARE_MESH_TYPES(P)
	DECLARE_UTILS3(K, P)

	typedef P polyhedron3;

	static void run(polyhedron3& p, face3_list& faces, face3_list* result, bool check_labels = true)
	{
		if (faces.empty())
			return;

		vertex3_list vertices;
		//std::cout << "faces: " << faces.size() << std::endl;

		for (typename face3_list::iterator it = faces.begin(); it != faces.end(); it++)
		{
			face3 f = *it;
			f->data.tag = TAG;

			typename polyhedron3::Halfedge_around_facet_circulator eit = f->facet_begin();
			typename polyhedron3::Halfedge_around_facet_circulator end = eit;
			CGAL_For_all(eit, end)
			{
				vertex3 v = eit->vertex();
				if (v->data.tag != TAG)
				{
					v->data.tag = TAG;
					vertices.push_back(v);
				}
			}
		}

		bool need_results = result != nullptr;
		vertex3_list result_vertices;

		for (typename vertex3_list::iterator it = vertices.begin(); it != vertices.end(); it++)
		{
			vertex3 v = *it;

			typename polyhedron3::Halfedge_around_vertex_circulator eit = v->vertex_begin();
			typename polyhedron3::Halfedge_around_vertex_circulator end = eit;

			edge3_list collapsable;
			int degree = 0;
			CGAL_For_all(eit, end)
			{
				degree++;
				if (are_collapsable(eit, check_labels))
				{
					//std::cout << "collapsing: " << eit->prev()->vertex()->point() << ", " << eit->vertex()->point() << std::endl;
					collapsable.push_back(eit);
				}
			}

			bool result_vertex = true;
			if (degree == collapsable.size())
			{
				//case: all incident faces can be collapsed
				result_vertex = false;
				edge3 he = collapsable[0];
				face3_data fd = he->face()->data;
				he = p.erase_center_vertex(he);
				he->face()->data = fd;
			}
			else if (!collapsable.empty())
			{
				//case: can remove some faces
				for (typename edge3_list::iterator cit = collapsable.begin(); cit != collapsable.end(); cit++)
				{
					edge3 he = *cit;
					//antennas
					if (he->vertex()->degree() < 3 || he->opposite()->vertex()->degree() < 3)
						continue;

					face3_data fd = he->face()->data;
					he = p.join_facet(he);
					he->face()->data = fd;
				}
			}

			//td: when stable
			//if (degree - collapsable.size() == 2)
			//{
			//  //case: an intermediate vertex, if can be removed
			//  edge3 he = v->halfedge()->next();
			//  if ( circulator_size(he->facet_begin()) >= 4
			//    && circulator_size(he->opposite()->facet_begin()) >= 4)
			//  {
			//    result_vertex = false;
			//    p.join_vertex(he);
			//  }
			//}

			if (result_vertex && need_results)
				result_vertices.push_back(v);
		}

		if (need_results)
		{
			for (typename vertex3_list::iterator it = result_vertices.begin(); it != result_vertices.end(); it++)
			{
				vertex3 v = *it;

				typename polyhedron3::Halfedge_around_vertex_circulator eit = v->vertex_begin();
				typename polyhedron3::Halfedge_around_vertex_circulator end = eit;
				CGAL_For_all(eit, end)
				{
					face3 f = eit->face();
					if (f != nullptr) {
						if (f->data.tag == TAG)
						{
							result->push_back(f);
							f->data.tag = -TAG; //avoid dupes
						}
					}
				}
			}
		}
	}

private:
	static const int TAG = 234980738; //td: randomize from context
	static bool are_collapsable(edge3 he, bool check_labels)
	{
		face3 f1 = he->face(),
			f2 = he->opposite()->face();

		//check nulls
		if (f1 == face3() || f2 == face3())
			return false;

		//same face, as it has been seen
		if (f1 == f2)
			return false;

		//check identity
		if (f1->data.tag != TAG || f2->data.tag != TAG)
			return false;

		if (check_labels && f1->data.label != f2->data.label)
			return false;

		/*geom_utils::print(f1, "f1");
		geom_utils::print(f2, "f2");
		if (CGAL::coplanar(
		he->prev()->vertex()->point(),
		he->vertex()->point(),
		he->next()->vertex()->point(),
		he->opposite()->next()->vertex()->point()))
		std::cout << "ARE COPLANAR" << std::endl;*/

		plane3 pl(
			he->prev()->vertex()->point(),
			he->vertex()->point(),
			he->next()->vertex()->point());

		if (pl.is_degenerate())
			return true;

		//and coplanarity
		const double PLANAR_TOLERANCE = 0.001 * 0.001;

		auto pd = CGAL::squared_distance(pl,
			he->opposite()->next()->vertex()->point());

		return pd < PLANAR_TOLERANCE;
	}
};

