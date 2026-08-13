
#pragma once

#include <CGAL/Cartesian.h>
#include <CGAL/Arr_segment_traits_2.h>
#include <CGAL/Arrangement_2.h>
#include <CGAL/Arrangement_with_history_2.h>
#include <CGAL/Arr_extended_dcel.h>
#include <CGAL/bounding_box.h>
#include <CGAL/Polygon_2.h>

#include <CGAL/Polyhedron_3.h>
#include <CGAL/Polyhedron_incremental_builder_3.h>
#include <CGAL/Bbox_3.h>

#include <CGAL/aff_transformation_tags.h>
#include <CGAL/Aff_transformation_2.h>
#include <CGAL/Gmpq.h>
#include <CGAL/Nef_polyhedron_3.h>
#include <CGAL/IO/Polyhedron_iostream.h>
#include <CGAL/IO/Nef_polyhedron_iostream_3.h>

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Exact_circular_kernel_2.h>

#include <vector>

/*#if defined(__GNUC__)
//#define null nullptr
#define null ((void *)0)
#else
#define null 0
#endif*/

// fake labels
// consider faces with this label as unbounded
const int LABEL_UNBOUNDED_IDX = -1000;
// to mark the faces that go the layout builder
const int LABEL_LAYOUT_IDX = -1001;

const int BAD_VERTEX_ID = 1000 * 1000000;

typedef CGAL::Cartesian<double>							  DoubleKernel;
typedef CGAL::Simple_cartesian<double>					  SimpleKernel;
typedef CGAL::Exact_predicates_exact_constructions_kernel ExactKernel;
typedef CGAL::Epick										  EpickKernel;
typedef CGAL::Exact_circular_kernel_2                     CircularKernel;

//typedef CGAL::Epick GeometryKernel;
typedef ExactKernel GeometryKernel;


template <typename K>
struct cgal2_types
  {
    typedef CGAL::Arr_segment_traits_2<K>                               arr_traits;
    typedef typename CGAL::Arr_segment_traits_2<K>::Point_2             point;
    typedef typename CGAL::Arr_segment_traits_2<K>::X_monotone_curve_2  curve;
    typedef typename CGAL::Arr_segment_traits_2<K>::Segment_2           segment;
    typedef typename CGAL::Arr_segment_traits_2<K>::Vector_2            vec2;
    typedef typename CGAL::Arr_segment_traits_2<K>::Direction_2         dir2;
    typedef typename CGAL::Arr_segment_traits_2<K>::Ray_2               ray2;
    typedef typename CGAL::Arr_segment_traits_2<K>::Circle_2            circle2;
    typedef typename CGAL::Arr_segment_traits_2<K>::Direction_2         direction2;
    typedef typename CGAL::Arr_segment_traits_2<K>::Line_2              line;
    typedef typename CGAL::Aff_transformation_2<K>                      transform2;

    typedef typename std::vector<point> point2_list;
	typedef typename std::vector<segment> segment2_list;
	typedef typename std::vector<vec2> vec2_list;
};

template <typename K>
struct cgal3_types
  {
    typedef typename K::Point_3		  point3;
    typedef typename K::Vector_3      vec3;
	typedef typename K::Direction_3   dir3;
    typedef typename K::Plane_3       plane3;
    typedef typename K::Triangle_3    triangle3;
    typedef typename K::Segment_3     segment3;
    typedef typename K::Line_3        line3;
    typedef typename K::Ray_3         ray3;

    typedef typename CGAL::Aff_transformation_3<K> transform3;
    typedef typename std::vector<point3>           point3_list;
    typedef typename std::vector<plane3>           plane3_list;
	typedef typename std::vector<segment3>         segment3_list;
  };

#define DECLARE_TYPES(K) \
  typedef typename cgal2_types<K>::point       point2;\
  typedef typename cgal2_types<K>::curve       curve2;\
  typedef typename cgal2_types<K>::segment     segment2;\
  typedef typename cgal2_types<K>::vec2        vec2;\
  typedef typename cgal2_types<K>::ray2        ray2;\
  typedef typename cgal2_types<K>::dir2        dir2;\
  typedef typename cgal2_types<K>::line        line2;\
  typedef typename cgal2_types<K>::circle2     circle2;\
  typedef typename cgal2_types<K>::direction2  direction2;\
  typedef typename cgal2_types<K>::point2_list point2_list;\
  typedef typename cgal2_types<K>::segment2_list segment2_list;\
  typedef typename cgal2_types<K>::transform2  transform2;\
  typedef typename cgal3_types<K>::point3      point3;\
  typedef typename cgal3_types<K>::vec3        vec3;\
  typedef typename cgal3_types<K>::dir3        dir3;\
  typedef typename cgal3_types<K>::plane3      plane3;\
  typedef typename cgal3_types<K>::triangle3   triangle3;\
  typedef typename cgal3_types<K>::segment3    segment3;\
  typedef typename cgal3_types<K>::line3       line3;\
  typedef typename cgal3_types<K>::ray3        ray3;\
  typedef typename cgal3_types<K>::transform3  transform3; \
  typedef typename cgal3_types<K>::point3_list point3_list; \
  typedef typename cgal3_types<K>::plane3_list plane3_list; \
  typedef typename cgal3_types<K>::segment3_list segment3_list; \

#define DECLARE_TYPES2(K) \
  typedef typename cgal2_types<K>::point       point2;\
  typedef typename cgal2_types<K>::curve       curve2;\
  typedef typename cgal2_types<K>::segment     segment2;\
  typedef typename cgal2_types<K>::vec2        vec2;\
  typedef typename cgal2_types<K>::vec2        ray2;\
  typedef typename cgal2_types<K>::dir2        dir2;\
  typedef typename cgal2_types<K>::line        line2;\
  typedef typename cgal2_types<K>::circle2     circle2;\
  typedef typename cgal2_types<K>::direction2  direction2;\
  typedef typename cgal2_types<K>::point2_list point2_list;\
  typedef typename cgal2_types<K>::segment2_list segment2_list;\
  typedef typename cgal2_types<K>::vec2_list   vec2_list;\
  typedef typename cgal2_types<K>::transform2  transform2;\

#define DECLARE_TYPES3(K) \
  typedef typename cgal3_types<K>::point3      point3;\
  typedef typename cgal3_types<K>::vec3        vec3;\
  typedef typename cgal3_types<K>::dir3        dir3;\
  typedef typename cgal3_types<K>::plane3      plane3;\
  typedef typename cgal3_types<K>::triangle3   triangle3;\
  typedef typename cgal3_types<K>::segment3    segment3;\
  typedef typename cgal3_types<K>::line3       line3;\
  typedef typename cgal3_types<K>::ray3        ray3;\
  typedef typename cgal3_types<K>::transform3  transform3; \
  typedef typename cgal3_types<K>::point3_list point3_list; \
  typedef typename cgal3_types<K>::plane3_list plane3_list; \
  typedef typename cgal3_types<K>::segment3_list segment3_list; \

#define CGAL_TYPES(K) \
  typedef cgal2_types<K>::point       point2;\
  typedef cgal2_types<K>::curve       curve2;\
  typedef cgal2_types<K>::segment     segment2;\
  typedef cgal2_types<K>::vec2        vec2;\
  typedef cgal2_types<K>::ray2        ray2;\
  typedef cgal2_types<K>::dir2        dir2;\
  typedef cgal2_types<K>::line        line2;\
  typedef cgal2_types<K>::circle2     circle2;\
  typedef cgal2_types<K>::direction2  direction2;\
  typedef cgal2_types<K>::point2_list point2_list;\
  typedef cgal2_types<K>::segment2_list segment2_list;\
  typedef cgal2_types<K>::vec2_list   vec2_list;\
  typedef cgal2_types<K>::transform2  transform2;\
  typedef cgal3_types<K>::point3      point3;\
  typedef cgal3_types<K>::vec3        vec3;\
  typedef cgal3_types<K>::dir3        dir3;\
  typedef cgal3_types<K>::plane3      plane3;\
  typedef cgal3_types<K>::triangle3   triangle3;\
  typedef cgal3_types<K>::segment3    segment3;\
  typedef cgal3_types<K>::line3       line3;\
  typedef cgal3_types<K>::ray3        ray3;\
  typedef cgal3_types<K>::transform3  transform3; \
  typedef cgal3_types<K>::point3_list point3_list; \
  typedef cgal3_types<K>::plane3_list plane3_list; \
  typedef cgal3_types<K>::segment3_list segment3_list; \

#define CGAL_TYPES2(K) \
  typedef cgal2_types<K>::point       point2;\
  typedef cgal2_types<K>::curve       curve2;\
  typedef cgal2_types<K>::segment     segment2;\
  typedef cgal2_types<K>::vec2        vec2;\
  typedef cgal2_types<K>::ray2        ray2;\
  typedef cgal2_types<K>::dir2        dir2;\
  typedef cgal2_types<K>::line        line2;\
  typedef cgal2_types<K>::circle2     circle2;\
  typedef cgal2_types<K>::direction2  direction2;\
  typedef cgal2_types<K>::point2_list point2_list;\
  typedef cgal2_types<K>::segment2_list segment2_list;\
  typedef cgal2_types<K>::vec2_list   vec2_list;\
  typedef cgal2_types<K>::transform2  transform2;\

#define CGAL_NAMED_TYPES2(K, T) \
  typedef cgal2_types<K>::point       T##_point2;\
  typedef cgal2_types<K>::curve       T##_curve2;\
  typedef cgal2_types<K>::segment     T##_segment2;\
  typedef cgal2_types<K>::vec2        T##_vec2;\
  typedef cgal2_types<K>::ray2        T##_ray2;\
  typedef cgal2_types<K>::dir2        T##_dir2;\
  typedef cgal2_types<K>::line        T##_line2;\
  typedef cgal2_types<K>::circle2     T##_circle2;\
  typedef cgal2_types<K>::direction2  T##_direction2;\
  typedef cgal2_types<K>::point2_list T##_point2_list;\
  typedef cgal2_types<K>::segment2_list T##_segment2_list;\
  typedef cgal2_types<K>::vec2_list   T##_vec2_list;\
  typedef cgal2_types<K>::transform2  T##_transform2;\

#define DECLARE_NAMED_TYPES2(K, T) \
  typedef typename cgal2_types<K>::point       T##_point2;\
  typedef typename cgal2_types<K>::curve       T##_curve2;\
  typedef typename cgal2_types<K>::segment     T##_segment2;\
  typedef typename cgal2_types<K>::vec2        T##_vec2;\
  typedef typename cgal2_types<K>::ray2        T##_ray2;\
  typedef typename cgal2_types<K>::dir2        T##_dir2;\
  typedef typename cgal2_types<K>::line        T##_line2;\
  typedef typename cgal2_types<K>::circle2     T##_circle2;\
  typedef typename cgal2_types<K>::direction2  T##_direction2;\
  typedef typename cgal2_types<K>::point2_list T##_point2_list;\
  typedef typename cgal2_types<K>::segment2_list T##_segment2_list;\
  typedef typename cgal2_types<K>::vec2_list   T##_vec2_list;\
  typedef typename cgal2_types<K>::transform2  T##_transform2;\

#define CGAL_TYPES3(K) \
  typedef cgal3_types<K>::point3      point3;\
  typedef cgal3_types<K>::vec3        vec3;\
  typedef cgal3_types<K>::dir3        dir3;\
  typedef cgal3_types<K>::plane3      plane3;\
  typedef cgal3_types<K>::triangle3   triangle3;\
  typedef cgal3_types<K>::segment3    segment3;\
  typedef cgal3_types<K>::line3       line3;\
  typedef cgal3_types<K>::ray3        ray3;\
  typedef cgal3_types<K>::transform3  transform3; \
  typedef cgal3_types<K>::point3_list point3_list; \
  typedef cgal3_types<K>::plane3_list plane3_list; \
  typedef cgal3_types<K>::segment3_list segment3_list; \

#define CGAL_NAMED_TYPES3(K, T) \
  typedef cgal3_types<K>::point3      T##_point3;\
  typedef cgal3_types<K>::vec3        T##_vec3;\
  typedef cgal3_types<K>::dir3        T##_dir3;\
  typedef cgal3_types<K>::plane3      T##_plane3;\
  typedef cgal3_types<K>::triangle3   T##_triangle3;\
  typedef cgal3_types<K>::segment3    T##_segment3;\
  typedef cgal3_types<K>::line3       T##_line3;\
  typedef cgal3_types<K>::ray3        T##_ray3;\
  typedef cgal3_types<K>::transform3  T##_transform3; \
  typedef cgal3_types<K>::point3_list T##_point3_list; \
  typedef cgal3_types<K>::plane3_list T##_plane3_list; \
  typedef cgal3_types<K>::segment3_list T##_segment3_list; \

#define DECLARE_NAMED_TYPES3(K, T) \
  typedef typename cgal3_types<K>::point3      T##_point3;\
  typedef typename cgal3_types<K>::vec3        T##_vec3;\
  typedef typename cgal3_types<K>::dir3        T##_dir3;\
  typedef typename cgal3_types<K>::plane3      T##_plane3;\
  typedef typename cgal3_types<K>::triangle3   T##_triangle3;\
  typedef typename cgal3_types<K>::segment3    T##_segment3;\
  typedef typename cgal3_types<K>::line3       T##_line3;\
  typedef typename cgal3_types<K>::ray3        T##_ray3;\
  typedef typename cgal3_types<K>::transform3  T##_transform3; \
  typedef typename cgal3_types<K>::point3_list T##_point3_list; \
  typedef typename cgal3_types<K>::plane3_list T##_plane3_list; \
  typedef typename cgal3_types<K>::segment3_list T##_segment3_list; \

template <typename K, typename E, typename F, typename P>
struct cgal2_arrangement
  {
    typedef CGAL::Arr_segment_traits_2<K>                 arr_traits;
    typedef CGAL::Arr_extended_dcel<arr_traits, P, E, F>  arr_dcel;
    typedef CGAL::Arrangement_2<arr_traits, arr_dcel>     arrangement;

    typedef typename CGAL::Arrangement_2<arr_traits, arr_dcel>::Ccb_halfedge_circulator edge2_circulator;
    typedef typename CGAL::Arrangement_2<arr_traits, arr_dcel>::Edge_iterator           edge2_iterator;
    typedef typename CGAL::Arrangement_2<arr_traits, arr_dcel>::Face_iterator           face2_iterator;
    typedef typename CGAL::Arrangement_2<arr_traits, arr_dcel>::Vertex_iterator         vertex2_iterator;

    typedef typename CGAL::Arrangement_2<arr_traits, arr_dcel>::Vertex_handle         vertex2;
	typedef typename CGAL::Arrangement_2<arr_traits, arr_dcel>::Vertex_const_handle   const_vertex2;
	typedef typename CGAL::Arrangement_2<arr_traits, arr_dcel>::Halfedge_handle       edge2;
    typedef typename CGAL::Arrangement_2<arr_traits, arr_dcel>::Halfedge_const_handle const_edge2;
    typedef typename CGAL::Arrangement_2<arr_traits, arr_dcel>::Face_handle           face2;
    typedef typename CGAL::Arrangement_2<arr_traits, arr_dcel>::Face_const_handle     const_face2;
  };

template <typename A>
struct cgal2_arrangement_types
  {
    typedef typename A::Ccb_halfedge_circulator edge2_circulator;
    typedef typename A::Edge_iterator           edge2_iterator;
    typedef typename A::Face_iterator           face2_iterator;
    typedef typename A::Vertex_iterator         vertex2_iterator;

    typedef typename A::Vertex_handle         vertex2;
	typedef typename A::Vertex_const_handle   const_vertex2;
	typedef typename A::Halfedge_handle       edge2;
    typedef typename A::Halfedge_const_handle const_edge2;
    typedef typename A::Face_handle           face2;
    typedef typename A::Face_const_handle     const_face2;
  };

template <typename K, typename V, typename E, typename F>
struct cgal3_mesh
{
  typedef CGAL::Point_3<K> Point;

  typedef V VertexData;
  typedef E EdgeData;
  typedef F FaceData;

  template <class Refs , class P>
  struct My_vertex : public CGAL::HalfedgeDS_vertex_base<Refs, CGAL::Tag_true, P> {
      VertexData data;

      //Constructors only call the parents'.
      My_vertex(): CGAL::HalfedgeDS_vertex_base<Refs, CGAL::Tag_true, P>(){}
      My_vertex(const Point& p): CGAL::HalfedgeDS_vertex_base<Refs, CGAL::Tag_true, P>(p) {}
  };

  typedef CGAL::Plane_3<K> Plane;

  template <class Refs , class Traits>
  struct My_face : public CGAL::HalfedgeDS_face_base<Refs , CGAL::Tag_true , Plane>
  {
      FaceData data;

      //Constructors only call the parents'.
      My_face(): CGAL::HalfedgeDS_face_base<Refs, CGAL::Tag_true, Plane>(Plane(0, 0, 0, 0)) {}
      My_face(const Plane& plane): CGAL::HalfedgeDS_face_base<Refs, CGAL::Tag_true, Plane>(plane) {}
  };

  template <class Refs, class Traits>
  struct My_edge : public CGAL::HalfedgeDS_halfedge_base<Refs,CGAL::Tag_true,CGAL::Tag_true,CGAL::Tag_true>
    {
      EdgeData data;

	    typedef CGAL::HalfedgeDS_halfedge_base<Refs,CGAL::Tag_true,CGAL::Tag_true,CGAL::Tag_true> Base;
	    typedef typename Base::Base_base Base_base;

	    My_edge() { }
    };

  //A new Polyhedron_items_3 type with the new vertex definition plugged.
  struct My_items : public CGAL::Polyhedron_items_3
  {
	template <class Refs, class Traits>
    struct Vertex_wrapper
	{
        typedef typename Traits::Point_3 Point;
        typedef My_vertex<Refs , Point> Vertex;
	};

	template < class Refs, class Traits>
	struct Halfedge_wrapper
    {
		typedef My_edge<Refs,Traits> Halfedge;
	};

    template <class Refs, class Traits>
    struct Face_wrapper
	{
		typedef typename Traits::Plane_3 Plane;
        typedef My_face<Refs , Plane> Face;
    };
  };

  typedef typename CGAL::Polyhedron_3<K, My_items>   polyhedron;
  typedef typename polyhedron::HalfedgeDS            HDS3D;
  typedef typename polyhedron::Facet_handle          face3;
  typedef typename polyhedron::Facet_const_handle    const_face3;
  typedef typename polyhedron::Halfedge_handle       edge3;
  typedef typename polyhedron::Halfedge_const_handle const_edge3;
  typedef typename polyhedron::Vertex_handle         vertex3;
  typedef typename polyhedron::Vertex_const_handle   const_vertex3;
  typedef typename polyhedron::Halfedge_around_facet_circulator   edge3_circulator;

  typedef CGAL::Polyhedron_incremental_builder_3<HDS3D> incremental_builder;

  typedef std::vector<vertex3> vertex3_list;
  typedef std::vector<edge3>   edge3_list;
  typedef std::vector<face3>   face3_list;
};

#define CGAL_MESH(K, V, E, F) \
  typedef cgal3_mesh<K, V, E, F>::polyhedron          polyhedron3; \
  typedef cgal3_mesh<K, V, E, F>::HDS3D               HDS3D; \
  typedef cgal3_mesh<K, V, E, F>::face3               face3;\
  typedef cgal3_mesh<K, V, E, F>::const_face3         const_face3;\
  typedef cgal3_mesh<K, V, E, F>::edge3               edge3;\
  typedef cgal3_mesh<K, V, E, F>::const_edge3         const_edge3;\
  typedef cgal3_mesh<K, V, E, F>::vertex3             vertex3;\
  typedef cgal3_mesh<K, V, E, F>::const_vertex3       const_vertex3;\
  typedef cgal3_mesh<K, V, E, F>::edge3_circulator    edge3_circulator;\
  typedef cgal3_mesh<K, V, E, F>::incremental_builder incremental_builder;\
  typedef cgal3_mesh<K, V, E, F>::vertex3_list        vertex3_list;\
  typedef cgal3_mesh<K, V, E, F>::edge3_list          edge3_list;\
  typedef cgal3_mesh<K, V, E, F>::face3_list          face3_list;\

#define DECLARE_MESH_TYPES(P) \
  typedef typename P::Facet_handle    face3;   \
  typedef typename P::Facet_const_handle const_face3;   \
  typedef typename P::Halfedge_handle edge3;   \
  typedef typename P::Vertex_handle   vertex3; \
  typedef typename P::Vertex_const_handle   const_vertex3; \
  typedef typename P::Halfedge_const_handle const_edge3; \
  typedef typename P::Halfedge_around_facet_circulator edge3_circulator; \
  typedef typename std::vector<vertex3>        vertex3_list;\
  typedef typename std::vector<edge3>          edge3_list;\
  typedef typename std::vector<face3>          face3_list;\

#define CGAL_MESH_TYPES(P) \
  typedef P::Facet_handle          face3;   \
  typedef P::Facet_const_handle    const_face3;   \
  typedef P::Halfedge_handle       edge3;   \
  typedef P::Halfedge_const_handle const_edge3; \
  typedef P::Vertex_handle         vertex3; \
  typedef P::Vertex_const_handle   const_vertex3; \
  typedef std::vector<vertex3>     vertex3_list;\
  typedef std::vector<edge3>       edge3_list;\
  typedef std::vector<face3>       face3_list;\

#define DECLARE_ARRANGEMENT(K, E, F, P) \
  typedef typename cgal2_arrangement<K, E, F, P>::arrangement     arrangement2;\
  typedef typename cgal2_arrangement<K, E, F, P>::edge2_circulator edge2_circulator;\
  typedef typename cgal2_arrangement<K, E, F, P>::edge2_iterator   edge2_iterator;\
  typedef typename cgal2_arrangement<K, E, F, P>::face2_iterator   face2_iterator;\
  typedef typename cgal2_arrangement<K, E, F, P>::vertex2_iterator vertex2_iterator;\
  typedef typename cgal2_arrangement<K, E, F, P>::vertex2          vertex2;\
  typedef typename cgal2_arrangement<K, E, F, P>::const_vertex2    const_vertex2;\
  typedef typename cgal2_arrangement<K, E, F, P>::edge2            edge2;\
  typedef typename cgal2_arrangement<K, E, F, P>::const_edge2      const_edge2;\
  typedef typename cgal2_arrangement<K, E, F, P>::face2            face2;\
  typedef typename cgal2_arrangement<K, E, F, P>::const_face2      const_face2; \
  typedef typename std::vector<vertex2>                            vertex2_list;\
  typedef typename std::vector<edge2>                              edge2_list;\
  typedef typename std::vector<face2>                              face2_list;\

#define CGAL_ARRANGEMENT(K, E, F, P) \
  typedef cgal2_arrangement<K, E, F, P>::arrangement      arrangement2;\
  typedef cgal2_arrangement<K, E, F, P>::edge2_circulator edge2_circulator;\
  typedef cgal2_arrangement<K, E, F, P>::edge2_iterator   edge2_iterator;\
  typedef cgal2_arrangement<K, E, F, P>::face2_iterator   face2_iterator;\
  typedef cgal2_arrangement<K, E, F, P>::vertex2_iterator vertex2_iterator;\
  typedef cgal2_arrangement<K, E, F, P>::vertex2          vertex2;\
  typedef cgal2_arrangement<K, E, F, P>::const_vertex2    const_vertex2;\
  typedef cgal2_arrangement<K, E, F, P>::edge2            edge2;\
  typedef cgal2_arrangement<K, E, F, P>::const_edge2      const_edge2;\
  typedef cgal2_arrangement<K, E, F, P>::face2            face2;\
  typedef cgal2_arrangement<K, E, F, P>::const_face2      const_face2; \
  typedef std::vector<vertex2>                            vertex2_list;\
  typedef std::vector<edge2>                              edge2_list;\
  typedef std::vector<face2>                              face2_list;\

#define DECLARE_ARRANGEMENT_TYPES(A) \
  typedef typename cgal2_arrangement_types<A>::edge2_circulator edge2_circulator;\
  typedef typename cgal2_arrangement_types<A>::edge2_iterator   edge2_iterator;\
  typedef typename cgal2_arrangement_types<A>::face2_iterator   face2_iterator;\
  typedef typename cgal2_arrangement_types<A>::vertex2_iterator vertex2_iterator;\
  typedef typename cgal2_arrangement_types<A>::vertex2          vertex2;\
  typedef typename cgal2_arrangement_types<A>::const_vertex2    const_vertex2;\
  typedef typename cgal2_arrangement_types<A>::edge2            edge2;\
  typedef typename cgal2_arrangement_types<A>::const_edge2      const_edge2;\
  typedef typename cgal2_arrangement_types<A>::face2            face2;\
  typedef typename cgal2_arrangement_types<A>::const_face2      const_face2; \
  typedef typename std::vector<vertex2>                         vertex2_list;\
  typedef typename std::vector<edge2>                           edge2_list;\
  typedef typename std::vector<face2>                           face2_list;\

#define DECLARE_NAMED_ARRANGEMENT_TYPES(A, T) \
  typedef typename cgal2_arrangement_types<A>::edge2_circulator T##_edge2_circulator;\
  typedef typename cgal2_arrangement_types<A>::edge2_iterator   T##_edge2_iterator;\
  typedef typename cgal2_arrangement_types<A>::face2_iterator   T##_face2_iterator;\
  typedef typename cgal2_arrangement_types<A>::vertex2_iterator T##_vertex2_iterator;\
  typedef typename cgal2_arrangement_types<A>::vertex2          T##_vertex2;\
  typedef typename cgal2_arrangement_types<A>::const_vertex2    T##_const_vertex2;\
  typedef typename cgal2_arrangement_types<A>::edge2            T##_edge2;\
  typedef typename cgal2_arrangement_types<A>::const_edge2      T##_const_edge2;\
  typedef typename cgal2_arrangement_types<A>::face2            T##_face2;\
  typedef typename cgal2_arrangement_types<A>::const_face2      T##_const_face2; \
  typedef typename std::vector<T##_vertex2>                     T##_vertex2_list;\
  typedef typename std::vector<T##_edge2>                       T##_edge2_list;\
  typedef typename std::vector<T##_face2>                       T##_face2_list;\

#define CGAL_ARRANGEMENT_TYPES(A) \
  typedef cgal2_arrangement_types<A>::edge2_circulator edge2_circulator;\
  typedef cgal2_arrangement_types<A>::edge2_iterator   edge2_iterator;\
  typedef cgal2_arrangement_types<A>::face2_iterator   face2_iterator;\
  typedef cgal2_arrangement_types<A>::vertex2_iterator vertex2_iterator;\
  typedef cgal2_arrangement_types<A>::vertex2          vertex2;\
  typedef cgal2_arrangement_types<A>::const_vertex2    const_vertex2;\
  typedef cgal2_arrangement_types<A>::edge2            edge2;\
  typedef cgal2_arrangement_types<A>::const_edge2      const_edge2;\
  typedef cgal2_arrangement_types<A>::face2            face2;\
  typedef cgal2_arrangement_types<A>::const_face2      const_face2;\
  typedef std::vector<vertex2>                         vertex2_list;\
  typedef std::vector<edge2>                           edge2_list;\
  typedef std::vector<face2>                           face2_list;
