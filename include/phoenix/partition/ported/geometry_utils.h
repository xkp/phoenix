#pragma once

#include <CGAL/Straight_skeleton_2.h>

#include "phoenix/partition/ported/geometry_constants.h"
#include "phoenix/partition/ported/geometry_types.h"

#include <CGAL/squared_distance_3.h>
#include <CGAL/Bbox_2.h>

#ifdef DEBUG
	#define PRINT_ENABLED
#else
	#define PRINT_ENABLED
#endif


template <typename K, typename A, typename P>
struct geometry_utils
{
	DECLARE_TYPES(K)
	DECLARE_MESH_TYPES(P)
	DECLARE_ARRANGEMENT_TYPES(A)

	typedef P polyhedron3;

	static vec2 normal(segment2& s)
	{
		vec2 res(s.source(), s.target());
		normalize(res);
		return res;
	}

	static vec2 normal(point2& p1, point2& p2)
	{
		vec2 res(p1, p2);
		normalize(res);
		return res;
	}

	static vec2 normal(edge2 he)
	{
		vec2 res(he->source()->point(), he->target()->point());
		normalize(res);
		return res;
	}

	static double line_eval(line2& line, point2 pt)
	{
		//return CGAL::to_double(line.a()*pt.x() + line.b()*pt.y() + line.c());
		return CGAL::to_double(line.a())*CGAL::to_double(pt.x()) + CGAL::to_double(line.b())*CGAL::to_double(pt.y()) + CGAL::to_double(line.c());
	}

	static double line_signed_distance(line2& line, point2 pt)
	{
		return line_eval(line, pt) / CGAL::sqrt(CGAL::to_double(line.a() * line.a() + line.b() * line.b()));
	}

	static vec2 rotate_unit(double angle)
	{
		const double thresold = 0.05*M_PI / 180.0;
		int sign = angle >= 0 ? 1 : -1;
		double angle_ = sign*angle + thresold;
		auto res = fmod(angle_, M_PI_2);
		if (res >= 0 && res < thresold * 2) {
			int m = sign * (((int)trunc(angle_ / M_PI_2)) & 3);
			if (m < 0)
				m += 4;

			vec2 dirs[] = { vec2(1, 0), vec2(0, 1), vec2(-1, 0), vec2(0, -1) };
			return dirs[m];
		}
		else
			return vec2(1, 0).transform(transform2(CGAL::ROTATION, sin(angle), cos(angle)));
	}

	static double unit_angle(vec2 vec)
	{
		normalize(vec);
		return std::acos(vec2(1, 0) * vec);
	}

	static void normalize(vec2& v)
	{
		double sl = CGAL::to_double(v.squared_length());
		if (sl > 1e-5)
			v = v/CGAL::sqrt(sl);
	}

	static void normalize(vec3& v)
	{
		v = v/CGAL::sqrt(CGAL::to_double(v.squared_length()));
	}

	static vec3 safe_normalize(const vec3 &v)
	{
		double len = CGAL::sqrt(CGAL::to_double(v.squared_length()));

		if (len == 0.0)
			return v;

		return v / len;
	}

	static double sign(double value)
	{
		return (value > 0.0) ? 1.0 : (value < 0.0 ? -1.0 : 0.0);
	}

	static vec2 segment_normalx(point2 p1, point2 p2)
	{
		auto dx = p2.x() - p1.x();
		auto dy = p2.y() - p1.y();
		return vec2(-dy, dx);
	}

	static vec2 segment_normalized(point2 p1, point2 p2)
	{
		vec2 result(p1, p2);
		normalize(result);
		return result;
	}

	static vec2 edge_normalized(edge2 e)
	{
		return segment_normalized(e->source()->point(), e->target()->point());
	}

	static vec3 segment_normalized(point3 p1, point3 p2)
	{
		vec3 result(p1, p2);
		normalize(result);
		return result;
	}

	static vec3 edge_normalized(edge3 e)
	{
		return segment_normalized(e->prev()->vertex()->point(), e->vertex()->point());
	}

	static vec3 face_normal(const face3 f)
	{
		if (f == nullptr)
		{
			return CGAL::NULL_VECTOR;
		}

		//DebugTimer::generic1.start();
		vec3 normal = CGAL::NULL_VECTOR;
		typename polyhedron3::Halfedge_around_facet_circulator he  = f->facet_begin();
		typename polyhedron3::Halfedge_around_facet_circulator end = he;

		vertex3_list vertices;
		CGAL_For_all(he,end)
		{
		  vertices.push_back(he->vertex());
		}

		size_t sz = vertices.size();
		for(size_t i = 0; i < sz; i++)
		  {
			point3& current = vertices[i]->point();
			point3& next = vertices[(i + 1)%sz]->point();

			typename K::FT x = (current.y() - next.y())*(current.z() + next.z());
			typename K::FT y = (current.z() - next.z())*(current.x() + next.x());
			typename K::FT z = (current.x() - next.x())*(current.y() + next.y());

		  //Set Normal.x to Sum of Normal.x and (multiply (Current.y minus Next.y) by (Current.z plus Next.z))
		  //Set Normal.y to Sum of Normal.y and (multiply (Current.z minus Next.z) by (Current.x plus Next.x))
		  //Set Normal.z to Sum of Normal.z and (multiply (Current.x minus Next.x) by (Current.y plus Next.y))

			normal = normal + vec3(x, y, z);
		  }

		//td: get rid of double precision
		double res_len = std::sqrt(CGAL::to_double(normal * normal));
		//if (res_len > 0.0001) //avoid degenerates
		if (res_len > 0.000001) //avoid degenerates
		{
			normal = normal / res_len;

			//precision problems
			typename K::FT x = normal.x();
			typename K::FT y = normal.y();
			typename K::FT z = normal.z();

			if (CGAL::abs(x) < 0.0000001) //td: !!!!
				x = 0;

			if (CGAL::abs(y) < 0.0000001)
				y = 0;

			if (CGAL::abs(z) < 0.0000001)
				z = 0;

			//DebugTimer::generic1.pause();
			return vec3(x, y, z);
		}

		//DebugTimer::generic1.pause();
		return CGAL::NULL_VECTOR;
	}

	static vec3 face_orthogonal(const face3& f)
	{
		vec3 normal = CGAL::NULL_VECTOR;
		typename polyhedron3::Halfedge_around_facet_circulator he = f->facet_begin();
		typename polyhedron3::Halfedge_around_facet_circulator end = he;

		CGAL_For_all(he, end)
		{
			point3& current = he->vertex()->point();
			point3& next = he->next()->vertex()->point();

			typename K::FT x = (current.y() - next.y())*(current.z() + next.z());
			typename K::FT y = (current.z() - next.z())*(current.x() + next.x());
			typename K::FT z = (current.x() - next.x())*(current.y() + next.y());

			normal = normal + vec3(x, y, z);
		}

		//td: get rid of double precision
		double res_len2 = CGAL::to_double(normal.squared_length());
		if (res_len2 > 0.000001*0.000001) //avoid degenerates
		{
			//precision problems
			typename K::FT x = normal.x();
			typename K::FT y = normal.y();
			typename K::FT z = normal.z();

			if (CGAL::abs(x) < 0.0000001) //td: !!!!
				x = 0;

			if (CGAL::abs(y) < 0.0000001)
				y = 0;

			if (CGAL::abs(z) < 0.0000001)
				z = 0;

			return vec3(x, y, z);
		}

		return CGAL::NULL_VECTOR;
	}

	static plane3 face_plane(const face3& f)
	{
		vec3 normal = face_normal(f);
		return plane3(f->facet_begin()->vertex()->point(), normal);
	}

	static plane3 null_plane()
	{
		return plane3(0, 0, 0, 0);
	}

	static plane3 degen_plane()
	{
		return plane3(0, 0, 0, -1);
	}

	static plane3 face_plane2(face3& f)
	{
		if (f->plane() == null_plane())
		{
			vec3 n = face_normal(f);
			// use a special value for degenerated planes so they are not constantly recalculated
			if (n == CGAL::NULL_VECTOR)
				f->plane() = degen_plane();
			else
				f->plane() = plane3(f->facet_begin()->vertex()->point(), n);
		}

		return f->plane();
	}

	static bool is_concave(const face2 f)
	{
		CGAL::Cartesian_converter<GeometryKernel, DoubleKernel> converter;

		auto eit = f->outer_ccb();
		auto end = eit;
		CGAL_For_all(eit, end)
		{
			auto pp = converter(eit->prev()->target()->point());
			auto p = converter(eit->target()->point());
			auto pn = converter(eit->next()->target()->point());

			auto l = CGAL::Line_2<DoubleKernel>(pp, pn);

			if (l.has_on_positive_side(p) && CGAL::squared_distance(l, p) > 1e-8)
			{
				return true; //is concave
			}
		}

		return false;
	}

	static size_t memory_size(const point2& p)
	{
		return p.x().exact().size() + p.y().exact().size();
	}

	static size_t memory_size(const point3& p)
	{
		return p.x().exact().size() + p.y().exact().size() + p.z().exact().size();
	}

	static size_t memory_size(const plane3& p)
	{
		return p.a().exact().size() + p.b().exact().size() + p.c().exact().size() + p.d().exact().size();
	}

	static size_t memory_size(const face3& f)
	{
		size_t size = 0;
		edge3_circulator eit = f->facet_begin();
		edge3_circulator end = eit;
		CGAL_For_all(eit, end)
		{
			size += memory_size(eit->vertex()->point());
		}

		return size;
	}

	static void print_memory(P& mesh, const std::string& message)
	{
		size_t size = 0;
		size_t size_planes = 0;
		/*for (auto fit = mesh.facets_begin(); fit != mesh.facets_end(); fit++)
		{
			//fl.push_back(fit);
		}*/

		for (auto pit = mesh.planes_begin(); pit != mesh.planes_end(); pit++)
		{
			size_planes += memory_size(*pit);
		}

		for (auto vit = mesh.vertices_begin(); vit != mesh.vertices_end(); vit++)
		{
			size += memory_size(vit->point());
		}

		std::cout << message << ", memory use: vertices = " << size << ", planes = " << size_planes << ", on " << mesh.size_of_vertices() << " vertices\n";
	}

	static plane3 trunc_to_double(const plane3& p)
	{
		return plane3(CGAL::to_double(p.a()), CGAL::to_double(p.b()), CGAL::to_double(p.c()), CGAL::to_double(p.d()));
	}

	static point3 trunc_to_double(const point3& p)
	{
		return point3(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
	}

	static bool is_corner(edge2 he, edge2& corner_edge, point2& corner_pt)
	{
		if (!CGAL::collinear(he->prev()->source()->point(), he->source()->point(), he->target()->point()))
		  {
			corner_edge = he->prev();
			corner_pt   = he->source()->point();
			return true;
		  }

		if (!CGAL::collinear(he->source()->point(), he->target()->point(), he->next()->target()->point()))
		  {
			corner_edge = he;
			corner_pt   = he->target()->point();
			return true;
		  }

		return false;
	}

	static vec2 get_bisector(edge2 he1, edge2 he2)
	{
		vec2 n1, n2;
		if (he1->prev() == he2)
		  {
			n1 = -normal(he1);
			n2 = normal(he2);
		  }
		else
		  {
			n1 = normal(he1);
			n2 = -normal(he2);
		  }

		vec2 result = n1 + n2;
		normalize(result);
		return result;
	}

	static vec2 get_bisector(vec2 v1, vec2 v2)
	{
		vec2 result = v1 + v2;
		normalize(result);
		return result;
	}

	static double distance(point2& p1, point2& p2)
	{
		vec2 vv(p1, p2);
		return std::sqrt(CGAL::to_double(vv.squared_length()));
	}

	static double distance(const point2& p1, const point2& p2)
	{
		vec2 vv(p1, p2);
		return std::sqrt(CGAL::to_double(vv.squared_length()));
	}

	static double distance(segment2& s)
	{
		vec2 vv(s.source(), s.target());
		return std::sqrt(CGAL::to_double(vv.squared_length()));
	}

	static double distance(const point3& p1, const point3& p2)
	{
		return CGAL::sqrt(CGAL::to_double(CGAL::squared_distance(p1, p2)));
	}

	static double distance(const edge3 he)
	{
		return CGAL::sqrt(CGAL::to_double(CGAL::squared_distance(he->prev()->vertex()->point(), he->vertex()->point())));
	}

	static double distance2(const edge2 he)
	{
		return CGAL::to_double(CGAL::squared_distance(he->source()->point(), he->target()->point()));
	}

	static double distance2(const edge3 he)
	{
		return CGAL::to_double(CGAL::squared_distance(he->prev()->vertex()->point(), he->vertex()->point()));
	}

  static bool same_point(point2& p1, point2& p2, double tolerance = 1e-3)
  {
    return CGAL::squared_distance(p1, p2) < tolerance*tolerance;
  }

  static bool same_point(point3& p1, point3& p2, double tolerance = 1e-3)
  {
    return CGAL::squared_distance(p1, p2) < tolerance*tolerance;
  }

  static double angle_between(edge2 he1, edge2 he2, bool positive = false)
  {
    vec2 v1(he1->source()->point(), he1->target()->point());
    vec2 v2(he2->source()->point(), he2->target()->point());

    return angle_between(v1, v2);
  }

  static double angle_between(segment2 s1, segment2 s2, bool positive = false)
  {
    return angle_between(vec2(s1), vec2(s2));
  }

  static double angle_between(vec2 v1, vec2 v2, bool positive = false)
  {
    double result = std::atan2(CGAL::to_double(v2.y()), CGAL::to_double(v2.x())) - std::atan2(CGAL::to_double(v1.y()), CGAL::to_double(v1.x()));
	if (result > M_PI)
		result -= 2 * M_PI;
	else if (result < -M_PI)
		result += 2 * M_PI;

	if (positive && result < 0)
      result += 2*M_PI;

    return result;
  }

  static double angle_between(segment3 s1, segment3 s2)
  {
	  return angle_between(vec3(s1), vec3(s2));
  }

  static double angle_vertex(edge3 he)
  {
	  vec3 v1(he->vertex()->point(), he->opposite()->vertex()->point());
	  vec3 v2(he->vertex()->point(), he->next()->vertex()->point());

	  return angle_between(v1, v2);
  }

  static double angle_between(const vec3 &v1, const vec3 &v2)
  {
	  vec3 v1_norm = safe_normalize(v1);
	  vec3 v2_norm = safe_normalize(v2);
	  //vec3 v1_norm = v1;
	  //vec3 v2_norm = v2;

	  // P * Q = |P| * |Q| * cos a
	  double num = CGAL::to_double(v1_norm * v2_norm);
	  double den = CGAL::sqrt(CGAL::to_double((v1_norm.squared_length() * v2_norm.squared_length())));

	  if (den == 0.0)
		  return 0.0;

	  // this is stupid case that quotient is greater than one,
	  // and the result of cosine inverse is undefined
	  double res = num / den;
	  if (::abs(res) > 1.0)
		  res = sign(res);

	  return ::acos(res);
  }

  static double angle(vec2 v1)
  {
    return std::atan2(CGAL::to_double(v1.y()), CGAL::to_double(v1.x()));
  }

  static bool colinear(edge2 he, edge2 he1, double tolerance = 1e-3)
  {
    double ts = tolerance*tolerance;
    line2 l(he->source()->point(), he->target()->point());
    return CGAL::squared_distance(l, he1->source()->point()) < ts &&
           CGAL::squared_distance(l, he1->target()->point()) < ts;
  }

  static bool colinear(segment2& s1, segment2& s2, double tolerance = 1e-3)
  {
    double ts = tolerance*tolerance;
    line2 l(s1.source(), s2.source());
    return CGAL::squared_distance(l, s1.source()) < ts &&
           CGAL::squared_distance(l, s2.target()) < ts;
  }

  static bool colinear(const point2& v1, const point2& v2, const point2& v3, double tolerance = 1e-3)
  {
	  double ts = tolerance * tolerance;
	  line2 l(v1, v2);
	  if (l.is_degenerate())
		  return true;
	  return CGAL::squared_distance(l, v3->point()) < ts;
  }

  static bool colinear(vertex3 v1, vertex3 v2, vertex3 v3, double tolerance = 1e-3)
  {
	  double ts = tolerance*tolerance;
	  line3 l(v1->point(), v2->point());
	  if (l.is_degenerate())
		  return true;
	  return CGAL::squared_distance(l, v3->point()) < ts;
  }

  static bool colinear(edge3 he, edge3 he1, double tolerance = 1e-3)
  {
	  double ts = tolerance*tolerance;
	  geometry::line3 l(he->opposite()->vertex()->point(), he->vertex()->point());
	  if (l.is_degenerate())
	  {
		  l = line3(he1->opposite()->vertex()->point(), he1->vertex()->point());
		  if (l.is_degenerate())
			  return true;

		  std::swap(he, he1);
	  }

	  return CGAL::squared_distance(l, he1->opposite()->vertex()->point()) < ts &&
		  CGAL::squared_distance(l, he1->vertex()->point()) < ts;
  }

  static double eval(const plane3& pl, point3& pt)
  {
	  return CGAL::to_double(pl.a()*pt.x() + pl.b()*pt.y() + pl.c()*pt.z() + pl.d());
  }

  // asumes planes are builds with normal vectors
  static bool coplanar_n(const plane3& pl1, const plane3& pl2, double tolerance)
  {
	  return CGAL::abs(pl1.a() - pl2.a()) < tolerance
		  && CGAL::abs(pl1.b() - pl2.b()) < tolerance
		  && CGAL::abs(pl1.c() - pl2.c()) < tolerance
		  && CGAL::abs(pl1.d() - pl2.d()) < tolerance;
  }

  static bool coplanar_n(const plane3& pl1, face3& f, double tolerance)
  {
	  plane3 pl2 = face_plane(f);
	  return colinear_n(pl1, pl2, tolerance);
  }

  static face2_list faces(A& arr)
  {
	  face2_list fl;
	  for (auto fit = arr.faces_begin(); fit != arr.faces_end(); fit++)
	  {
		  if (is_unbounded_face(fit))
			  continue;

		  fl.push_back(fit);
	  }

	  return fl;
  }

  static face3_list faces(P& mesh)
  {
	  face3_list fl;
	  for (auto fit = mesh.facets_begin(); fit != mesh.facets_end(); fit++)
	  {
		  fl.push_back(fit);
	  }

	  return fl;
  }

  static void tag(A& arr, int tag)
  {
	  for (auto fit = arr.faces_begin(); fit != arr.faces_end(); fit++)
	  {
		  if (is_unbounded_face(fit))
			  continue;

		  fit->data().tag = tag;
	  }
  }

  static void tag(P& mesh, int tag)
  {
	  for (auto fit = mesh.facets_begin(); fit != mesh.facets_end(); fit++)
		  fit->data.tag = tag;
  }

  static void tag(edge2_list& items, int tag)
  {
    for(typename edge2_list::iterator it = items.begin(); it != items.end(); it++)
      (*it)->data().tag = tag;
  }

  static void tag(face2_list& items, int tag)
  {
    for(typename face2_list::iterator it = items.begin(); it != items.end(); it++)
      (*it)->data().tag = tag;
  }

  static void tag_vertexs(face2_list& items, int tag)
  {
	  for (auto f : items)
	  {
		  auto eit = f->outer_ccb();
		  auto end = eit;
		  CGAL_For_all(eit, end)
		  {
			  eit->target()->data().tag = tag;
		  }
	  }
  }

  static void tag_vertexs(face3_list& items, int tag)
  {
	  for (auto f: items)
	  {
		  auto eit = f->facet_begin();
		  auto end = eit;
		  CGAL_For_all(eit, end)
		  {
			  eit->vertex()->data.tag = tag;
		  }
	  }
  }

  static void tag_edges(A& arr, int tag)
  {
	  typename A::Halfedge_iterator eit;
	  CGAL_forall_halfedges(eit, arr)
	  {
		  eit->data().tag = tag;
	  }
  }

  static void tag_edges(face2_list& items, int tag)
  {
	  for (typename face2_list::iterator it = items.begin(); it != items.end(); it++)
	  {
		  auto eit = (*it)->outer_ccb();
		  auto end = eit;
		  CGAL_For_all(eit, end)
		  {
			  eit->data().tag = tag;
			  eit->twin()->data().tag = tag;
		  }
	  }
  }

  static void tag_edges(P& mesh, int tag)
  {
	  auto end = mesh.halfedges_end();
	  for (auto eit = mesh.halfedges_begin(); eit != end; eit++)
		  eit->data.tag = tag;
  }

  static void tag_edges(face3_list& items, int tag)
  {
	  for (typename face3_list::iterator it = items.begin(); it != items.end(); it++)
	  {
		  auto eit = (*it)->facet_begin();
		  auto end = eit;
		  CGAL_For_all(eit, end)
		  {
			  eit->data.tag = tag;
			  eit->opposite()->data.tag = tag;
		  }
	  }
  }

  static void tag(edge3_list& items, int tag)
  {
    for(typename edge3_list::iterator it = items.begin(); it != items.end(); it++)
      (*it)->data.tag = tag;
  }

  static plane3 sanitize(const plane3& p)
  {
    typedef typename K::FT FT;

    typename K::FT a = p.a();
    typename K::FT b = p.b();
    typename K::FT c = p.c();
    typename K::FT d = p.d();

    return plane3(
      CGAL::abs(a) < 1e-10? 0 : a,
      CGAL::abs(b) < 1e-10? 0 : b,
      CGAL::abs(c) < 1e-10? 0 : c,
      CGAL::abs(d) < 1e-10? 0 : d);
  }

  static void tag(face3_list& items, int tag)
  {
    for(typename face3_list::iterator it = items.begin(); it != items.end(); it++)
      (*it)->data.tag = tag;
  }

  template<typename Kernel>
  static point2 convert_point2(CGAL::Point_2<Kernel>& p )
  {
    return point2(CGAL::to_double(p.x()), CGAL::to_double(p.y()));
  }

  template<typename Kernel>
  static CGAL::Point_2<Kernel> convert_point2(point2& p )
  {
    return CGAL::Point_2<Kernel>(CGAL::to_double(p.x()), CGAL::to_double(p.y()));
  }

  template<typename Kernel>
  static point3 convert_point3(CGAL::Point_3<Kernel>& p )
  {
    return point3(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
  }


static void make_double(A& arr_)
{
	//CGAL::Cartesian_converter<geometry::Kernel, DoubleKernel> exact_to_double;
	//CGAL::Cartesian_converter<DoubleKernel, geometry::Kernel> double_to_exact;
	arrangement2 arr;

	std::map<arrangement2::Vertex_iterator, arrangement2::Vertex_iterator> vertex_map;
	// round vertex points to double
	arrangement2::Vertex_iterator vit;
	CGAL_forall_vertices(vit, arr_)
	{
		point2 p(CGAL::to_double(vit->point().x()), CGAL::to_double(vit->point().y()));
		auto v = arr.insert_in_face_interior(p, arr.unbounded_face());
		v->data() = vit->data();
		vertex_map[vit] = v;
	}

	auto eit = arr_.edges_begin();
	auto end = arr_.edges_end();
	CGAL_For_all(eit, end)
	{
		auto v1 = vertex_map[eit->source()];
		auto v2 = vertex_map[eit->target()];
		edge2 he = arr.insert_at_vertices(segment2(v1->point(), v2->point()), v1, v2);
		he->data() = eit->data();
		he->twin()->data() = eit->twin()->data();

		he->face()->data() = eit->face()->data();
		he->twin()->face()->data() = eit->twin()->face()->data();
	}

	arr_.assign(arr);
}

static edge2_list insert(A& arr, segment2 seg)
{
	class observer : public CGAL::Arr_observer<A>
	{
		edge2_list _edges;

	public:
		observer(A& arr) :
			CGAL::Arr_observer<A>(arr),
			_edges()
		{
		}

		edge2_list edges()
		{
			return _edges;
		}

		virtual void after_create_edge(typename A::Halfedge_handle e)
		{
			_edges.push_back(e);
		}

	};

	observer obs(arr);

	CGAL::insert(arr, seg);

	return obs.edges();
}

static point2 centroid(face2 f)
{
	int point_amount = 0;
	double x = 0.0, y = 0.0;
	
	edge2_circulator eit = f->outer_ccb();
	edge2_circulator end = eit;
	CGAL_For_all(eit, end)
	{
		auto pt = eit->target()->point();

		x += CGAL::to_double(pt.x());
		y += CGAL::to_double(pt.y());

		point_amount++;
	}

	return point2(x / point_amount, y / point_amount);
}

static point3 centroid(face3 f)
{
	int point_amount = 0;
	double x = 0.0, y = 0.0, z = 0.0;

	edge3_circulator eit = f->facet_begin();
	edge3_circulator end = eit;
	CGAL_For_all(eit, end)
	{
		auto pt = eit->vertex()->point();

		x += CGAL::to_double(pt.x());
		y += CGAL::to_double(pt.y());
		z += CGAL::to_double(pt.z());

		point_amount++;
	}

	return point3(x / point_amount, y / point_amount, z / point_amount);
}

static CGAL::Bbox_2 bounding_box(face2 f)
{
	CGAL::Bbox_2 box;
	edge2_circulator eit = f->outer_ccb();
	edge2_circulator end = eit;
	CGAL_For_all(eit, end)
	{
		auto pt = eit->target()->point();

		box += pt.bbox();
	}

	return box;
}

static point2 bbox_center(face2 f)
{
	CGAL::Bbox_2 box = bounding_box(f);
	return point2((box.xmin() + box.xmax()) / 2, (box.ymin() + box.ymax()) / 2);
}

/*static CGAL::Bbox_3 bounding_box(face3 f)
{
	CGAL::Bbox_3 box;
	edge3_circulator eit = f->facet_begin();
	edge3_circulator end = eit;
	CGAL_For_all(eit, end)
	{
		auto pt = eit->vertex()->point();

		box += pt.bbox();
	}

	return box;
}*/

static point3 bbox_center(face3 f);

static vec3 axis_from_planes(const vec3 &norm1, const vec3 &norm2)
{
	vec3 axis = CGAL::cross_product(norm1, norm2);
	axis = safe_normalize(axis);

	return axis;
}

static transform3 rotate_align(vec3 v1, vec3 v2)
{
	vec3 axis = CGAL::cross_product(v1, v2);
	const auto cosA = CGAL::scalar_product(v1, v2);
	const auto k = 1.0f / (1.0f + cosA);

	auto m00 = (axis.x() * axis.x() * k) + cosA;
	auto m01 = (axis.y() * axis.x() * k) - axis.z();
	auto m02 = (axis.z() * axis.x() * k) + axis.y();

	auto m10 = (axis.x() * axis.y() * k) + axis.z();
	auto m11 = (axis.y() * axis.y() * k) + cosA;
	auto m12 = (axis.z() * axis.y() * k) - axis.x();

	auto m20 = (axis.x() * axis.z() * k) - axis.y();
	auto m21 = (axis.y() * axis.z() * k) + axis.x();
	auto m22 = (axis.z() * axis.z() * k) + cosA;

	return transform3(
		m00, m01, m02, 
		m10, m11, m12, 
		m20, m21, m22, 1.0
	);
}

static double degrees_to_radian(double deg)
{
	return deg * M_PI / 180.0;
}

static double radian_to_degrees(double rad)
{
	return rad * 180.0 / M_PI;
}

static void print_angle(double angle, const std::string& message)
{
#ifdef PRINT_ENABLED
	  std::cout << message << " = " << (angle*180.0/M_PI) << std::endl;
#endif
}

static void print(edge2 he, const std::string& message)
{
	#ifdef PRINT_ENABLED
	std::cout << message << " [edge] label = " << he->data().label << " id = " << he->data().id << std::endl;
	std::cout << '\t' << (float)CGAL::to_double(he->source()->point().x()) << ", " << (float)CGAL::to_double(he->source()->point().y()) << ", id: " << he->source()->data().id << std::endl;
	std::cout << '\t' << (float)CGAL::to_double(he->target()->point().x()) << ", " << (float)CGAL::to_double(he->target()->point().y()) << ", id: " << he->target()->data().id << std::endl;
	#endif
}

static void print(const_edge2 he, const std::string& message)
{
	#ifdef PRINT_ENABLED
	std::cout << message << " [edge] label = " << he->data().label << " id = " << he->data().id << std::endl;
	std::cout << '\t' << (float)CGAL::to_double(he->source()->point().x()) << ", " << (float)CGAL::to_double(he->source()->point().y()) << ", id: " << he->source()->data().id << std::endl;
    std::cout << '\t' << (float)CGAL::to_double(he->target()->point().x()) << ", " << (float)CGAL::to_double(he->target()->point().y()) << ", id: " << he->target()->data().id << std::endl;
	#endif
}

static void print(const point2& p, const std::string& message)
{
	#ifdef PRINT_ENABLED
	std::cout << message << " [point] = " << (float)CGAL::to_double(p.x()) << ", " << (float)CGAL::to_double(p.y()) << std::endl;
	#endif
}

static void print(const vec2& v, const std::string& message)
{
	#ifdef PRINT_ENABLED
	std::cout << message << " [point] = " << (float)CGAL::to_double(v.x()) << ", " << (float)CGAL::to_double(v.y()) << std::endl;
	#endif
}

static void print(const segment2& s, const std::string& message)
{
	#ifdef PRINT_ENABLED
	std::cout << message << " [segment] = " << std::endl;
	std::cout << '\t' << (float)CGAL::to_double(s.source().x()) << ", " << (float)CGAL::to_double(s.source().y()) << std::endl;
	std::cout << '\t' << (float)CGAL::to_double(s.target().x()) << ", " << (float)CGAL::to_double(s.target().y()) << std::endl;
	#endif
}

static void print(const vertex2& v, const std::string& message, int precision = 6)
{
#ifdef PRINT_ENABLED
	std::cout << std::setprecision(precision) << message << " [vertex]" << std::endl;
	std::cout << '\t'
		<< CGAL::to_double(v->point().x()) << ", "
		<< CGAL::to_double(v->point().y()) << ", "
		<< "id: " << v->data().id << std::endl;
#endif
}

static void print(const face2& f, const std::string& message, int precision = 6)
{
	#ifdef PRINT_ENABLED
	std::cout << message << " [face] label = " << f->data().label << std::endl;
	if (is_unbounded_face(f))
	{
		std::cout << '\t' << "unbounded" << std::endl;
	}
	else if (f->has_outer_ccb())
	{
		edge2_circulator eit = f->outer_ccb();
		edge2_circulator end = eit;
		do
		{
			edge2 he = eit;
			if (precision == -1)
				std::cout << std::setprecision(precision) << "\tface vertex: " << " [point2] = " << he->target()->point().exact() << ", eid: " << he->data().id << ", vid: " << he->target()->data().id << ", label " << he->data().label << std::endl;
			else
				std::cout << std::setprecision(precision) << "\tface vertex: " << " [point2] = " << he->target()->point() << ", eid: " << he->data().id << ", vid: " << he->target()->data().id << ", label " << he->data().label << std::endl;
		} while (++eit != end);
	}
	#endif
}

static void print(const A& arr, const std::string& message, int precision = 6)
{
#ifdef PRINT_ENABLED
	std::cout << message << " [arrangement] = " << std::endl;
	typename A::Face_iterator fit = arr.faces_begin();
	typename A::Face_iterator fnd = arr.faces_end();
	for (; fit != fnd; fit++)
	{
		if (is_unbounded_face(fit))
			continue;

		print(fit, "");
	}

#endif
}

static void print(const face3& f, const std::string& message, int precision = 6)
{
	#ifdef PRINT_ENABLED
	std::cout << message << " [face3D] label = " << f->data.label << ", tag = " << f->data.tag << ", id = " << f->data.id << std::endl;
	typename polyhedron3::Halfedge_around_facet_circulator eit = f->facet_begin();
	typename polyhedron3::Halfedge_around_facet_circulator end = eit;
	do
	{
		std::cout << std::setprecision(precision) << "\tface vertex: " << " [point3] = " << eit->vertex()->point() << ", eid: " << eit->data.id << ", vid: " << eit->vertex()->data.id << ", label: " << eit->data.label << std::endl;
	} while (++eit != end);
	#endif
}

template<typename context>
static void print_ex(const face3& f, const std::string& message, context ctx, int precision = 6)
{
#ifdef PRINT_ENABLED
	std::cout << message << " [face3D] label = " << ctx->label_name(f->data.label) << ", tag = " << f->data.tag << std::endl;
	typename polyhedron3::Halfedge_around_facet_circulator eit = f->facet_begin();
	typename polyhedron3::Halfedge_around_facet_circulator end = eit;
	do
	{
		std::cout << std::setprecision(precision) << "\tface vertex: " << " [point3] = " << eit->vertex()->point() << ", eid: " << eit->data.id << ", vid: " << eit->vertex()->data.id << ", label: " << ctx->label_name(eit->data.label) << std::endl;
		auto eit_opp = eit->opposite();
		std::cout << std::setprecision(precision) << "\t\t\t opposite: " << "eid: " << eit_opp->data.id << ", vid: " << eit_opp->vertex()->data.id << ", label: " << ctx->label_name(eit_opp->data.label) << std::endl;
	} while (++eit != end);
#endif
}

template<typename context>
static void print(const face3& f, const std::string& message, context ctx, int precision = 6)
{
#ifdef PRINT_ENABLED
	std::cout << message << " [face3D] label = " << f->data.label << " \"" << ctx->label_name(f->data.label) << "\"" << std::endl;
	typename polyhedron3::Halfedge_around_facet_circulator eit = f->facet_begin();
	typename polyhedron3::Halfedge_around_facet_circulator end = eit;
	do
	{
		std::cout << std::setprecision(precision) << "\tface vertex: " << " [point3] = " << eit->vertex()->point() << ", vid: " << eit->vertex()->data.id << ", label: " << eit->data.label << " \"" << ctx->label_name(eit->data.label) << "\"" << std::endl;
	} while (++eit != end);
#endif
}

static void print(const point3& p, const std::string& message, int precision = 6)
{
	#ifdef PRINT_ENABLED
	std::cout << std::setprecision(precision) << message << " [point3D] = " << p <<std::endl;
	#endif
}

static void print(const vec3& p, const std::string& message)
{
	#ifdef PRINT_ENABLED
	std::cout << message << " [point3D] = " << p << std::endl;
	#endif
}

static void print(const segment3& s, const std::string& message)
{
#ifdef PRINT_ENABLED
	std::cout << message << " [segment] = " << std::endl;
	std::cout << '\t' << (float)CGAL::to_double(s.source().x()) << ", " << (float)CGAL::to_double(s.source().y()) << ", " << (float)CGAL::to_double(s.source().z()) << std::endl;
	std::cout << '\t' << (float)CGAL::to_double(s.target().x()) << ", " << (float)CGAL::to_double(s.target().y()) << ", " << (float)CGAL::to_double(s.target().z()) << std::endl;
#endif
}

static void print(const vertex3& v, const std::string& message, int precision = 6)
{
#ifdef PRINT_ENABLED
	std::cout << std::setprecision(precision) << message << " [vertex]" << std::endl;
	std::cout << '\t'
		<< CGAL::to_double(v->point().x()) << ", "
		<< CGAL::to_double(v->point().y()) << ", "
		<< CGAL::to_double(v->point().z()) << ", "
		<< "id: " << v->data.id << std::endl;
#endif
}

static void print(const edge3& he, const std::string& message, int precision = 6)
{
	#ifdef PRINT_ENABLED
	if (he == edge3())
		std::cout << message << " [edge] (empty)" << std::endl;
	else
	{
		std::cout << std::setprecision(precision) << message << " [edge] label = " << he->data.label << ", id = " << he->data.id << ", tag = " << he->data.tag << std::endl;
		std::cout << '\t'
			<< CGAL::to_double(he->prev()->vertex()->point().x()) << ", "
			<< CGAL::to_double(he->prev()->vertex()->point().y()) << ", "
			<< CGAL::to_double(he->prev()->vertex()->point().z()) << ", "
			<< "vid: " << he->prev()->vertex()->data.id << std::endl;

		std::cout << '\t'
			<< CGAL::to_double(he->vertex()->point().x()) << ", "
			<< CGAL::to_double(he->vertex()->point().y()) << ", "
			<< CGAL::to_double(he->vertex()->point().z()) << ", "
			<< "vid: " << he->vertex()->data.id << std::endl;
	}
	#endif
}

static void print(const_edge3& he, const std::string& message, int precision = 6)
{
#ifdef PRINT_ENABLED
	if (he == edge3())
		std::cout << message << " [edge] (empty)" << std::endl;
	else
	{
		std::cout << std::setprecision(precision) << message << " [edge] label = " << he->data.label << ", id = " << he->data.id << ", tag = " << he->data.tag << std::endl;
		std::cout << '\t'
			<< CGAL::to_double(he->prev()->vertex()->point().x()) << ", "
			<< CGAL::to_double(he->prev()->vertex()->point().y()) << ", "
			<< CGAL::to_double(he->prev()->vertex()->point().z()) << ", "
			<< "vid: " << he->prev()->vertex()->data.id << std::endl;

		std::cout << '\t'
			<< CGAL::to_double(he->vertex()->point().x()) << ", "
			<< CGAL::to_double(he->vertex()->point().y()) << ", "
			<< CGAL::to_double(he->vertex()->point().z()) << ", "
			<< "vid: " << he->vertex()->data.id << std::endl;
	}
#endif
}

static face2 first_face(A& arr)
{
    typename A::Face_iterator fit = arr.faces_begin();
    typename A::Face_iterator fnd = arr.faces_end();
    for(; fit != fnd; fit++)
    {
		if (is_unbounded_face(fit))
			continue;

		return fit;
	}

    return face2();
}

static bool check(A& arr)
{
	bool res = true;
	typename A::Face_iterator fit = arr.faces_begin();
	typename A::Face_iterator fnd = arr.faces_end();
	for (; fit != fnd; fit++)
	{
		if (is_unbounded_face(fit))
			continue;

		if (fit->has_outer_ccb())
		{
			edge2_circulator eit = fit->outer_ccb();
			edge2_circulator end = eit;
			do
			{
				edge2 he = eit;
				if (he->source()->point() == he->target()->point())
				{
					std::cout << "degen edge: ";
					print(he, "");
					res = false;
				}
			} while (++eit != end);
		}
	}

	return res;
}

static bool check(face3& f, const std::string& message, int precision = 6)
{
	bool res = true;
	typename K::FT min_dist = 100000000000000;
	typename polyhedron3::Halfedge_around_facet_circulator eit = f->facet_begin();
	typename polyhedron3::Halfedge_around_facet_circulator end = eit;
	do
	{
		auto dist = CGAL::squared_distance(eit->prev()->vertex()->point(), eit->vertex()->point());
		if (min_dist > dist)
			min_dist = dist;
		if (eit->prev()->vertex()->point() == eit->vertex()->point())
		{
			res = false;
#ifdef PRINT_ENABLED
			std::cout << std::setprecision(precision) << "\t degenerated edge: " << " [point3] = " << eit->vertex()->point() << std::endl;
#endif
		}
	} while (++eit != end);

	if (min_dist < 0.00001)
		std::cout << std::setprecision(precision) << "min_dist: " << min_dist << "\n";
	return res;
}

  static void segment_overlay(point2& seg1_src, point2& seg1_tgt,
	  point2& seg2_src, point2& seg2_tgt, point2& ovl_src, point2& ovl_tgt)
  {
	// -- check if is descending to invert Y
	double slope = CGAL::to_double((seg1_tgt.y() - seg1_src.y())/(seg1_tgt.x() - seg1_src.x()));
	bool isHorizontal = CGAL::abs(slope) < 0.00001;
    bool isDescending = slope < 0 && !isHorizontal;
    double invertY = isDescending || isHorizontal ? -1 : 1;

	point2 min1 = point2(MIN(seg1_src.x(), seg1_tgt.x()),
		MIN(seg1_src.y() * invertY, seg1_tgt.y() * invertY));
	point2 max1 = point2(MAX(seg1_src.x(), seg1_tgt.x()),
		MAX(seg1_src.y() * invertY, seg1_tgt.y() * invertY));

	point2 min2 = point2(MIN(seg2_src.x(), seg2_tgt.x()),
		MIN(seg2_src.y() * invertY, seg2_tgt.y() * invertY));
	point2 max2 = point2(MAX(seg2_src.x(), seg2_tgt.x()),
		MAX(seg2_src.y() * invertY, seg2_tgt.y() * invertY));

	point2 minIntersection = point2(MAX(min1.x(), min2.x()), MAX(min1.y(), min2.y()));
	point2 maxIntersection = point2(MIN(max1.x(), max2.x()), MIN(max1.y(), max2.y()));

	bool intersect = (minIntersection.x() < maxIntersection.x()) &&
		(minIntersection.y() < maxIntersection.y());

	if(intersect)
	{
		if(seg1_src.x() < seg1_tgt.x())
		{
			ovl_src = point2(minIntersection.x(), minIntersection.y() * invertY);
			ovl_tgt = point2(maxIntersection.x(), maxIntersection.y() * invertY);
		}
		else
		{
			ovl_src = point2(maxIntersection.x(), maxIntersection.y() * invertY);;
			ovl_tgt = point2(minIntersection.x(), minIntersection.y() * invertY);
		}
	}
	else
	{
		ovl_src = point2(INT_MAX, INT_MAX);
		ovl_tgt = point2(INT_MAX, INT_MAX);
	}
  }
};

// Phoenix boundary: production SVG/file diagnostics are intentionally dropped.
template<typename K, typename A, typename P>
struct io_utils {};

#define DECLARE_UTILS(K, A, P)\
  typedef geometry_utils<K, A, P> geom_utils;\
  typedef io_utils<K, A, P> file_utils;\

#define CGAL_UTILS(K, A, P) \
  typedef geometry_utils<K, A, P> geom_utils;\
  typedef io_utils<K, A, P> file_utils;\

#define DEFINE_UTILS(K, A, P, Prefix) typedef geometry_utils<K, A, P> Prefix##_geom_utils;
#define DEFINE_UTILS2(K, A, Prefix) typedef geometry_utils<K, A, cgal3_mesh<K, int, int, int>::polyhedron> Prefix##_geom_utils;
#define DEFINE_UTILS3(K, P, Prefix) typedef geometry_utils<K, cgal2_arrangement<K, int, int, int>::arrangement, P> Prefix##_geom_utils;

#define DEFINE_FILE_UTILS(K, A, P, Prefix) typedef io_utils<K, A, cgal3_mesh<K, int, int, int>::polyhedron> Prefix##_file_utils;
#define DEFINE_FILE_UTILS2(K, A, Prefix) typedef io_utils<K, A, cgal3_mesh<K, int, int, int>::polyhedron> Prefix##_file_utils;
#define DEFINE_FILE_UTILS3(K, P, Prefix) typedef io_utils<K, cgal2_arrangement<K, int, int, int>::arrangement, P> Prefix##_file_utils;

#define DECLARE_UTILS2(K, A) \
  typedef typename geometry_utils<K, A, typename cgal3_mesh<K, int, int, int>::polyhedron> geom_utils;\
  typedef typename io_utils<K, A, cgal3_mesh<K, int, int, int>::polyhedron> file_utils;\

#define CGAL_UTILS2(K, A) \
  typedef geometry_utils<K, A, typename cgal3_mesh<K, int, int, int>::polyhedron> geom_utils;\
  typedef io_utils<K, A, cgal3_mesh<K, int, int, int>::polyhedron> file_utils;\

#define DECLARE_UTILS3(K, P) typedef geometry_utils<K, typename cgal2_arrangement<K, int, int, int>::arrangement, P> geom_utils;
#define CGAL_UTILS3(K, P) typedef geometry_utils<K, typename cgal2_arrangement<K, int, int, int>::arrangement, P> geom_utils;

