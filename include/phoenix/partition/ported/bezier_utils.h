#pragma once

#include "geometry_constants.h"
#include "geometry_types.h"
//#include "simple_svg_1.0.0.hpp"
#include "phoenix/partition/ported/vm_compat.hpp"
//#include "timer.h"

#include <CGAL/squared_distance_3.h>

#include <variant>


template<typename K>
struct bezier_evaluator
{
	DECLARE_TYPES(K);

	point2 p1, p2, p3, p4;
	double ax, bx, cx;
	double ay, by, cy;

	bezier_evaluator(const point2& p1_, const point2& p2_, const point2& p3_, const point2& p4_) :
		p1(p1_),
		p2(p2_),
		p3(p3_),
		p4(p4_)
	{
		cx = CGAL::to_double(3.0 * (p2.x() - p1.x()));
		bx = CGAL::to_double(3.0 * (p3.x() - p2.x()) - cx);
		ax = CGAL::to_double(p4.x() - p1.x() - cx - bx);

		cy = CGAL::to_double(3.0 * (p2.y() - p1.y()));
		by = CGAL::to_double(3.0 * (p3.y() - p2.y()) - cy);
		ay = CGAL::to_double(p4.y() - p1.y() - cy - by);
	}

	point2 operator()(double t) const
	{
		double tSquared = t * t;
		double tCubed = tSquared * t;

		return point2((ax * tCubed) + (bx * tSquared) + (cx * t) + p1.x(), (ay * tCubed) + (by * tSquared) + (cy * t) + p1.y());
	}
};

struct bezier_options
{
	bezier_subdivision divide_by;
	double step_length;
	bezier_adjust adjust;

	int step_count;

	int lod;

	static bezier_options by_length(double step_length_, bezier_adjust adjust_by_)
	{
		return bezier_options(BEZIER_BY_LENGTH, step_length_, adjust_by_);
	}

	static bezier_options by_count(int step_count_)
	{
		return bezier_options(BEZIER_BY_COUNT, step_count_);
	}

	static bezier_options by_lod(int lod)
	{
		bezier_options options;
		options.lod = lod;
		return options;
	}

	bezier_options() :
		divide_by(BEZIER_BY_AUTO),
		step_length(-1),
		adjust(BEZIER_ADJUST_END),
		step_count(-1),
		lod(-1)
	{}

	bezier_options(bezier_subdivision divide_by_, double step_length_, bezier_adjust adjust_) :
		divide_by(divide_by_),
		step_length(step_length_),
		adjust(adjust_),
		step_count(-1),
		lod(-1)
	{}

	bezier_options(bezier_subdivision divide_by_, int step_count_) :
		divide_by(divide_by_),
		step_length(-1),
		adjust(BEZIER_ADJUST_END),
		step_count(step_count_),
		lod(-1)
	{}
};

struct var_bezier_options
{
	vm::variable_value step_length;
	bezier_adjust adjust;

	var_bezier_options() :
		step_length(),
		adjust(BEZIER_ADJUST_END)
	{}

	var_bezier_options(vm::variable_value& step_length_, bezier_adjust adjust_) :
		step_length(step_length_),
		adjust(adjust_)
	{}

	bezier_options to_bezier_options(vm::const_icontext_ref ctx) const;
};

/*template<typename T>
T clamp(const T& x, const T& a, const T& b)
{
	return (x < a) ? a : (x > b) ? b : x;
}*/

// intersects circle with segment formed by p1 and p2
template<typename Point2>
boost::optional<Point2> intersect_cs(const Point2& center, double radius2, const Point2& p1, const Point2& p2)
{
	typedef CGAL::Exact_circular_kernel_2             Circular_k;
	typedef CGAL::Point_2<Circular_k>                 Point_2;
	typedef CGAL::Line_2<Circular_k>				  Line_2;
	typedef CGAL::Line_arc_2<Circular_k>			  Line_arc_2;
	typedef CGAL::Segment_2<Circular_k>				  Segment_2;
	typedef CGAL::Circle_2<Circular_k>                Circle_2;
	typedef CGAL::Circular_arc_point_2<Circular_k>	  CPoint_2;

	Segment_2 s(Point_2(CGAL::to_double(p1.x()), CGAL::to_double(p1.y())), Point_2(CGAL::to_double(p2.x()), CGAL::to_double(p2.y())));
	Line_arc_2 la(s);
	Circle_2 c(Point_2(CGAL::to_double(center.x()), CGAL::to_double(center.y())), radius2);
	/*Segment_2 s(Point_2(p1.x().exact(), p1.y().exact()), Point_2(p2.x().exact(), p2.y().exact()));
	Line_arc_2 la(s);
	Circle_2 c(Point_2(pivot.x().exact(), pivot.y().exact()), length2);*/

	typedef std::pair<CPoint_2, unsigned> res_entry;
	typedef typename CGAL::CK2_Intersection_traits<Circular_k, Circle_2, Line_arc_2>::type Intersection_result;
	std::vector<Intersection_result> res;
	CGAL::intersection(c, la, std::back_inserter(res));
	boost::optional<Point2> result;
	if (res.size() == 1)
	{
		const res_entry* foo = std::get_if<res_entry>(&res[0]);
		if (foo)
			result = Point2(CGAL::to_double(foo->first.x()), CGAL::to_double(foo->first.y()));
	}
	else if (res.size() == 2)
	{
		const res_entry* foo1 = std::get_if<res_entry>(&res[0]);
		const res_entry* foo2 = std::get_if<res_entry>(&res[1]);
		if (foo1 && foo2)
		{
			auto pp1 = Point2(CGAL::to_double(foo1->first.x()), CGAL::to_double(foo1->first.y()));
			auto pp2 = Point2(CGAL::to_double(foo2->first.x()), CGAL::to_double(foo2->first.y()));
			if ((p2 - pp1).squared_length() <= (p2 - pp2).squared_length())
				result = pp1;
			else
				result = pp2;
		}
	}

	return result;
}

template <typename K, typename A, typename P>
struct bezier_utils_t
{
	DECLARE_TYPES(K)
    DECLARE_MESH_TYPES(P)
    DECLARE_ARRANGEMENT_TYPES(A)

	enum curve_recursion_limit_e { curve_recursion_limit = 32 };

	struct recursive_bezier_calculator
	{
		recursive_bezier_calculator(point2_list& result, double tolerance, double angle_tolerance = 0):
		  _result(result),
		  _angle_tolerance(angle_tolerance),
		  _cusp_limit(0)
		{
			_distance_tolerance = tolerance*tolerance;
		}

		void calculate(const point2 p1, const point2 p2, const point2 p3, const point2 p4)
		{
			_result.clear();
			_result.push_back(p1);

			double x1 = CGAL::to_double(p1.x()); double y1 = CGAL::to_double(p1.y());
			double x2 = CGAL::to_double(p2.x()); double y2 = CGAL::to_double(p2.y());
			double x3 = CGAL::to_double(p3.x()); double y3 = CGAL::to_double(p3.y());
			double x4 = CGAL::to_double(p4.x()); double y4 = CGAL::to_double(p4.y());

			recursive_bezier(x1, y1, x2, y2, x3, y3, x4, y4, 0);

			_result.push_back(p4);
		}

		void recursive_bezier(double x1, double y1, double x2, double y2, double x3, double y3,
								double x4, double y4, unsigned level)
		{
			if(level > curve_recursion_limit)
			{
				return;
			}

			// Calculate all the mid-points of the line segments
			//----------------------
			double x12   = (x1 + x2) / 2;
			double y12   = (y1 + y2) / 2;
			double x23   = (x2 + x3) / 2;
			double y23   = (y2 + y3) / 2;
			double x34   = (x3 + x4) / 2;
			double y34   = (y3 + y4) / 2;
			double x123  = (x12 + x23) / 2;
			double y123  = (y12 + y23) / 2;
			double x234  = (x23 + x34) / 2;
			double y234  = (y23 + y34) / 2;
			double x1234 = (x123 + x234) / 2;
			double y1234 = (y123 + y234) / 2;

			//if(level > 0) // Enforce subdivision first time
			if (level >= 0)
			{
				// Try to approximate the full cubic curve by a single straight line
				//------------------
				double dx = x4-x1;
				double dy = y4-y1;

				double d2 = fabs(((x2 - x4) * dy - (y2 - y4) * dx));
				double d3 = fabs(((x3 - x4) * dy - (y3 - y4) * dx));

				if (level == 0 && d2 < 1e-10 && d3 < 1e-10)
				{
					//std::cout << d2 << ", " << d3 << "\n";
					return;
				}

				double da1, da2;

				if(d2 > curve_collinearity_epsilon && d3 > curve_collinearity_epsilon)
				{
					// Regular care
					//-----------------
					if((d2 + d3)*(d2 + d3) <= _distance_tolerance * (dx*dx + dy*dy))
					{
						// If the curvature doesn't exceed the distance_tolerance value
						// we tend to finish subdivisions.
						//----------------------
						if(_angle_tolerance < curve_angle_tolerance_epsilon)
						{
							_result.push_back(point2(x1234, y1234));
							return;
						}

						// Angle & Cusp Condition
						//----------------------
						double a23 = atan2(y3 - y2, x3 - x2);
						da1 = fabs(a23 - atan2(y2 - y1, x2 - x1));
						da2 = fabs(atan2(y4 - y3, x4 - x3) - a23);
						if(da1 >= M_PI) da1 = 2*M_PI - da1;
						if(da2 >= M_PI) da2 = 2*M_PI - da2;

						if(da1 + da2 < _angle_tolerance)
						{
							// Finally we can stop the recursion
							//----------------------
							_result.push_back(point2(x1234, y1234));
							return;
						}

						if(_cusp_limit != 0.0)
						{
							if(da1 > _cusp_limit)
							{
								_result.push_back(point2(x2, y2));
								return;
							}

							if(da2 > _cusp_limit)
							{
								_result.push_back(point2(x3, y3));
								return;
							}
						}
					}
				}
				else
				{
					if(d2 > curve_collinearity_epsilon)
					{
						// p1,p3,p4 are collinear, p2 is considerable
						//----------------------
						if(d2 * d2 <= _distance_tolerance * (dx*dx + dy*dy))
						{
							if(_angle_tolerance < curve_angle_tolerance_epsilon)
							{
								_result.push_back(point2(x1234, y1234));
								return;
							}

							// Angle Condition
							//----------------------
							da1 = fabs(atan2(y3 - y2, x3 - x2) - atan2(y2 - y1, x2 - x1));
							if(da1 >= M_PI) da1 = 2*M_PI - da1;

							if(da1 < _angle_tolerance)
							{
								_result.push_back(point2(x2, y2));
								_result.push_back(point2(x3, y3));
								return;
							}

							if(_cusp_limit != 0.0)
							{
								if(da1 > _cusp_limit)
								{
									_result.push_back(point2(x2, y2));
									return;
								}
							}
						}
					}
					else
					if(d3 > curve_collinearity_epsilon)
					{
						// p1,p2,p4 are collinear, p3 is considerable
						//----------------------
						if(d3 * d3 <= _distance_tolerance * (dx*dx + dy*dy))
						{
							if(_angle_tolerance < curve_angle_tolerance_epsilon)
							{
								_result.push_back(point2(x1234, y1234));
								return;
							}

							// Angle Condition
							//----------------------
							da1 = fabs(atan2(y4 - y3, x4 - x3) - atan2(y3 - y2, x3 - x2));
							if(da1 >= M_PI) da1 = 2*M_PI - da1;

							if(da1 < _angle_tolerance)
							{
								_result.push_back(point2(x2, y2));
								_result.push_back(point2(x3, y3));
								return;
							}

							if(_cusp_limit != 0.0)
							{
								if(da1 > _cusp_limit)
								{
									_result.push_back(point2(x3, y3));
									return;
								}
							}
						}
					}
					else
					{
						// Collinear case
						//-----------------
						dx = x1234 - (x1 + x4) / 2;
						dy = y1234 - (y1 + y4) / 2;
						if(dx*dx + dy*dy <= _distance_tolerance)
						{
							_result.push_back(point2(x1234, y1234));
							return;
						}
					}
				}
			}

			// Continue subdivision
			//----------------------
			recursive_bezier(x1, y1, x12, y12, x123, y123, x1234, y1234, level + 1);
			recursive_bezier(x1234, y1234, x234, y234, x34, y34, x4, y4, level + 1);
		  }

		point2_list& _result;
		double      _distance_tolerance;
		//double      _approximation_scale;
		double      _angle_tolerance;
		double      _cusp_limit;
	};

	static double bezier_length(const point2 p1, const point2 p2, const point2 p3, const point2 p4)
	{
		auto len = geometry_utils<K, A, P>::distance(p1, p2) + geometry_utils<K, A, P>::distance(p2, p3) + geometry_utils<K, A, P>::distance(p3, p4);
		return len;
	}

	static void recursive_bezier(point2_list& result, const point2 p1, const point2 p2, const point2 p3, const point2 p4, double tolerance, double angle_tolerance = 0)
	{
		recursive_bezier_calculator c(result, tolerance, angle_tolerance);
		c.calculate(p1, p2, p3, p4);
	}

	static void subdivide_bezier(point2_list& result, const point2& p1, const point2& p2, const point2& p3, const point2& p4, bezier_options& options)
    {
		if (options.step_length > 0)
			subdivide_bezier_length(result, p1, p2, p3, p4, options.step_length, options.adjust);
        else if (options.step_count > 0)
            subdivide_bezier_count(result, p1, p2, p3, p4, options.step_count);
        else
            recursive_bezier_lod(result, p1, p2, p3, p4, options.lod);
    }

	static void subdivide_bezier_count(point2_list& result, const point2& p1, const point2& p2, const point2& p3, const point2& p4, int num_steps)
	{
        result.clear();

        if (num_steps <= 0)
            return;

        result.reserve(num_steps + 1);

        result.push_back(p1);

        if (num_steps > 1)
        {
            bezier_evaluator<K> bez(p1, p2, p3, p4);
            double t_inc = 1.0 / num_steps;
            double t = t_inc;
            for (int i = 1; i < num_steps; i++, t += t_inc)
            {
                result.push_back(bez(t));
            }
        }

        result.push_back(p4);
    }


	static void subdivide_bezier_length(point2_list& result, point2 p1, point2 p2, point2 p3, point2 p4, double step_length, bezier_adjust adjust)
	{
		// create the curve with margin at the end always, then reverse curve if necesary
		bool swap_order = adjust == BEZIER_ADJUST_BEGIN;

		point2_list base_points;

		if (swap_order)
		{
			std::swap(p1, p4);
			std::swap(p2, p3);
		}

		const int MAX_ITERATIONS = 5;
		int desired_num_segments = -1;
		double first_step_length = -1;
		// discretize the bezier in 150 small segments
		subdivide_bezier_count(base_points, p1, p2, p3, p4, 150);

		for(int iteration=0; true; iteration++)
		{
			result.clear();

			double step_length2, first_step_length2;
			first_step_length2 = step_length2 = step_length*step_length;
			if (first_step_length > 0)
				first_step_length2 = first_step_length*first_step_length;

			point2 pivot = base_points[0];
			result.push_back(pivot);

			for (int i = 1; i < base_points.size(); )
			{
				auto p = base_points[i];
				auto l2 = (p - pivot).squared_length();
				auto current_step_length2 = result.size() == 1 ? first_step_length2 : step_length2;
				if (l2 >= current_step_length2)
				{	// time to create a new segment, from the last pivot to the intersection of a circle of center in the last pivot and radius 'step_length' and the current small discretized segment
					auto pt_int = intersect_cs(pivot, current_step_length2, base_points[i - 1], p);
					if (pt_int)
					{
						pivot = *pt_int;
						//std::cout << "point: " << pivot << "\n";
						result.push_back(pivot);
					}
					else
					{	 // it happens
						//std::cout << "***********************************************\n";
						pivot = p;
						i++;
					}
				}
				else
					i++;
			}

			bool last_vertex_adjusted = false;
			if (result.back() != p4)
			{	// either add or replace the last vertex
				double deviation_from_end = CGAL::sqrt(CGAL::to_double((result.back() - p4).squared_length()));
				if (deviation_from_end < 0.01)
				{
					// replace the last vertex for the real last vertex
					//std::cout << "adjusting last point\n";
					last_vertex_adjusted = true;
					result.back() = p4;
				}
				else
					result.push_back(p4);
			}

			//std::cout << "iteration: " << iteration+1 << "\n";
			//for (int i = 1; i<result.size(); i++)
			//	std::cout << CGAL::sqrt(CGAL::to_double(CGAL::squared_distance(result[i], result[i - 1]))) << "\n";

			if (adjust == BEZIER_ADJUST_BEGIN || adjust == BEZIER_ADJUST_END || iteration >= MAX_ITERATIONS)
				break;

			// make another iteration to get a better aproximated curve

			double last_step_length = CGAL::sqrt(CGAL::to_double(CGAL::squared_distance(result[result.size() - 1], result[result.size() - 2])));
			double error_pct = 0;

			if (adjust == BEZIER_ADJUST_BEGIN_END)
				error_pct = first_step_length < 0 ? 100 : 100.0 * fabs(first_step_length - last_step_length) / last_step_length;
			else// if (adjust == BEZIER_ADJUST_SEGMENT_EXCESS || adjust == BEZIER_ADJUST_SEGMENT_DEFECT)
				error_pct = 100.0 * fabs(last_step_length - step_length) / step_length;

			//std::cout << "error: " << error_pct << "%\n";
			if (error_pct < 0.1)
				break; // enough precision

			if (adjust == BEZIER_ADJUST_BEGIN_END)
			{
				// only change the first step
				if (first_step_length < 0)
					first_step_length = last_step_length / 2;
				else
					first_step_length = (first_step_length + last_step_length) / 2;
			}
			else// if (adjust == BEZIER_ADJUST_SEGMENT_EXCESS || adjust == BEZIER_ADJUST_SEGMENT_DEFECT)
			{
				// recreate the curve with a new better aproximated step
				int num_segments = (int)result.size() - 1;

				if (desired_num_segments < 0)
				{
					desired_num_segments = last_vertex_adjusted ? num_segments : num_segments - 1;
					if (adjust == BEZIER_ADJUST_SEGMENT_DEFECT)
						desired_num_segments++;
				}

				if (desired_num_segments <= 0)
				{	// typically this means the curve can not fit a single segment at the desired step, so make no curve at all
					break;
				}

				double curve_len = step_length*(num_segments - 1) + last_step_length;
				step_length = curve_len / desired_num_segments;
			}
		}

        if (swap_order)
            std::reverse(result.begin(), result.end());

        /*for (auto p : result)
            std::cout << p << ", ";
        std::cout << "/n curve points: " << result.size() << "\n";*/
    }

	static void recursive_bezier_lod(point2_list& result, const point2 p1, const point2 p2, const point2 p3, const point2 p4, int lod)
	{
        double tolerance = 0.1;
        double angle_tolerance = 0.5;
        if (lod <= 0)
        {
            angle_tolerance = 0;
            tolerance = 1.0;
        }
        else
        {
            double len = bezier_length(p1, p2, p3, p4);
            if (len > 0)
            {
                if (lod == 1)
                {
                    //angle_tolerance = 0.1;
                    angle_tolerance = 0.1 / len;
                    angle_tolerance = std::clamp(angle_tolerance, 0.1, 0.5);
                }
                else
                {
                    //angle_tolerance = 0.025;
                    angle_tolerance = 0.05 / len;
                    angle_tolerance = std::clamp(angle_tolerance, 0.05, 0.25);
                }
            }
        }

        recursive_bezier_calculator c(result, tolerance, angle_tolerance);
        c.calculate(p1, p2, p3, p4);
    }

};


#define DECLARE_BEZIER(K, A, P) \
  typedef bezier_utils_t<K, A, P> bezier_utils;
