#include "phoenix/partition/ported/production/stdafx.h"

#include<CGAL/Polygon_2.h>
#include<CGAL/Polygon_with_holes_2.h>
#include<CGAL/create_offset_polygons_2.h>
#include<CGAL/Cartesian_converter.h>
#include <CGAL/IO/Arr_text_formatter.h>
#include <CGAL/IO/Arr_iostream.h>
#include <boost/property_tree/xml_parser.hpp>

#include "phoenix/inset/ported/production/inset.h"
#include "phoenix/partition/ported/geometry_ops.h"
#include "phoenix/inset/ported/cleanup_face.h"


#include "phoenix/partition/ported/null_diagnostics.hpp"

DEFAULT_CGAL_TYPES()
DEFAULT_ARRANGEMENT_TYPES()
//CGAL_EXACT_TYPES2()
//CGAL_EXACT_ARRANGEMENT()
DEFAULT_UTILS()

//straight skeleton types
//typedef CGAL::Cartesian<double> SSKernel;
//typedef CGAL::Epick SSKernel;
typedef CGAL::Epeck SSKernel;

typedef CGAL::Polygon_2<SSKernel> Polygon_2;
typedef CGAL::Straight_skeleton_2<SSKernel> Straight_skeleton;
typedef Straight_skeleton::Halfedge_handle SSEdge;
typedef Straight_skeleton::Vertex_handle SSVertex;

typedef CGAL::Arr_extended_dcel_text_formatter<geometry::arrangement2> Formatter;

template <typename K>
struct perturb_poly2
{
	typedef CGAL::Polygon_2<K> Polygon_2;
	typedef typename Polygon_2::Segment_2 segment2;

	struct request
	{
		request() :
			perturb_epsilon(1e-6)
		{
		}

		typename K::FT perturb_epsilon;
	};

	static void run(Polygon_2& poly, request r, Polygon_2& result)
	{
		auto first = poly.edges_circulator();
		auto curr = first;

		do
		{
			merge(poly, curr[-1], curr[0], r, result);

			curr++;
		} while (curr != first);
	}

private:
	static void merge(Polygon_2& poly, const segment2& he, const segment2& next, request& r, Polygon_2& result)
	{
		//check colinearity
		bool colinear = CGAL::squared_distance(he.supporting_line(), next.target()) < 1e-6;
		if (colinear)
		{
			// perturb in a direction perpendicular to the edge line
			auto v = he.to_vector().perpendicular(CGAL::COUNTERCLOCKWISE);

			auto sl = CGAL::to_double(v.squared_length());
			if (sl > r.perturb_epsilon*r.perturb_epsilon)
				v = v / CGAL::sqrt(sl);

			auto np = he.target() + v*r.perturb_epsilon;
			result.push_back(np);
		}
		else
			result.push_back(he.target());
	}
};

//inset
/*void build_inner_bisector(arrangement2& arr, face2 f, GeometryKernel::FT amount, SSEdge he, vertex2& right_vertex, vertex2& left_vertex)
{
	if (right_vertex != vertex2() && left_vertex != vertex2())
		return;

	CGAL::Cartesian_converter<SSKernel, GeometryKernel> converter;
	line2 rb(
		converter(he->vertex()->point()),
		converter(he->next()->vertex()->point()));

	line2 lb(
		converter(he->prev()->vertex()->point()),
		converter(he->prev()->prev()->vertex()->point()));

	point2 p1 = converter(he->prev()->vertex()->point());
	point2 p2 = converter(he->vertex()->point());

	vec2 v(p1, p2);
	v = v.perpendicular(CGAL::POSITIVE);
	v = v / CGAL::sqrt(CGAL::to_double(v.squared_length()));

	line2 il(p1 + v*amount, p2 + v*amount);

	if (right_vertex == vertex2())
	{
		CGAL::Object iobj = CGAL::intersection(rb, il);
		const point2* ipoint = CGAL::object_cast<point2>(&iobj); assert(ipoint);
		right_vertex = arr.insert_in_face_interior(*ipoint, f);
	}

	if (left_vertex == vertex2())
	{
		CGAL::Object iobj = CGAL::intersection(lb, il);
		const point2* ipoint = CGAL::object_cast<point2>(&iobj); assert(ipoint);
		left_vertex = arr.insert_in_face_interior(*ipoint, f);
	}
}

edge2 add_bisector(inset_request& request, vertex2 source, vertex2 target, bool right)
{
	if (!source->is_isolated())
	{
		auto it = source->incident_halfedges();
		auto nd = it;
		CGAL_For_all(it, nd)
		{
			if (it->source() == target)
				return it;
		}
	}

	auto he = request.arr.insert_at_vertices(
		segment2(source->point(), target->point()),
		source,
		target);

	if (right)
	{
		he->data().label = request.labels.right_edge;
		he->twin()->data().label = request.labels.left_edge;
	}
	else
	{
		he->data().label = request.labels.left_edge;
		he->twin()->data().label = request.labels.right_edge;
	}

	he->data().tag = inset_side_tag;
	he->twin()->data().tag = inset_side_tag;

	//mark all faces side and then the result faces will override
	he->face()->data().tag = inset_side_tag;
	he->twin()->face()->data().tag = inset_side_tag;
	return he;
}

vertex2 add_inner_bisector(inset_request& request, edge2 outer, vertex2 source, vertex2 target, int default_label)
{
	if (!source->is_isolated())
	{
		auto it = source->incident_halfedges();
		auto nd = it;
		CGAL_For_all(it, nd)
		{
			if (it->source() == target)
				return target;
		}
	}

	if (default_label < 0)
		default_label = outer->data().label;

	auto he = request.arr.insert_at_vertices(
		segment2(source->point(), target->point()),
		source,
		target);

	he->data().label = request.labels.inner_edge >= 0
		? request.labels.inner_edge
		: outer->data().label;

	he->twin()->data().label = request.labels.result_edge >= 0
		? request.labels.result_edge
		: outer->data().label;

	if (request.labels.outer_edge >= 0)
		outer->data().label = request.labels.outer_edge;

	he->face()->data().label = request.labels.side_face >= 0
		?  request.labels.side_face
		: default_label;

	he->twin()->face()->data().label = request.labels.result_face >= 0
		? request.labels.result_face
		: default_label;

	he->face()->data().tag = inset_side_tag;
	return target;
}*/

line2 cut_line(edge2 he, double amount)
{
	point2 p1 = he->source()->point();
	point2 p2 = he->target()->point();

	vec2 v(p1, p2);
	v = v.perpendicular(CGAL::POSITIVE);
	v = v / CGAL::sqrt(CGAL::to_double(v.squared_length()));

	auto pp1 = p1 + v*amount;
	auto pp2 = p2 + v*amount;
	return line2(p1 + v*amount, p2 + v*amount);
}

/*vertex2 cut_segment(edge2 edge, SSEdge he, vertex2 last, inset_request& request, line2& il)
{
	CGAL::Cartesian_converter<SSKernel, GeometryKernel> converter;
	line2 bl(
		converter(he->prev()->vertex()->point()),
		converter(he->vertex()->point()));

	CGAL::Object iobj = CGAL::intersection(bl, il);
	const point2* ipoint = CGAL::object_cast<point2>(&iobj); assert(ipoint);
	auto result = request.arr.insert_in_face_interior(*ipoint, edge->face());

	if (he->slope() == CGAL::POSITIVE)
		add_bisector(request, last, result, true);
	else
		add_bisector(request, result, last, false);

	return result;
}

vertex2 add_segment(std::map<SSVertex, vertex2>& cache, edge2 edge, SSEdge he, vertex2 last, inset_request& request)
{
	vertex2 result;
	SSVertex vertex = he->slope() == CGAL::POSITIVE
		? he->vertex()
		: he->prev()->vertex();

	auto it = cache.find(vertex);
	if (it == cache.end())
	{
		CGAL::Cartesian_converter<SSKernel, GeometryKernel> converter;
		point2 p = converter(vertex->point());
		result = request.arr.insert_in_face_interior(p, edge->face());
		cache[vertex] = result;
	}
	else
		result = it->second;

	if (he->slope() == CGAL::POSITIVE)
		add_bisector(request, last, result, true);
	else
		add_bisector(request, result, last, false);

	return result;
}*/

edge2 cut_edge(arrangement2& arr, edge2 he, line2& l)
{
	auto& p1 = he->source()->point();
	auto& p2 = he->target()->point();
	line2 ll(p1, p2);
	CGAL::Object iobj = CGAL::intersection(l, ll);
	const point2* ipoint = CGAL::object_cast<point2>(&iobj); assert(ipoint);
	if (!ipoint) {
		std::cout << "\ninset: not intersection\n";
		return he;
	}

	he = arr.split_edge(he, segment2(p1, *ipoint), segment2(*ipoint, p2));
	// keep labels
	auto he_next = he->next();
	he_next->data() = he->data();
	he_next->twin()->data() = he->twin()->data();
	return he;
}

edge2 _cut_face(arrangement2& arr, face2 face, line2 l, edge2 source, edge2 target)
{
	edge2 he1;
	if (CGAL::squared_distance(l, source->source()->point()) < 1e-8)
		he1 = source->prev();
	else if (CGAL::squared_distance(l, source->target()->point()) < 1e-8)
		he1 = source;
	else
		he1 = cut_edge(arr, source, l);

	edge2 he2;
	if (CGAL::squared_distance(l, target->source()->point()) < 1e-8)
		he2 = target->prev();
	else if (CGAL::squared_distance(l, target->target()->point()) < 1e-8)
		he2 = target;
	else
		he2 = cut_edge(arr, target, l);

	if (he1 == he2 || he2->source() == he1->target())
		return edge2();

	auto v1 = he1->target();
	auto v2 = he2->target();
	assert(v1 != v2);

	auto he = arr.insert_at_vertices(
		segment2(v1->point(), v2->point()),
		v1,
		v2);

	return he;
}

/*void apply_labels(edge2 he, edge2 outer, int default_label, inset_request& request, CGAL::Sign slope)
{
	switch (slope)
	{
		case CGAL::NEGATIVE:
			he->data().label = request.labels.left_edge;
			he->twin()->data().label = request.labels.right_edge;
			break;
		case CGAL::ZERO:
			he->data().label = request.labels.inner_edge >= 0
				? request.labels.inner_edge
				: default_label;

			he->twin()->data().label = request.labels.result_edge >= 0
				? request.labels.result_edge
				: default_label;

			if (request.labels.outer_edge >= 0)
				outer->data().label = request.labels.outer_edge;
			break;
		case CGAL::POSITIVE:
			he->data().label = request.labels.right_edge;
			he->twin()->data().label = request.labels.left_edge;
			break;
		default:
			assert(false);
			break;
	}
}*/

void load_svg(const char* filename, geometry::arrangement2& arr)
{
	property_tree pt;
	arr.clear();

	read_xml(filename, pt);
	auto path = pt.get<std::string>("svg.path.<xmlattr>.d", "");
	std::stringstream ss(path);
	point2_list points;
	char ch;
	ss >> ch;
	for (;!ss.eof();)
	{
		double x, y;
		ss >> x;
		if (ss.fail())
			break;

		ss >> ch;
		ss >> y;

		point2 pt;
		if (points.empty())
			pt = point2(x, 640 - y);
		else
			pt = point2(x + points.begin()->x(), points.begin()->y() - y);

		points.insert(points.begin(), pt);
		//std::cout << pt.x() << "," << pt.y() << "  ";
	}

	vertex2 first, last;
	bool bfirst = true;
	for (auto pt : points)
	{
		vertex2 v = arr.insert_in_face_interior(pt, arr.unbounded_face());

		if (bfirst)
			first = v;
		else
			arr.insert_at_vertices(curve2(last->point(), v->point()), last, v);

		bfirst = false;
		last = v;
	}

	arr.insert_at_vertices(curve2(last->point(), first->point()), last, first);
}

//#define DEBUG_INSET 1

bool inset::run(inset_request& request, debug_json* dj_, bool randomize)
{
#ifdef DEBUG_INSET
	static int INSET_DEBUG = 0;
	std::cout << "INSET_DEBUG = " << ++INSET_DEBUG << "\n";
#endif

	//geom_utils::print(request.f, "insetting face");
#ifdef DEBUG_INSET
	file_utils::save(request.arr, "c:\\dev\\inset_input.svg");
#endif

#if DEBUG_INSET
	if (!request.arr.is_valid())
	{
		std::cerr << "inset: invalid input arrangement\n";
		file_utils::save(request.arr, "c:\\dev\\inset_invalid.svg");
		return false;
	}
#endif

	auto default_label = request.f->data().label;

	// clear face tags
	geom_utils::tag(request.arr, 0);

	//load_svg("c:\\dev\\inset_input_simple.svg", request.arr);
	//io_utils<GeometryKernel, geometry::arrangement2, geometry::polyhedron3>::save(request.arr, "c:\\dev\\inset1.svg");
	/*Formatter formatter;
	std::ofstream out_file("c:\\dev\\inset_input.dat");
	CGAL::write(request.arr, out_file, formatter);
	out_file << request.amount;
	out_file.close();*/

	/*std::ifstream in_file("c:\\dev\\inset_input.dat");
	request.arr.clear();
	arrangement2 arr;
	CGAL::read(arr, in_file, formatter);
	//in_file >> request.amount;
	in_file.close();
	//auto kk = CGAL::is_valid(arr);
	bool valid = arr.is_valid();
	auto f = arr.vertices_begin()->incident_halfedges()->face();*/

	if (dj_)
	{
		dj_->add("amount", request.amount);
		dj_->add("input", request.f);
	}

	typedef cleanup_face2<GeometryKernel, geometry::arrangement2> cleanup2;
	struct merge_function
	{
		bool operator()(edge2 he1, edge2 he2)
		{
			return he1->data().label == he2->data().label && he1->twin()->data().label == he2->twin()->data().label;
		}
	};

	cleanup2::request creq;
	creq.degen_epsilon = 0.0001;
	creq.merge_predicate = merge_function();
	creq.remove_antennas = true;
	cleanup2::run(request.arr, request.f, creq);

#if DEBUG_INSET
	if (!request.arr.is_valid())
	{
		std::cerr << "inset2: invalid input arrangement (2)\n";
		file_utils::save(request.arr, "c:\\dev\\inset_invalid.svg");
		return false;
	}
#endif

	//if (dj_) dj_->add("arr_after_cleanup", request.arr);
	//file_utils::save(request.arr, "c:\\dev\\inset_cleanup.svg");

	Polygon_2 p;
	auto idx = 0;
	auto eit = request.f->outer_ccb();
	auto end = eit;

	edge2_list edges;
	std::map<edge2, int> original_labels;
	srand(123456);
	//srand(654321);
	CGAL_For_all(eit, end)
	{
		if (randomize)
		{
			auto pt = eit->target()->point();
			float x = (float)(CGAL::to_double(pt.x()) + 1e-5*(0.5 - ((double)rand()) / RAND_MAX));
			float y = (float)(CGAL::to_double(pt.y()) + 1e-5*(0.5 - ((double)rand()) / RAND_MAX));
			p.push_back(Polygon_2::Point_2(x, y));
		}
		else
		{
			CGAL::Cartesian_converter<GeometryKernel, SSKernel> converter;
			auto pp = converter(eit->target()->point());
			p.push_back(pp);
		}

		edges.push_back(eit);
		original_labels[eit] = eit->data().label;
	}

	Polygon_2 poly_filtered;
	if (randomize)
	{
		typedef perturb_poly2<SSKernel> perturb;
		perturb::request perturb_request;

		perturb::run(p, perturb_request, poly_filtered);
	}
	else
		poly_filtered = p;

	if (!poly_filtered.is_simple())
	{
		std::cout << "inset: poly_filtered is not simple\n";
		return false; //td: error
	}
	//std::cout << "begin skeleton\n";
	auto ss = CGAL::create_interior_straight_skeleton_2(poly_filtered, SSKernel());
	//auto ss = CGAL::create_interior_straight_skeleton_2(p, SSKernel());
	//std::cout << "end skeleton\n";

	//if (dj_)
	{
		//dj_->add("skeleton", ss);
#ifdef DEBUG_INSET
		file_utils::save<SSKernel>(*ss, "c:\\dev\\inset_skeleton.svg");//, save_request(NONE));
#endif

		//auto result = CGAL::create_offset_polygons_2(amount, *ss, SSKernel());
		//dj_->add("offset", result);

		if (!ss->is_valid())
		{
			//dj_->error("SKELETON NOT VALID");
			std::cout << "inset: skeleton not valid\n";
			return false; //td: error
		}
	}

	std::map<SSVertex, vertex2> vcache; // maps skeleton vertexs to arrangement vertexs

	// add non-borders skeleton vertices to the arrangement
	for(auto vit = ss->vertices_begin(); vit != ss->vertices_end(); vit++)
	{
		CGAL::Cartesian_converter<SSKernel, GeometryKernel> converter;
		if (vit->is_contour())
		{
			auto he = vit->halfedge()->defining_contour_edge();
			auto hee = edges[he->id() / 2];

			if (he->vertex() == vit)
				vcache[vit] = hee->target();
			else
				vcache[vit] = hee->source();

			continue;
		}

		// detect repeated vertices (O(n*n/2))
		bool repeated = false;
		for (auto vit2 = ss->vertices_begin(); vit2 != vit; vit2++)
		{
			//if (!vit2->is_contour() && vit2->point() == vit->point())
			if (!vit2->is_contour() && CGAL::squared_distance(vit2->point(), vit->point()) < 1e-15)
				{
				//std::cout << "vertex repeat" << std::endl;
				vcache[vit] = vcache[vit2]; // map to the original vertex, so no duplicates
				repeated = true;
				break;
			}
		}

		if (!repeated)
		{
			point2 pt = converter(vit->point());
			auto vv = request.arr.insert_in_face_interior(pt, request.f);
			vv->data().index = -1;
			vcache[vit] = vv;
		}
	}

	/*for (auto eit = ss->halfedges_begin(); eit != ss->halfedges_end(); eit++)
	{
		if (eit->is_border() || eit->opposite()->is_border())
			continue;

		auto seg1 = segment2(vcache[eit->prev()->vertex()]->point(), vcache[eit->vertex()]->point());

		auto eit2 = eit; eit2++;
		for (; eit2 != ss->halfedges_end(); eit2++)
		{
			auto seg2 = segment2(vcache[eit2->prev()->vertex()]->point(), vcache[eit2->vertex()]->point());
			CGAL::Object iobj = CGAL::intersection(seg1, seg2);
			const point2* ipoint = CGAL::object_cast<point2>(&iobj);
			if (ipoint && *ipoint != seg1.source() && *ipoint != seg1.target())
			{
				std::cout << "******************** intersection ***************\n";
				print(seg1);
				print(seg2);
				std::cout << "inter: ";
				print(*ipoint, "\n");
			}
		}
	}*/

	// add non-borders skeleton edges to the arrangement

	std::set<SSEdge> ecache; // cache to avoid processing opposites halfedges
	for (auto eit = ss->halfedges_begin(); eit != ss->halfedges_end(); eit++)
	{
		if (eit->is_border() || eit->opposite()->is_border())
			continue;

		auto cit = ecache.find(eit);
		if (cit != ecache.end())
			continue;  // ignore already processed halfedges (through their opposites)

		if (eit->prev()->vertex()->point() == eit->vertex()->point())
			continue; // ignore degenerated edges

		auto vs = vcache[eit->prev()->vertex()];
		auto vt = vcache[eit->vertex()];

		if (vs == vt)
			continue;

		//if (segment2(vs->point(), vt->point()).squared_length() < 1e-15)
		//if (CGAL::squared_distance(vs->point(), vt->point()) < 1e-15)
		//	continue;
		//file_utils::save(request.arr, "c:\\dev\\inset_arr_pre_error.svg");
		//geom_utils::print(segment2(vs->point(), vt->point()), "seg");
		auto inserted_edges = geom_utils::insert(request.arr, segment2(vs->point(), vt->point()));
		//auto he = request.arr.insert_at_vertices(segment2(vs->point(), vt->point()), vs, vt);

		// mark all faces as inside
		for (auto he : inserted_edges)
		{
			he->face()->data().tag = SIDE_TAG;
			he->twin()->face()->data().tag = SIDE_TAG;
		}

		ecache.insert(eit->opposite()); // so the opposite is not processed
	}

	//this should show the full skeleton
#ifdef DEBUG_INSET
	file_utils::save(request.arr, "c:\\dev\\inset_skeleton_arr.svg");
#endif
	//if (dj_) dj_->add("arr_skeleton1", request.arr);

	//apply amount to each border edge
	face2_list results;
	for (auto he_border : edges)
	{
		auto line = cut_line(he_border, request.amount);
		auto face = he_border->face();
		// cut 'face' by 'line'
		edge2 he_c1;
		edge2 he = he_border; // he always start on the negative side of the line
		//auto he_end = he_border;
		do
		{
			auto tsign = line.oriented_side(he->target()->point());
			if (he_c1 == edge2())
			{
				if (tsign == CGAL::ON_POSITIVE_SIDE)
					he_c1 = he;	// cut start
			}
			else if (tsign != CGAL::ON_POSITIVE_SIDE)
			{	// cut end
				auto cut = _cut_face(request.arr, face, line, he_c1, he); // apply the cut
				if (cut != edge2())
				{
					if (cut->face() != he_border->face())
						cut = cut->twin();

					cut->face()->data().tag = SIDE_TAG;

					auto result_edge = cut->twin();
					result_edge->data().tag = RESULT_TAG;

					//if (std::find(results.begin(), results.end(), result_edge->face()) == results.end())
					results.push_back(result_edge->face());
					he = cut; // continue from the cut
				}

				he_c1 = edge2();
			}

			he = he->next();
		} while (he != he_border);
	}

	//face cuts
	//io_utils<GeometryKernel, geometry::arrangement2, geometry::polyhedron3>::save(request.arr, "c:\\dev\\inset.svg");
	//if (dj_) dj_->add("face_cuts", request.arr);

	//clear result, remove edges inside the result faces
	edge2_list to_remove;
	for (auto fit : results)
	{
		auto eit = fit->outer_ccb();
		auto end = eit;
		CGAL_For_all(eit, end)
		{
			if (eit->data().tag == RESULT_TAG)
				continue;

			to_remove.push_back(eit);
			eit->twin()->data().tag = RESULT_TAG;
		}
	}

	//if (dj_) dj_->add("to_remove", to_remove);
	for (auto eit : to_remove)
	{
		auto f2 = request.arr.remove_edge(eit);
		f2->data().tag = RESULT_TAG;
	}

	//if (dj_) dj_->add("cleared", request.arr);

	// apply edge labels to sides and results
	// edge labels policy for sides:
	// bottom edge label = bottom_edge || side_edge
	// left edge label = left_edge || side_edge
	// right edge label = right_edge || side_edge
	// top edge label = top_edge || side_edge

	// edge labels policy for results (center faces):
	// edge label = result_edge || original_label

	for (auto eit : edges)
	{
		eit->data().label = request.labels.bottom_edge;
		eit->prev()->data().label = request.labels.left_edge;
		eit->next()->data().label = request.labels.right_edge;

		for (auto inner = eit->next()->next(); inner != eit->prev(); inner = inner->next())
		{
			inner->data().label = request.labels.top_edge;

			// result edges labels
			auto opp = inner->twin();
			if (opp->face()->data().tag == RESULT_TAG)
				opp->data().label = request.labels.result_edge >= 0 ? request.labels.result_edge : original_labels[eit];
		}
	}

	//by default, side faces will have the same label
	//as the originating segment
	for (auto eit : edges)
	{
		auto original_label = original_labels[eit];
		eit->face()->data().label = original_label;
	}

	//collect results
	for (auto fit = request.arr.faces_begin(); fit != request.arr.faces_end(); fit++)
	{
		if (is_unbounded_face(fit))
			continue;

		if (fit->data().tag == RESULT_TAG)
		{
			fit->data().label = default_label;

			if (request.labels.result_face >= 0)
				fit->data().label = request.labels.result_face;

			if (request.faces)
				request.faces->push_back(fit);
		}
		else if (fit->data().tag == SIDE_TAG)
		{
			int labelFromEdge = fit->data().label;
			fit->data().label = default_label;

			if (request.labels.side_face >= 0)
				fit->data().label = request.labels.side_face;
			else if (labelFromEdge >= 0)
				fit->data().label = labelFromEdge;

			if (request.sides)
				request.sides->push_back(fit);
		}
	}

	//finished
#ifdef DEBUG_INSET
	io_utils<GeometryKernel, geometry::arrangement2, geometry::polyhedron3>::save(request.arr, "c:\\dev\\inset_final.svg");
#endif
	//if (dj_) dj_->add("inset_final", request.arr);
	return true;
}

//void inset::run(inset_request& request)
//{
//	auto default_label = request.f->data().label;
//	auto amount = request.amount;
//
//	Polygon_2 p;
//	auto idx = 0;
//	auto vit = request.f->outer_ccb();
//	auto vnd = vit;
//
//	CGAL::Cartesian_converter<GeometryKernel, SSKernel> converter;
//	edge2_list edges;
//	CGAL_For_all(vit, vnd)
//	{
//		p.push_back(converter(vit->target()->point()));
//		edges.push_back(vit);
//	}
//
//	auto ss = CGAL::create_interior_straight_skeleton_2(p);
//
//	std::map<SSVertex, vertex2> cache;
//	std::map<vertex2, vertex2> cut_cache;
//	for (auto fit = ss->faces_begin(); fit != ss->faces_end(); fit++)
//	{
//		auto contour = fit->halfedge()->defining_contour_edge();
//		auto edge = edges[contour->id() / 2];
//		auto cl = cut_line(edge, request.amount);
//
//		//build right side
//		auto bisector = contour->next();
//		auto last = edge->target();
//		auto slope = bisector->slope();
//		auto has_right_cut = false;
//		while (bisector->slope() == slope)
//		{
//			auto v = bisector->vertex();
//			if (v->time() > request.amount)
//			{
//				auto it = cut_cache.find(edge->target());
//				if (it != cut_cache.end())
//					last = it->second;
//				else
//				{
//					has_right_cut = true;
//					last = cut_segment(edge, bisector, last, request, cl);
//					cut_cache[edge->target()] = last;
//				}
//			}
//			else
//				last = add_segment(cache, edge, bisector, last, request);
//
//			if (v->time() >= request.amount)
//				break;
//
//			bisector = bisector->next();
//		}
//
//		auto rlast = last;
//		auto inner = bisector;
//
//		//left side
//		bisector = contour->prev();
//		last = edge->source();
//		slope = bisector->slope();
//		auto has_left_cut = false;
//		while (bisector->slope() == slope)
//		{
//			auto v = bisector->prev()->vertex();
//			if (v->time() > request.amount)
//			{
//				auto it = cut_cache.find(edge->source());
//				if (it != cut_cache.end())
//					last = it->second;
//				else
//				{
//					has_left_cut = true;
//					last = cut_segment(edge, bisector, last, request, cl);
//					cut_cache[edge->source()] = last;
//				}
//			}
//			else
//				last = add_segment(cache, edge, bisector, last, request);
//
//			if (v->time() >= request.amount)
//				break;
//
//			bisector = bisector->prev();
//		}
//
//		auto llast = last;
//
//		//close, if needed
//		if (rlast != llast)
//		{
//			if (has_right_cut || has_left_cut)
//			{
//				add_inner_bisector(request, edge, rlast, llast, default_label);
//			}
//			else
//			{
//				//follow skeleton
//				assert(inner != SSEdge());
//				last = rlast;
//				while (inner->is_inner_bisector())
//				{
//					auto iit = cache.find(inner->vertex());
//					auto vv = vertex2();
//					if (iit == cache.end())
//					{
//						CGAL::Cartesian_converter<SSKernel, GeometryKernel> converter;
//						point2 p = converter(inner->vertex()->point());
//						vv = request.arr.insert_in_face_interior(p, edge->face());
//						cache[inner->vertex()] = vv;
//					}
//					else
//						vv = iit->second;
//
//					last = add_inner_bisector(request, edge, last, vv, default_label);
//					inner = inner->next();
//				}
//			}
//		}
//
//		edge->face()->data().tag = inset_side_tag;
//		io_utils<GeometryKernel, geometry::arrangement2, geometry::polyhedron3>::save(request.arr, "c:\\dev\\inset.svg");
//	}
//
//	for (auto fit = request.arr.faces_begin(); fit != request.arr.faces_end(); fit++)
//	{
//		if (fit->data().tag == inset_side_tag && request.sides)
//			request.sides->push_back(fit);
//		else if (fit->data().tag != inset_side_tag && request.faces)
//			request.faces->push_back(fit);
//	}
//}

