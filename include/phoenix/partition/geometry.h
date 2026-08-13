#pragma once

#include <boost/smart_ptr.hpp>
#include <CGAL/exceptions.h>
#include "backend/geometry_types.h"
#include "backend/geometry_utils.h"
#include "phoenix/partition/ported/bezier_utils.h"

CGAL_TYPES(GeometryKernel)

template<typename T>
bool is_unbounded_face(const T& face)
{
	return face->is_unbounded() || face->data().label == LABEL_UNBOUNDED_IDX;
}

struct vertex2_data
{
	vertex2_data() : id(-1), index(-1), tag(-1) {}

	int id;
	int index;
	int tag;
};

struct edge3_data;

struct edge2_data
{
	edge2_data() : id(-1), label(-1), tag(-1) {}

	int id;
	int label;
	int tag;

	inline edge3_data to_3d() const;
};

struct face2_data
{
	face2_data() : id(-1), label(-1), tag(-1) {}

	int id;
	int label;
	int tag;
};

struct vertex3_data
{
	vertex3_data() : id(-1), index(-1), tag(-1) {}

	int id;
	int index;
	int tag;
};

struct edge3_data
{
	edge3_data() : id(-1), label(-1), tag(-1) {}

	int id;
	int label;
	int tag;

	inline edge2_data to_2d() const;
};

edge3_data edge2_data::to_3d() const
{
	edge3_data data;
	data.id = id;
	data.label = label;
	data.tag = tag;
	return data;
};

edge2_data edge3_data::to_2d() const
{
	edge2_data data;
	data.id = id;
	data.label = label;
	data.tag = tag;
	return data;
};

struct face3_data
{
	face3_data() : id(-1), label(-1), tag(-1), smoothing_group(0){}

	int id;
	int label;
	int tag;
	uint32_t smoothing_group;
};

inline std::ostream& operator<< (std::ostream& os, const vertex2_data& vd)
{
	os << vd.id << " " << vd.index;
	return os;
}

inline std::istream& operator >> (std::istream& is, vertex2_data& vd)
{
	is >> vd.id;
	is >> vd.index;
	return is;
}

inline std::ostream& operator<< (std::ostream& os, const edge2_data& ed)
{
	os << ed.id << " " << ed.tag << " " << ed.label;
	return os;
}

inline std::istream& operator >> (std::istream& is, edge2_data& ed)
{
	is >> ed.id;
	is >> ed.tag;
	is >> ed.label;
	return is;
}

inline std::ostream& operator<< (std::ostream& os, const face2_data& fd)
{
	os << fd.id << " " << fd.tag << " " << fd.label;
	return os;
}

inline std::istream& operator >> (std::istream& is, face2_data& fd)
{
	is >> fd.id;
	is >> fd.tag;
	is >> fd.label;
	return is;
}

// forward declarations
namespace vm
{
	struct icontext;
	typedef std::shared_ptr<icontext> icontext_ref;
}

struct geometry;
typedef std::shared_ptr<geometry> geometry_ref;
typedef std::vector<geometry_ref> geometry_ref_list;

struct geometry
{
public:
	typedef GeometryKernel Kernel;
	typedef Kernel::FT number;

	CGAL_TYPES(Kernel)
	CGAL_ARRANGEMENT(Kernel, edge2_data, face2_data, vertex2_data)
	CGAL_MESH(Kernel, vertex3_data, edge3_data, face3_data)

	typedef HDS3D HDS;

	arrangement2 arr;
	polyhedron3 mesh;

	geometry() {}
	geometry(arrangement2& arr_, polyhedron3& mesh_) : arr(arr_), mesh(mesh_) {}

	bool empty()
	{
		if (arr.number_of_faces() > 1)
			return false;

		if (mesh.size_of_facets() > 0)
			return false;

		return true;
	}

	void copy(geometry_ref& input)
	{
		arr = geometry::arrangement2(input->arr);
		mesh = geometry::polyhedron3(input->mesh);
	}

	void add_mesh(vm::icontext_ref ctx, geometry_ref other);

	int num_valid_faces() const
	{
		int f3 = (int)mesh.size_of_facets();
		if (f3 > 0)
			return f3;
		else
		{
			// count the number of valid faces (ignore unbounded and fake unbounded faces)
			int number_of_faces = 0;
			auto fit = arr.faces_begin();
			auto fnd = arr.faces_end();
			CGAL_For_all(fit, fnd)
			{
				if (!is_unbounded_face(fit))
					number_of_faces++;
			}

			return number_of_faces;
			//return (int)arr.number_of_faces() - (int)arr.number_of_unbounded_faces();
		}
	}

	void enum_face2(const std::function<void(face2 &)> enumerator)
	{
		for (auto fit = arr.faces_begin(); fit != arr.faces_end(); fit++)
		{
			if (is_unbounded_face(fit))
				continue;

			enumerator(fit);
		}
	}

	void enum_face3(const std::function<void(face3 &)> enumerator)
	{
		for (auto fit = mesh.facets_begin(); fit != mesh.facets_end(); fit++)
		{
			enumerator(fit);
		}
	}

	geometry::face2_list faces2()
	{
		geometry::face2_list fl;

		for (auto fit = arr.faces_begin(); fit != arr.faces_end(); fit++)
		{
			if (is_unbounded_face(fit))
				continue;

			fl.push_back(fit);
		}

		return fl;
	}

	geometry::face3_list faces3()
	{
		geometry::face3_list fl;

		for (auto fit = mesh.facets_begin(); fit != mesh.facets_end(); fit++)
		{
			fl.push_back(fit);
		}

		return fl;
	}

	void to_double()
	{
		if (!mesh.is_empty())
		{
			for (auto vit = mesh.vertices_begin(); vit != mesh.vertices_end(); vit++)
			{
				point3& p = vit->point();
				vit->point() = point3(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
			}
		}
	}

	void to_3d();
	void save(std::fstream& ofs);

	static void save_arr(geometry::arrangement2& arr, std::fstream& ofs);
	static void save_mesh(geometry::polyhedron3& mesh, std::fstream& ofs);

	bool load(std::fstream& ifs);

private:
	bool load_arr(std::fstream& ifs);
	bool load_mesh(std::fstream& ifs);
};


// geometry with SimpleKernel (CGAL::Simple_cartesian<double>) as kernel, used to convert a geometry::polyhedron3 to a polyhedron3 with a SimpleKernel
struct simple_geometry
{
	typedef SimpleKernel Kernel;
	typedef Kernel::FT number;

	CGAL_TYPES(Kernel);
	CGAL_ARRANGEMENT(Kernel, edge2_data, face2_data, vertex2_data);
	CGAL_MESH(Kernel, vertex3_data, edge3_data, face3_data);

	//arrangement2 arr;
	polyhedron3 mesh;

	simple_geometry() {}

	simple_geometry(geometry::polyhedron3& mesh_)
	{
		mesh_builder b(/*on_error,*/ mesh_);
		mesh.delegate(b);
	}

	typedef typename polyhedron3::HalfedgeDS HDS;
	class mesh_builder : public CGAL::Modifier_base<HDS>
	{
	public:
		geometry::polyhedron3& _mesh;
		face3_list* _result;
		mesh_builder(geometry::polyhedron3& mesh, face3_list* result = nullptr) :
			_mesh(mesh),
			_result(result)
		{
		}

		typedef CGAL::Polyhedron_incremental_builder_3<HDS> incremental_builder;
		void operator()(HDS& hds)
		{
			incremental_builder builder(hds, true);
			builder.begin_surface(_mesh.size_of_vertices(), _mesh.size_of_facets(), _mesh.size_of_halfedges());

			//collect vertices
			CGAL::Cartesian_converter<GeometryKernel, Kernel> cc;
			auto vertex_idx = 0;
			for (auto it = _mesh.vertices_begin(); it != _mesh.vertices_end(); it++)
			{
				auto v = it;
				auto vv = builder.add_vertex(cc(v->point()));
				v->data.index = vertex_idx++;
				vv->data = v->data;
			}

			int invalid_faces = 0;
			//add faces
			for (auto it = _mesh.facets_begin(); it != _mesh.facets_end(); it++)
			{
				auto f = it;

				//auto result_face = builder.begin_facet();
				std::map<int, geometry::edge3> edges;
				auto eit = f->facet_begin();
				auto end = eit;

				// check validity of new face
				std::vector<size_t> indices;
				CGAL_For_all(eit, end)
				{
					int index = eit->vertex()->data.index;
					indices.push_back(index);
					edges[index] = eit;
				}

				bool valid_face = true;
				try
				{
					valid_face = builder.test_facet(indices.begin(), indices.end());
				}
				catch (CGAL::Precondition_exception e)
				{
					valid_face = false;
				}

				if (!valid_face)
				{
					invalid_faces++;
					continue;
					// isolate the face
					std::cout << "isolating face\n";
					std::vector<size_t> new_indices;
					//int idx = 0;
					CGAL_For_all(eit, end)
					{
						auto v = eit->vertex();
						//auto oldindex = indices[idx++];
						auto old_index = v->data.index;

						auto vv = builder.add_vertex(cc(v->point()));
						int new_index = vertex_idx++;
						//v->data.index = new_index;
						vv->data = v->data;
						vv->data.index = new_index;

						new_indices.push_back(new_index);

						edges[new_index] = edges[(int)old_index];
					}

					indices = new_indices;
				}

				/*valid_face = builder.test_facet(indices.begin(), indices.end());
				if (!valid_face)
				{
					std::cout << "invalid face\n";
					continue;
				}*/

				auto result_face = builder.add_facet(indices.begin(), indices.end())->face();

				/*CGAL_For_all(eit, end)
				{
					auto v = eit->vertex();
					auto index = v->data.index;
					edges[index] = eit;
					builder.add_vertex_to_facet(index);
					if (builder.error())
					{
						// this should not happen
						//_on_error(context_errors::create(CONTEXT_ERROR_INVALID_FACE_ARR_MESH));
						throw std::logic_error("invalid face in simple geometry");
					}
				}

				builder.end_facet();*/

				result_face->data = f->data;

				// update edges data
				auto rit = result_face->facet_begin();
				auto rnd = rit;
				CGAL_For_all(rit, rnd)
				{
					auto index = rit->vertex()->data.index;
					auto he_original = edges[index];
					rit->data = he_original->data;
					rit->opposite()->data = he_original->opposite()->data;
				}

				if (_result)
					_result->push_back(result_face);
			}

			if (invalid_faces > 0)
				std::cout << invalid_faces << " invalid face in simple_geometry\n";

			builder.end_surface();
		}

	};
};

struct geometry_helpers
{
	CGAL_TYPES(GeometryKernel)
	CGAL_ARRANGEMENT(GeometryKernel, edge2_data, face2_data, vertex2_data)
	CGAL_MESH(GeometryKernel, vertex3_data, edge3_data, face3_data)

	geometry_ref _geom;
	vertex2 arr_init;
	vertex2 arr_curr;

	geometry_helpers(geometry_ref geom) : _geom(geom) {}

	void arr_start(point2 p);
	void arr_add(point2 p, int l);
	face2 arr_close(int l);
	void arr_save(std::string path);
	face3 f3(face2 f2);
	face2 f2(face3 f3);
};

struct shared_geometry
{
	shared_geometry(geometry_ref _g) :
		g(_g)
	{
	}

	shared_geometry(const shared_geometry& other) :
		g(other.g),
		faces2(other.faces2),
		faces3(other.faces3)
	{
	}

	geometry_ref g;
	geometry::face2_list faces2;
	geometry::face3_list faces3;
};

typedef std::shared_ptr<shared_geometry> shared_geometry_ref;

//default geometric types for this context
#define DEFAULT_CGAL_TYPES() CGAL_TYPES(GeometryKernel)
#define DEFAULT_ARRANGEMENT_TYPES() typedef geometry::arrangement2 arrangement2; CGAL_ARRANGEMENT_TYPES(arrangement2)
#define DEFAULT_MESH_TYPES() typedef geometry::polyhedron3 polyhedron3; CGAL_MESH_TYPES(polyhedron3)
#define DEFAULT_UTILS() CGAL_UTILS(GeometryKernel, geometry::arrangement2, geometry::polyhedron3)

// beziers
#define DEFAULT_BEZIER_UTILS()\
  typedef bezier_utils_t<geometry::Kernel, geometry::arrangement2, geometry::polyhedron3> bezier_utils;
#define BEZIER_UTILS(K/*, A, P*/)\
  typedef bezier_utils_t<K, geometry::arrangement2, geometry::polyhedron3> bezier_utils;


//td: we exact by default now, refactor users of these types.
//default exact types
#define CGAL_EXACT_TYPES2() \
  typedef cgal2_types<ExactKernel>::point       exact_point2;\
  typedef cgal2_types<ExactKernel>::curve       exact_curve2;\
  typedef cgal2_types<ExactKernel>::segment     exact_segment2;\
  typedef cgal2_types<ExactKernel>::vec2        exact_vec2;\
  typedef cgal2_types<ExactKernel>::dir2        exact_dir2;\
  typedef cgal2_types<ExactKernel>::line        exact_line2;\
  typedef cgal2_types<ExactKernel>::point2_list exact_point2_list;\
  typedef cgal2_types<ExactKernel>::vec2_list   exact_vec2_list;\
  typedef cgal2_types<ExactKernel>::transform2  exact_transform2;\

#define CGAL_EXACT_ARRANGEMENT() \
  typedef cgal2_arrangement<ExactKernel, edge2_data, face2_data, vertex2_data>::arrangement      exact_arrangement2;\
  typedef cgal2_arrangement<ExactKernel, edge2_data, face2_data, vertex2_data>::edge2_circulator exact_edge2_circulator;\
  typedef cgal2_arrangement<ExactKernel, edge2_data, face2_data, vertex2_data>::edge2_iterator   exact_edge2_iterator;\
  typedef cgal2_arrangement<ExactKernel, edge2_data, face2_data, vertex2_data>::face2_iterator   exact_face2_iterator;\
  typedef cgal2_arrangement<ExactKernel, edge2_data, face2_data, vertex2_data>::vertex2_iterator exact_vertex2_iterator;\
  typedef cgal2_arrangement<ExactKernel, edge2_data, face2_data, vertex2_data>::vertex2          exact_vertex2;\
  typedef cgal2_arrangement<ExactKernel, edge2_data, face2_data, vertex2_data>::edge2            exact_edge2;\
  typedef cgal2_arrangement<ExactKernel, edge2_data, face2_data, vertex2_data>::const_edge2      exact_const_edge2;\
  typedef cgal2_arrangement<ExactKernel, edge2_data, face2_data, vertex2_data>::face2            exact_face2;\
  typedef cgal2_arrangement<ExactKernel, edge2_data, face2_data, vertex2_data>::const_face2      exact_const_face2; \
  typedef std::vector<exact_vertex2>                                                             exact_vertex2_list;\
  typedef std::vector<exact_edge2>                                                               exact_edge2_list;\
  typedef std::vector<exact_face2>                                                               exact_face2_list;\
  typedef std::vector<exact_segment2>                                                            exact_segment2_list;

#define CGAL_INEXACT_TYPES2() \
  typedef cgal2_types<EpickKernel>::point       inexact_point2;\
  typedef cgal2_types<EpickKernel>::curve       inexact_curve2;\
  typedef cgal2_types<EpickKernel>::segment     inexact_segment2;\
  typedef cgal2_types<EpickKernel>::vec2        inexact_vec2;\
  typedef cgal2_types<EpickKernel>::dir2        inexact_dir2;\
  typedef cgal2_types<EpickKernel>::line        inexact_line2;\
  typedef cgal2_types<EpickKernel>::point2_list inexact_point2_list;\
  typedef cgal2_types<EpickKernel>::vec2_list   inexact_vec2_list;\
  typedef cgal2_types<EpickKernel>::transform2  inexact_transform2;\

#define CGAL_INEXACT_ARRANGEMENT() \
  typedef cgal2_arrangement<EpickKernel, edge2_data, face2_data, vertex2_data>::arrangement      inexact_arrangement2;\
  typedef cgal2_arrangement<EpickKernel, edge2_data, face2_data, vertex2_data>::edge2_circulator inexact_edge2_circulator;\
  typedef cgal2_arrangement<EpickKernel, edge2_data, face2_data, vertex2_data>::edge2_iterator   inexact_edge2_iterator;\
  typedef cgal2_arrangement<EpickKernel, edge2_data, face2_data, vertex2_data>::face2_iterator   inexact_face2_iterator;\
  typedef cgal2_arrangement<EpickKernel, edge2_data, face2_data, vertex2_data>::vertex2_iterator inexact_vertex2_iterator;\
  typedef cgal2_arrangement<EpickKernel, edge2_data, face2_data, vertex2_data>::vertex2          inexact_vertex2;\
  typedef cgal2_arrangement<EpickKernel, edge2_data, face2_data, vertex2_data>::const_vertex2    inexact_const_vertex2;\
  typedef cgal2_arrangement<EpickKernel, edge2_data, face2_data, vertex2_data>::edge2            inexact_edge2;\
  typedef cgal2_arrangement<EpickKernel, edge2_data, face2_data, vertex2_data>::const_edge2      inexact_const_edge2;\
  typedef cgal2_arrangement<EpickKernel, edge2_data, face2_data, vertex2_data>::face2            inexact_face2;\
  typedef cgal2_arrangement<EpickKernel, edge2_data, face2_data, vertex2_data>::const_face2      inexact_const_face2; \
  typedef std::vector<inexact_vertex2>                                                           inexact_vertex2_list;\
  typedef std::vector<inexact_edge2>                                                             inexact_edge2_list;\
  typedef std::vector<inexact_face2>                                                             inexact_face2_list;\

#define CGAL_INEXACT_TYPES3() \
  typedef typename cgal3_types<EpickKernel>::point3      inexact_point3;\
  typedef typename cgal3_types<EpickKernel>::vec3        inexact_vec3;\
  typedef typename cgal3_types<EpickKernel>::dir3        inexact_dir3;\
  typedef typename cgal3_types<EpickKernel>::plane3      inexact_plane3;\
  typedef typename cgal3_types<EpickKernel>::triangle3   inexact_triangle3;\
  typedef typename cgal3_types<EpickKernel>::segment3    inexact_segment3;\
  typedef typename cgal3_types<EpickKernel>::line3       inexact_line3;\
  typedef typename cgal3_types<EpickKernel>::ray3        inexact_ray3;\
  typedef typename cgal3_types<EpickKernel>::transform3  inexact_transform3; \
  typedef typename cgal3_types<EpickKernel>::point3_list inexact_point3_list; \
  typedef typename cgal3_types<EpickKernel>::plane3_list inexact_plane3_list; \


#define CGAL_EXACT_TYPES3() \
  typedef typename cgal3_types<ExactKernel>::point3      exact_point3;\
  typedef typename cgal3_types<ExactKernel>::vec3        exact_vec3;\
  typedef typename cgal3_types<ExactKernel>::dir3        exact_dir3;\
  typedef typename cgal3_types<ExactKernel>::plane3      exact_plane3;\
  typedef typename cgal3_types<ExactKernel>::triangle3   exact_triangle3;\
  typedef typename cgal3_types<ExactKernel>::segment3    exact_segment3;\
  typedef typename cgal3_types<ExactKernel>::line3       exact_line3;\
  typedef typename cgal3_types<ExactKernel>::ray3        exact_ray3;\
  typedef typename cgal3_types<ExactKernel>::transform3  exact_transform3; \
  typedef typename cgal3_types<ExactKernel>::point3_list exact_point3_list; \
  typedef typename cgal3_types<ExactKernel>::plane3_list exact_plane3_list; \

#define CGAL_INEXACT_MESH() \
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::polyhedron          inexact_polyhedron3; \
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::HDS3D               inexact_HDS3D; \
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::face3               inexact_face3;\
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::const_face3         inexact_const_face3;\
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::edge3               inexact_edge3;\
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::const_edge3         inexact_const_edge3;\
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::vertex3             inexact_vertex3;\
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::const_vertex3       inexact_const_vertex3;\
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::incremental_builder inexact_incremental_builder;\
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::vertex3_list        inexact_vertex3_list;\
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::edge3_list          inexact_edge3_list;\
  typedef cgal3_mesh<EpickKernel, vertex3_data, edge3_data, face3_data>::face3_list          inexact_face3_list;

// for using face2 and face3 as keys in std::map and std::set
struct face_comparer {
	bool operator()(const geometry::face2& f1, const geometry::face2& f2) const {
		if (f1->data().id < 0 || f2->data().id < 0)
			std::cerr << "ERROR: face2d id not set *******************\n";
		return f1->data().id < f2->data().id;
	}

	bool operator()(const geometry::face3& f1, const geometry::face3& f2) const {
		if (f1->data.id < 0 || f2->data.id < 0)
			std::cerr << "ERROR: face2d id not set *******************\n";
		return f1->data.id < f2->data.id;
	}
};
