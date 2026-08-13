
#pragma once

#include "geometry_types.h"
#include "geometry_utils.h"
#include "../geometry.h"

#include <queue>

template <typename K, typename A>
struct cut_face
  {
    DECLARE_TYPES(K)
    DECLARE_ARRANGEMENT_TYPES(A)

    typedef A arrangement2;

    cut_face(arrangement2& arr, face2 f, const line2& l):
      arr_(arr),
      f_(f),
      l_(l),
      tolerance_(1e-3)
      {
      }

    edge2 cut()
      {
        edge2 he1 = find_first();
        if (he1 == edge2())
          return he1;

        edge2 he2 = find_next(he1->next());
        if (he1 == he2 || he1->next() == he2 || he2->next() == he1)
          {
            intersection_ = he1;
            return edge2();
          }

        vertex2 v1 = he1->target();
        vertex2 v2 = he2->target();

        if (colinear(he1, he2))
          {
            intersection_ = he1->next();
            return edge2();
          }

        if (colinear(he2, he1))
          {
            intersection_ = he2->next();
            return edge2();
          }

        //I guess they intersect now
        face2_data fd = f_->data();
        edge2      r  = arr_.insert_at_vertices(curve2(v1->point(), v2->point()), v1, v2);

        r->face()->data()         = fd;
        r->twin()->face()->data() = fd;
		//r->twin()->face()->data().id = -1; // do not propagate face ids (pending)
		return r;
      }

    bool intersected(edge2& he, vertex2& v)
      {
        if (intersection_ == edge2())
          return false;

        if (CGAL::squared_distance(l_, intersection_->source()->point()) < tolerance_*tolerance_)
          {
            he = intersection_;
          }
        else if (CGAL::squared_distance(l_, intersection_->next()->target()->point()) < tolerance_*tolerance_)
          {
            he = intersection_->next();
          }
        else
          {
            he = intersection_;
            v  = intersection_->target();
          }

        return true;
      }

    bool colinear(edge2 he1, edge2 he2)
      {
        while (he1 != he2)
          {
            if (CGAL::squared_distance(l_, he1->target()->point()) > tolerance_*tolerance_)
              return false;

            he1 = he1->next();
          }

        return true;
      }

    private:
      arrangement2& arr_;
      face2         f_;
      line2         l_;
      double        tolerance_;
      edge2         intersection_;

      edge2 find_first()
        {
          edge2   he = f_->outer_ccb();
          point2& p  = he->source()->point();
          if (l_.has_on(p))
            return he->prev();

          return find_next(he);
        }

      edge2 find_next(edge2 he)
        {
          edge2 it = he;
          edge2 nd = he;

          bool first       = true;
          bool positive    = false;
          bool intersected = false;
          do
          {
            point2& p = it->target()->point();

            if (l_.has_on(p))
              return it;

            bool on_positive = l_.has_on_positive_side(p);

            if (first)
              {
                first = false;
                positive = on_positive;
              }

            if (positive != on_positive)
              {
                intersected = true;
                break;
              }

            it = it->next();
          }
          while (it != nd);

          if (intersected)
            return cut_edge(it);

          return edge2();
        }

      edge2 cut_edge(edge2 e)
        {
          segment2 s(e->source()->point(), e->target()->point());
          CGAL::Object iobj = CGAL::intersection(s, l_);
          assert(!iobj.empty());

          const point2* ipoint = CGAL::object_cast<point2>(&iobj);
          if (ipoint)
            {
              point2 p = *ipoint;

              if (CGAL::squared_distance(l_, e->source()->point()) < tolerance_*tolerance_)
                {
                  return e->prev();
                }

              if (CGAL::squared_distance(l_, e->target()->point()) < tolerance_*tolerance_)
                {
                  return e;
                }

              edge2_data ed  = e->data();
              edge2_data oed = e->twin()->data();

              edge2  r = arr_.split_edge(e, segment2(e->source()->point(), p), segment2(p, e->target()->point()));
              r->data() = ed;
              r->twin()->data() = oed;
              r->next()->data() = ed;
              r->next()->twin()->data() = oed;

              return r;
            }

          return edge2();
        }
  };

template <typename K, typename A, typename FN>
struct find_next_edge
  {
    DECLARE_TYPES(K)
    DECLARE_ARRANGEMENT_TYPES(A)

    typedef A arrangement2;

    find_next_edge(FN& fn, bool forward):
      fn_(fn),
      forward_(forward)
      {
      }

    edge2 next(edge2 he)
      {
        vertex2 v = forward_? he->target() : he->source();
        typename arrangement2::Halfedge_around_vertex_circulator it = v->incident_halfedges();
        typename arrangement2::Halfedge_around_vertex_circulator nd = it;

        CGAL_For_all(it, nd)
          {
            if (forward_)
              {
                if (it == he)
                  continue;
              }
            else if (it->twin() == he)
              continue;

            if (fn_(it))
              return forward_? it->twin() : it;
          }

        return edge2();
      }
    private:
      FN&  fn_;
      bool forward_;
  };

template <class HDS, typename K, typename A, typename P>
class arrangement_in_face3 : public CGAL::Modifier_base<HDS>
{
  DECLARE_TYPES(K)
  DECLARE_ARRANGEMENT_TYPES(A)
  DECLARE_MESH_TYPES(P)

  typedef A arrangement2;
  typedef P polyhedron3;

  DECLARE_UTILS(K, A, P)

  public:
    struct observer
    {
      virtual void vertex_created(vertex2 v, vertex3 v3) = 0;
      virtual void edge_created(edge2 he, edge3 he3)     = 0;
      virtual void face_created(face2 f, face3 f3)       = 0;
    };

  public:
    arrangement_in_face3(arrangement2& data, polyhedron3& p, face3 target):
    data_(data),
    p_(p),
    face_(target),
    observer_(nullptr)
    {
    }

    face3_list& result()
    {
      return result_;
    }

    void add_observer(observer* o)
    {
      observer_ = o;
    }

    void build()
    {
      p_.delegate(*this);
    }

    void operator()(HDS& hds)
    {
      plane3 fp = geom_utils::face_plane(face_);
      p_.erase_facet(face_->facet_begin());

      size_t vcount = data_.number_of_vertices();
      size_t fcount = data_.number_of_faces() - 1;

      typedef typename HDS::Vertex   Vertex;
      typedef typename Vertex::Point Point;

      CGAL::Polyhedron_incremental_builder_3<HDS> B( hds, true);
      B.begin_surface(vcount, fcount);

      //add vertices
      typename arrangement2::Vertex_iterator it = data_.vertices_begin();
      typename arrangement2::Vertex_iterator nd = data_.vertices_end();

      int curr = 0;
      for(; it != nd; it++, curr++)
      {
        const point2& p = it->point();
        vertex3 result = B.add_vertex(fp.to_3d(p));

        it->data() = curr;
        if (observer_)
          observer_->vertex_created(it, result);
      }

      typename arrangement2::Face_iterator fit = data_.faces_begin();
      typename arrangement2::Face_iterator fnd = data_.faces_end();
      for(; fit != fnd; fit++)
      {
        if (is_unbounded_face(fit))
          continue;

        edge2_circulator eit = fit->outer_ccb();
        edge2_circulator end = eit;
        face3 r = B.begin_facet();
        CGAL_For_all(eit, end)
        {
          B.add_vertex_to_facet(eit->target()->data());
        }
        B.end_facet();

        r->data.lab = fit->data().lab;

        if (observer_)
          observer_->face_created(fit, r);

        result_.push_back(r);
      }

      B.end_surface();
    }
  private:
    arrangement2& data_;
    polyhedron3&  p_;
    face3         face_;
    face3_list    result_;
    observer*     observer_;

    edge2 find_vertex_edge(vertex2 v, edge3 he)
    {
      int prev_id = he->prev()->vertex()->index;
      typename arrangement2::Halfedge_around_vertex_circulator it = v->incident_halfedges();
      typename arrangement2::Halfedge_around_vertex_circulator nd = it;
      CGAL_For_all(it, nd)
        {
          if (it->source()->data() == prev_id)
            return it;
        }

      return edge2();
    }

    /*
    bool valid_index(int idx)
    {
      return idx >= 0 && idx < vmap_.size();
    }
    */
};

template <typename KS, typename AS, typename KT, typename AT>
struct arrangement_in_face2
{
  DECLARE_TYPES(KS)
  DECLARE_ARRANGEMENT_TYPES(AS)

  typedef typename AT::Vertex_handle   target_vertex;
  typedef typename AT::Halfedge_handle target_edge;
  typedef typename AT::Face_handle     target_face;

  typedef typename cgal2_types<KT>::point   target_point;
  typedef typename cgal2_types<KT>::segment target_segment;

  typedef typename std::vector<target_face>   target_face_list;
  typedef typename std::vector<target_vertex> target_vertex_list;

  DECLARE_UTILS(KS, AS, geometry::polyhedron3)

  public:
    struct observer
    {
      virtual void vertex_created(target_vertex v, vertex2 sv) = 0;
      virtual void edge_created(target_edge he,    edge2 se)   = 0;
      virtual void face_created(target_face f,     face2 sf)   = 0;
    };

  public:
    arrangement_in_face2(AS& data, AT& target_arr, target_face target):
    data_(data),
    arr_(target_arr),
    face_(target),
    observer_(nullptr)
    {
    }

    /*
    // NOTE: commented because its does not used and result_ is not declared in this scope
    target_face_list& result()
    {
      return result_;
    }
    */

    void add_observer(observer* o)
    {
      observer_ = o;
    }

    void build(target_vertex_list& v, vertex2_list& vv)
    {
      const int my_tag = 76459;

      //mark boundary vertices
      for(int i = 0; i < v.size(); i++)
      {
        v[i]->data()  = my_tag + i;
        vv[i]->data() = my_tag + i;
      }

      //create non existent vertices
      typename AS::Vertex_iterator vit = data_.vertices_begin();
      typename AS::Vertex_iterator vnd = data_.vertices_end();
      CGAL_For_all(vit, vnd)
      {
        int idx = vit->data() - my_tag;
        if (idx < 0 || idx > v.size())
        {
          idx = my_tag + v.size();

          target_point  tp = vit->point();
          target_vertex nv = arr_.insert_in_face_interior(tp, face_);

          nv->data()  = idx;
          vit->data() = idx;

          v.push_back(nv);
          vv.push_back(vit);

          if (observer_)
            observer_->vertex_created(nv, vit);
        }
      }

      //recreate face structure
      typename AS::Face_iterator it = data_.faces_begin();
      typename AS::Face_iterator nd = data_.faces_end();
      target_edge       he;
      CGAL_For_all(it, nd)
      {
        if (is_unbounded_face(it))
          continue;

        edge2_circulator eit = it->outer_ccb();
        edge2_circulator end = eit;
        CGAL_For_all(eit, end)
        {
          int    idx  = eit->target()->data() - my_tag;
          int    sidx = eit->source()->data() - my_tag;
          target_vertex tv  = v[idx];
          target_vertex sv  = v[sidx];

          if (!find_edge(sv, tv, he))
          {
            target_segment s(sv->point(), tv->point());
            he = arr_.insert_at_vertices(s, sv, tv);

            if (observer_)
              observer_->edge_created(he, eit);
          }
        }

        if (observer_)
          observer_->face_created(he->face(), it);
      }
    }

  private:
    AS&         data_;
    AT&         arr_;
    target_face face_;
    observer*   observer_;

    bool find_edge(target_vertex sv, target_vertex tv, target_edge& he)
    {
      if (sv->is_isolated() || tv->is_isolated())
        return false;

      typename AT::Halfedge_around_vertex_circulator it = tv->incident_halfedges();
      typename AT::Halfedge_around_vertex_circulator nd = it;
      CGAL_For_all(it, nd)
      {
        if (it->source() == sv)
        {
          he = it;
          return true;
        }
      }

      return false;
    }
};

template <typename KS, typename MS, typename KT, typename AT>
struct arrangement_from_face3
{
  DECLARE_TYPES(KS)
  DECLARE_MESH_TYPES(MS)

  typedef MS polyhedron3;

  typedef typename AT::Vertex_handle   target_vertex;
  typedef typename AT::Halfedge_handle target_edge;
  typedef typename AT::Face_handle     target_face;

  typedef typename cgal2_types<KT>::point target_point;
  typedef AT                     target_arrangement;

  DECLARE_UTILS(KS, target_arrangement, MS)

  static target_face build(face3 f, target_arrangement& arr)
  {
    plane3 build_plane = geom_utils::face_plane(f);

    bool          first = true;
    target_point  p1, p2;
    target_vertex v1, v2, fv;

    typename polyhedron3::Halfedge_around_facet_circulator eit = f->facet_begin();
    typename polyhedron3::Halfedge_around_facet_circulator end = eit;
    CGAL_For_all(eit, end)
    {
      bool last = eit->next() == end;

      point2 pp1 = build_plane.to_2d(eit->vertex()->point());
      point2 pp2 = build_plane.to_2d(eit->next()->vertex()->point());

      if (first)
      {
        first = false;
        p1 = geom_utils::template convert_point2<DoubleKernel>(pp1);
        p2 = geom_utils::template convert_point2<DoubleKernel>(pp2);
	      v1 = arr.insert_in_face_interior(p1, arr.unbounded_face());
        v2 = arr.insert_in_face_interior(p2, arr.unbounded_face());
        fv = v1;
      }
      else
      {
        p1 = p2;
        v1 = v2;

        if (last)
        {
          p2 = fv->point();
          v2 = fv;
        }
        else
        {
          p2 = geom_utils::template convert_point2<DoubleKernel>(pp2);
          v2 = arr.insert_in_face_interior(p2, arr.unbounded_face());
        }
      }

      target_edge he = arr.insert_at_vertices(curve2(p1, p2), v1, v2);
      he->data().lab = eit->data.lab;
      he->twin()->data().lab = eit->opposite()->data.lab;

      if (last)
      {
        he->face()->data().lab = f->data.lab;
        return he->face();
      }
    }

    assert(false); //should never get here
    return target_face();
  }
};

template<typename K, typename A, typename P>
struct face_projector {
	DECLARE_TYPES(K)
	DECLARE_MESH_TYPES(P)
	DECLARE_UTILS(K, A, P)

	plane3 _plane;
	vec3 _normal;
	point3 _origin;
	//point3 _prev;
	vec3 _base1, _base2;
	double _scale;
	bool _valid;
	bool _flip;
	double _flip_yz;

	face_projector() :
		_valid(false),
		_flip(false),
		_flip_yz(-1),
		_plane(0, 0, 0, 0),
		_normal()
	{}

	face_projector(const plane3& pl, bool flip = false):
		_flip_yz(-1),
		_plane(0, 0, 0, 0),
		_normal()
	{
		_valid = true;
		_flip = flip;
		_plane = pl;

		if (_plane.is_degenerate())
		{
			_valid = false;
			return;
		}

		vec3 normal = _plane.orthogonal_vector();
		if (flip)
		{
			normal = -normal;
			_plane = _plane.opposite();
		}

		//_origin = _plane.to_3d(point2(0, 0));
		_origin = _plane.point();

		_base1 = _plane.base1();
		_base2 = _plane.base2();

		if (!computeScale())
		{
			_valid = false;
			return;
		}

		_base1 = _plane.base1() * _scale;
		_base2 = _plane.base2() * _scale;
	}

	face_projector(const face3& face, bool flip = false):
		_flip_yz(-1),
		_plane(0, 0, 0, 0),
		_normal()
	{
		_valid = true;
		_flip = flip;
		vec3 normal = geom_utils::face_normal(face);
		if (normal == CGAL::NULL_VECTOR)
		{
			_valid = false;
			return;
		}

		if (flip)
			normal = -normal;

        //_origin = geom_utils::centroid(face);
        //_plane = plane3(_origin, normal);
        
		_plane = plane3(face->facet_begin()->vertex()->point(), normal);
		_origin = _plane.point();

		_base1 = _plane.base1();
		_base2 = _plane.base2();

		if (!computeScale())
		{
			_valid = false;
			return;
		}

		_base1 = _plane.base1() * _scale;
		_base2 = _plane.base2() * _scale;
	}

	bool is_valid() const
	{
		return _valid;
	}

	vec3 normal()
	{
		return _flip ? -_plane.orthogonal_vector() : _plane.orthogonal_vector();
	}

	plane3 plane()
	{
		return _flip ? _plane.opposite()  : _plane;
	}

	/*double scale()
	{
		return 1.0;
	}*/

	point2 to_2d(const point3& p) const
	{
		assert(is_valid());
		//if (!is_valid())
		//	std::cout << "!is_valid()\n";

		typename K::FT alpha, beta, gamma;
		auto normal = _plane.orthogonal_vector();
		auto d = p - _origin;
		CGAL::solve(_base1.x(), _base1.y(), _base1.z(),
			_base2.x(), _base2.y(), _base2.z(),
			normal.x(), normal.y(), normal.z(),
			d.x(), d.y(), d.z(),
			alpha, beta, gamma);

		return point2(alpha, _flip_yz*beta);
	}

	point3 to_3d(const point2& p) const
	{
		assert(is_valid());
		//if (!is_valid())
		//	std::cout << "!is_valid()\n";
		point3 result = _origin + _base1*p.x() + _flip_yz*_base2*p.y();
		return result;
	}

private:

	bool computeScale()
	{
		_scale = 1.0;

		auto pp1 = to_3d(point2(0, 0));
		auto pp2 = to_3d(point2(1, 0));

		auto d3 = CGAL::squared_distance(pp1, pp2);
		if (d3 == 0)
			return false;

		_scale = CGAL::sqrt(CGAL::to_double(1 / d3));

		/*auto _prev = _origin + _base1;
		auto pp1 = to_2d(_prev);
		auto pp2 = to_2d(_origin);

		auto d3 = CGAL::squared_distance(_prev, _origin);
		auto d2 = CGAL::squared_distance(pp1, pp2);

		if (d2 == 0 || d3 == 0)
			return false;

		_scale = CGAL::sqrt(CGAL::to_double(d2 / d3));*/
		return true;
	}

};
