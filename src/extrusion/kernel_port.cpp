#include "phoenix/extrusion/kernel_support.hpp"

namespace phoenix::extrusion::detail {

// 5000m max. to remove large antennas
const double EXTRUDE_MAX_EDGE_LEN2 = 5000.0 * 5000.0;

struct preserved_extrude
{
	struct corner;
	typedef std::shared_ptr<corner> corner_ref;

	struct corner_collision
	{
		corner_collision() : vertex(-1)
		{
		}

		corner_collision(point3& point_, int vertex_) :
			point(point_),
			vertex(vertex_)
		{
		}

		point3 point;
		int    vertex;
	};

	typedef std::map<double, corner_collision> collision_map;

	enum CORNER_FLAG
	{
		CF_VERTICAL = 0x01,
		CF_HORIZONTAL = 0x02,
		CF_SKIRT = 0x04,
		CF_COMPLEX = 0x08
	};

	struct corner_labels
	{
		int label;
		int cap_label;
		int bottom_label;
		int right_label;
		int top_label;
		int left_label;
		int skirt_label;

		corner_labels(int label_, int cap_label_, int bottom_label_ = -1, int right_label_ = -1, int top_label_ = -1, int left_label_ = -1):
			label(label_),
			cap_label(cap_label_),
			bottom_label(bottom_label_),
			right_label(right_label_),
			top_label(top_label_),
			left_label(left_label_),
			skirt_label(-1)
		{
		}

		corner_labels(const corner_labels& other):
			label(other.label),
			cap_label(other.cap_label),
			bottom_label(other.bottom_label),
			right_label(other.right_label),
			top_label(other.top_label),
			left_label(other.left_label),
			skirt_label(other.skirt_label)
		{
		}
	};

	struct corner
	{
		int id;

		plane3		  plane;
		point3		  point;
		int			  vertex;
		int			  flags;
		corner_labels labels;

		corner(int id_, point3 p_, plane3 pl_, int flags_, corner_labels& labels_) :
			id(id_),
			point(p_),
			plane(pl_),
			flags(flags_),
			vertex(-1),
			labels(labels_)
		{
		}


		collision_map collisions;
		corner_ref prev;
		corner_ref next;

		bool is_horizontal()
		{
			return (flags & CF_HORIZONTAL) != 0;
		}

		bool is_vertical()
		{
			return (flags & CF_VERTICAL) != 0;
		}

		bool is_degenerate()
		{
			assert(false); //td:
			return false;
		}

		bool is_skirt()
		{
			return (flags & CF_SKIRT) != 0;
		}

		void add_collision(double d, corner_collision& c)
		{
			collisions.insert(std::pair<double, corner_collision>(d, c));
		}

		bool has(CORNER_FLAG flag)
		{
			return (flags & flag) != 0;
		}

		void set(CORNER_FLAG flag, bool value)
		{
			if (value)
				flags |= flag;
			else
				flags &= ~flag;
		}
	};

	struct plan_container
	{
		std::vector<corner_ref> plans;

		void add(corner_ref head)
		{
			plans.push_back(head);
		}

		corner_ref get(int id) const
		{
			auto it = corners.find(id);
			if (it != corners.end())
				return it->second;

			return corner_ref();
		}

		void clear()
		{
			corners.clear();
			plans.clear();
		}

		void register_corner(corner_ref c)
		{
			assert(corners.find(c->id) == corners.end());
			corners[c->id] = c;
		}

		size_t max_id()
		{
			size_t result = 0;
			for (auto& k : corners)
			{
				if (result < k.first)
					result = k.first;
			}

			return result + 1;
		}
	private: 
		std::map<int, corner_ref> corners;
	};

	struct input_point
	{
		input_point(point3 p_, profile_ref profile_, uniqueid cap_label_, uniqueid vertex_id_,
			uniqueid halfedge_id_, uniqueid edge_id_) :
			p(p_),
			profile(profile_),
			cap_label(cap_label_),
			vertex_id(vertex_id_),
			halfedge_id(halfedge_id_),
			edge_id(edge_id_),
			vertex_index(-1)
		{
		}

		point3 p;
		profile_ref profile;
		uniqueid vertex_id;
		uniqueid vertex_index;
		uniqueid halfedge_id;
		uniqueid edge_id;
		uniqueid cap_label;
	};

	struct input
	{
		typedef typename std::vector<input_point> input_polygon;
		typedef typename std::vector<input_polygon> input_polygons;

		input(CGAL::Sign sign_, int bottom_label_, int right_label_, int top_label_, int left_label_, int skirt_label_, int cap_label_):
			sign(sign_),
			bottom_label(bottom_label_),
			right_label(right_label_),
			top_label(top_label_),
			left_label(left_label_),
			skirt_label(skirt_label_),
			cap_label(cap_label_)
		{
		}

		input_polygons polygons;
		plane3 plane;
		CGAL::Sign sign;

		//default labels
		int bottom_label;
		int right_label;
		int top_label;
		int left_label;
		int skirt_label;
		int cap_label;
	};

	enum EXTRUDE_EVENT
	{
		EV_PROFILE = 1,
	};

	struct extrude_event;
	typedef std::shared_ptr<extrude_event> event_ref;

	struct extrude_event
	{
		extrude_event(int id_, int corner_id_, point3 point_) :
			id(id_),
			point(point_),
			corner_id(corner_id_)
		{
		}

		int id;
		int corner_id;
		point3 point;

		virtual event_ref clone() = 0;
	};

	struct profile_event : extrude_event
	{
		profile_event(profile_ref profile_, point3 pt, int corner_id, int index_):
			extrude_event((int)EV_PROFILE, corner_id, pt),
			profile(profile_),
			index(index_)
		{
		}

		profile_ref profile;
		int index;

		virtual event_ref clone()
		{
			return event_ref(new profile_event(profile, point, corner_id, index));
		}
	};

	struct advance_profile_result
	{
		advance_profile_result(int label, int cap_label) : 
			flags(0),
			abort(false),
			labels(label, cap_label)
		{
		}

		point3 p;
		plane3 plane;
		int flags;
		corner_labels labels;
		bool abort;
	};

	struct collision
	{
		point3 p;

		int source;
		int target;
		ExactKernel::FT distance;

		collision(point3 p_, int source_, int target_, ExactKernel::FT distance_) : 
			p(p_), 
			source(source_), 
			target(target_),
			distance(distance_)
		{
		}
	};

	typedef std::vector<collision> collision_list;

	struct extruder;
	static void run(input& input, incremental_builder& builder)
	{
		extruder e(input.plane, builder, input.left_label, input.right_label,
			input.bottom_label, input.top_label, input.skirt_label, input.cap_label);
		plan_container plan;
		e.build_initial_plan(input, plan);
		e.build(plan);
		e.apply_edge_ids(input);
	};

	struct extruder
	{
		typedef std::multimap<double, event_ref> event_map;
		typedef ExactProjector exact_projector;

		incremental_builder& builder;

		event_map events;
		event_map hevents;
		plane3 base;
		plane3 last_plane;
		vec3 normal;
		bool horizontal;
		plane3 horizontal_plane;
		double epsilon;
		int current_corner;
		int vertex_count;
		CGAL::Sign sign;
		int default_left;
		int default_right;
		int default_bottom;
		int default_top;
		int default_skirt;
		int default_cap;

		bool _debug;

		extruder(plane3 base_, incremental_builder& builder_, int default_left_, int default_right_, int default_bottom_, int default_top_, int default_skirt_, int default_cap_):
			builder(builder_),
			base(base_),
			last_plane(base_),
			horizontal_plane(base_),
			horizontal(false),
			current_corner(0),
			vertex_count(0),
			sign(CGAL::POSITIVE),
			epsilon(1e-10), //squared
			default_left(default_left_),
			default_right(default_right_),
			default_bottom(default_bottom_),
			default_top(default_top_),
			default_skirt(default_skirt_),
			default_cap(default_cap_),
			_debug(false)
		{
		}

		void add_event(event_ref ev)
		{
			auto d = CGAL::squared_distance(base, ev->point);
			events.insert(std::pair<double, event_ref>(d, ev));
		}

		void add_hevent(event_ref ev)
		{
			auto d = CGAL::squared_distance(base, ev->point);
			hevents.insert(std::pair<double, event_ref>(d, ev));
		}

		void build(plan_container& plan)
		{
			while (!plan.plans.empty())
			{
				plan_container next_plan;
				advance(plan, next_plan);

				plan = next_plan;
			}
		}

		void print_corner(corner_ref c)
		{
			(void)c;
		}

		void print_plan(plan_container& plan, bool force = false)
		{
			(void)plan;
			(void)force;
		}

		void default_labels(corner_labels& labels)
		{
			if (labels.top_label < 0)
				labels.top_label = default_top;
			if (labels.bottom_label < 0)
				labels.bottom_label = default_bottom;
			if (labels.left_label < 0)
				labels.left_label = default_left;
			if (labels.right_label < 0)
				labels.right_label = default_right;
		}

		void apply_edge_ids(input& input)
		{
			for (auto p : input.polygons)
			{
				for (int i = 0; i < p.size(); i++)
				{
					auto& curr = p[i];
					auto& prev = p[i == 0 ? p.size() - 1 : i - 1];

					if (prev.vertex_index >= 0 && curr.vertex_index >= 0)
					{
						builder.set_vertex_id(static_cast<OutputVertexIndex>(curr.vertex_index),
							VertexId{static_cast<std::uint64_t>(curr.vertex_id)});
						if (curr.halfedge_id >= 0)
							builder.set_halfedge_id_by_vertices(
								static_cast<OutputVertexIndex>(prev.vertex_index),
								static_cast<OutputVertexIndex>(curr.vertex_index),
								HalfedgeId{static_cast<std::uint64_t>(curr.halfedge_id)});
						if (curr.edge_id >= 0)
							builder.set_edge_id_by_vertices(
								static_cast<OutputVertexIndex>(prev.vertex_index),
								static_cast<OutputVertexIndex>(curr.vertex_index),
								EdgeId{static_cast<std::uint64_t>(curr.edge_id)});

					}
				}
			}
		}

		void build_initial_plan(input& i, plan_container& plan)
		{
			auto base_plane = i.plane;
			bool first = true;

			for (auto& pol : i.polygons)
			{
				corner_ref head;
				corner_ref prevc;
				for (int i = 0; i < pol.size(); i++)
				{
					auto& p = pol[i];
					auto& prev = i == 0 ? pol[pol.size() - 1] : pol[i - 1];
					auto profile = p.profile; 

					if (CGAL::squared_distance(p.p, prev.p) < epsilon)
						continue;

					if (profile)
					{
						auto ps = profile->sign();
						if (first)
						{
							first = false;
							sign = ps;
							//if (sign == CGAL::NEGATIVE)
							//{
							//	base = base.opposite();
							//}
						}
						else if (ps != sign)
						{
							//td: error
							profile = profile_ref();
						}
					}

					advance_profile_result apr(p.cap_label, p.cap_label);
					bool horz = false;

					if (profile)
					{
						if (advance_profile(profile, 0, prev.p, p.p, apr))
							horz = (apr.flags & CF_HORIZONTAL) != 0;
						else
							profile = profile_ref();
					}
					
					if (!profile)
					{
						apr.flags = CF_VERTICAL;
						apr.plane = plane3(prev.p, p.p, p.p + base_plane.orthogonal_vector());
						default_labels(apr.labels);
					}
					
					auto c = create_corner(p.p, apr.plane, apr.flags, apr.labels);
					p.vertex_index = vertex_from_corner(c); 

					plan.register_corner(c);

					if (profile)
					{
						if (horz)
						{
							horizontal = true;
							add_hevent(event_ref(new profile_event(profile, apr.p, c->id, 1)));
						}
						else
							add_event(event_ref(new profile_event(profile, apr.p, c->id, 1)));
					}

					if (!head)
					{
						head = c;
					}
					else
					{
						prevc->next = c;
						c->prev = prevc;
					}

					prevc = c;
				}

				head->prev = prevc;
				prevc->next = head;
				plan.add(head);
			}
		}

		bool next_event(point3& point)
		{
			event_map& evs = horizontal ? hevents : events;

			if (evs.empty())
				return false;
			
			const auto& ev = evs.begin();
			point = ev->second->point;
			return true;
		}

		point3 infinite_point()

		{
			vec3 normal = base.orthogonal_vector();
			geometry_math::normalize(normal);
			return base.point() + normal * 1e10;
		}

		void advance(plan_container& plan, plan_container& next_plan)
		{
			//print_plan(plan);

			point3 next_ev;
			bool has_event = next_event(next_ev);
			if (!has_event)
				next_ev = infinite_point();

			//std::cout << "next_ev: " << next_ev << std::endl;

			//get the next scheduled height
			plane3 next_plane = plane3(next_ev, base.orthogonal_vector());

			//find collisions, create a new plan based on the new height
			auto old_horz = horizontal;
			auto abort = false;

			//print_plan(plan);
			if (!advance_plan(plan, next_plane, next_plan, abort))
			{
				//if we can not advance just close the plans and bail
				for (auto h : plan.plans)
					close_plan(h);

				next_plan.clear();
				return;
			}

			//create geometry, must work with the previous horizontal state
			auto new_horz = horizontal;
			horizontal = old_horz;

			//print_plan(next_plan);
			tesselate(plan, next_plan);

#ifdef DEBUG_EXTRUDE
			if (_debug)
				io_utils<GeometryKernel, geometry::arrangement2, geometry::polyhedron3>::save(mesh, "c:\\dev\\extrude.off");
#endif // DEBUG

			//then restore
			horizontal = new_horz;
			if (!old_horz && new_horz)
				horizontal_plane = next_plane;

			//calculate final next plan
			build_next_plan(next_plan, next_plane);

			//handle abort request
			if (abort)
			{
				for (auto h : next_plan.plans)
					close_plan(h);

				next_plan.clear();
			}

			if (old_horz && !new_horz)
				last_plane = horizontal_plane;
			else
				last_plane = next_plane;
		}

		corner_ref clone_corner(corner_ref  c)
		{
			auto result = corner_ref(new corner(c->id, c->point, c->plane, c->flags & ~CF_SKIRT, c->labels));
			return result;
		}

		point3 intersect_planes(plane3 p1, plane3 p2, plane3 p3)
		{
			CGAL::Cartesian_converter<DoubleKernel, ExactKernel> ek;
			CGAL::Object iobj = CGAL::intersection(ek(p1), ek(p2), ek(p3));
			const exact_point3* ip = CGAL::object_cast<exact_point3>(&iobj); assert(ip); 

			CGAL::Cartesian_converter<ExactKernel, DoubleKernel> dk;
			return dk(*ip);
		}

		bool is_skirt(corner_ref c, plane3 next_plane, plane3& result)
		{
			plane3 pl;
			if (!is_colinear(c, &pl))
				return false;

			auto p = intersect_planes(corner_plane(c->next), pl, next_plane);
			auto pp = corner_plane_intersection(c, next_plane);
			if (same_point(p, pp))
				return false;

			result = pl;
			return true;
		}

		void join_corner(corner_ref c)
		{
			if (c->vertex < 0)
			{
				if (c->next->vertex >= 0)
				{
					c->vertex = c->next->vertex;
					c->point = c->next->point;
				}
				else
				{
					c->vertex = create_vertex(c->point);
					c->next->vertex = c->vertex;
					c->next->point = c->point;
				}
			}
			else if (c->next->vertex != c->vertex)
			{
				assert(c->next->vertex < 0); //disconnected same point vertices
				c->next->vertex = c->vertex;
				c->next->point = c->point;
			}
		}

		bool is_degenerate(corner_ref c)
		{
			return c->vertex >= 0 && c->vertex == c->prev->vertex;
		}

		bool create_new_plan(plan_container& plan, plane3 next_plane, plan_container& next_plan, collision_list& collisions)
		{
			for (auto& c : plan.plans)
			{
				corner_ref head, curr;

				auto pit = c;
				auto pnd = c;

				do
				{
					auto nc = clone_corner(pit);
					next_plan.register_corner(nc);

					nc->point = corner_plane_intersection(pit, next_plane);
					if (CGAL::squared_distance(pit->point, nc->point) > EXTRUDE_MAX_EDGE_LEN2) //try to catch antenas due to bad intersections. 100m is arbitrary
					{
						return false;
					}

					if (!head)
					{
						head = nc;
					}
					else
					{
						curr->next = nc;
						nc->prev = curr;
					}

					curr = nc;
					pit = pit->next;
				} while (pit != pnd);

				head->prev = curr;
				curr->next = head;
				
				//once the new plan is created, we need to eliminate degenerate segments
				pit = head;
				pnd = head;

				do
				{
					if (CGAL::squared_distance(pit->point, pit->next->point) < 1e-7)
					{
						join_corner(pit);
					}
					
					pit = pit->next;
				} while (pit != pnd);

				next_plan.plans.push_back(head);
			}

			return true;
		}

		exact_segment2 collision_segment(corner_ref c, plane3 plane)
		{
			auto p1 = plane.to_2d(c->point);
			auto pp = corner_plane_intersection(c, plane);
			auto p2 = plane.to_2d(pp);

			CGAL::Cartesian_converter<DoubleKernel, ExactKernel> ek;
			return exact_segment2(ek(p1), ek(p2));
		}

		//exact_segment2 add_skirt_collision(extrusion_collider& collider, corner_ref c, exact_segment2 prev_seg, plane3 next_plane)
		//{
		//	auto result = collision_segment(c, next_plane);
		//	if (CGAL::squared_distance(result.source(), result.target()) < epsilon)
		//	{
		//		return prev_seg;
		//	}

		//	collider.add(prev_seg.source(), result.target(), c->id);
		//	return exact_segment2(prev_seg.source(), result.target());
		//}

		bool valid_intersection(corner_ref c1, corner_ref c2)
		{
			auto next = c1->next->is_skirt() ? c1->next->next : c1->next;
			if (c2 == next)
				return false;

			auto prev = c1->prev->is_skirt() ? c1->prev->prev : c1->prev;
			if (c2 == prev && (c1->is_vertical() || c2->is_vertical()))
				return false;

			return true;
		}

		profile_ref corner_profile(corner_ref c, int& idx, point3& p)
		{
			for (auto& ev : events)
			{
				if (ev.second->id == EV_PROFILE && ev.second->corner_id == c->id)
				{
					profile_event* pev = (profile_event*)ev.second.get();
					idx = pev->index;
					p = pev->point;
					return pev->profile;
				}
			}

			for (auto& ev : hevents)
			{
				if (ev.second->id == EV_PROFILE && ev.second->corner_id == c->id)
				{
					profile_event* pev = (profile_event*)ev.second.get();
					idx = pev->index;
					p = pev->point;
					return pev->profile;
				}
			}

			return profile_ref();
		}

		profile_ref create_skirt_profile(corner_ref c, point3& profile_point, int& profile_index, int& skirt_label)
		{
			//return profile_ref(); //break in case of emergency
			int idx = -1;
			profile_ref p = corner_profile(c, idx, profile_point);
			if (!p)
				return profile_ref();

			bool horizontal_segment = p->delta(idx - 1) == 0;

			if (horizontal_segment)
			{
				//need to calculate the real profile point on horizontals
				auto next_delta = 0.0;
				for (; idx < p->size(); idx++)
				{
					next_delta = p->delta(idx);
					if (next_delta != 0)
						break;
				}

				if (next_delta == 0)
					return profile_ref();
				
				advance_profile_result ar(c->labels.label, c->labels.cap_label);
				if (!advance_profile(p, idx, c->prev->point, c->point, ar))
					return profile_ref();

				profile_point = ar.p;
			}
			else
			{
				idx--; //when in doubt
			}

			std::vector<ProfileSegment> segments;
			auto found = false;
			auto p_index = 0;
			for (int i = 0; i < p->size(); i++)
			{
				auto delta = p->delta(i);
				if (delta == 0)
					continue;

				auto sl = p->segment(i).skirt_label;
				if (sl.is_registered())
					found = true;
				else
					sl = LabelId{default_skirt};

				segments.push_back(ProfileSegment{0, delta, sl,
					LabelId{default_left}, LabelId{default_bottom}, LabelId{default_right},
					LabelId{default_top}, sl, false});

				p_index++;
				if (idx == i)
				{
					profile_index = p_index;
					skirt_label = sl.value();
				}
			}

			if (!found || segments.empty())
				return profile_ref();

			return Profile::create(std::move(segments));
		}

		corner_ref create_skirt(corner_ref c, plane3 sp)
		{
			point3 profile_point;
			int profile_index = -1;
			int skirt_label = c->labels.skirt_label;
			profile_ref skirt_profile = create_skirt_profile(c, profile_point, profile_index, skirt_label);
			if (!skirt_profile)
			{
				skirt_label = c->next->labels.skirt_label;
				skirt_profile = create_skirt_profile(c->next, profile_point, profile_index, skirt_label);
			}

			if (skirt_label < 0)
				skirt_label = default_skirt;

			//if (skirt_label < 0)
			//	skirt_label = c->labels.label;
 
			corner_labels labels(skirt_label, skirt_label, default_bottom, default_right, default_top, default_left);
			auto skirt = create_corner(c->point, sp, CF_VERTICAL | CF_SKIRT, labels);
			skirt->vertex = vertex_from_corner(c);

			if (skirt_profile && profile_index >= 0)
			{
				add_event(event_ref(new profile_event(skirt_profile, profile_point, skirt->id, profile_index)));
			}

			return skirt;
		}

		bool brute_force_collisions(plan_container& plan, plane3 base, plane3& next_plane, collision_list& results, point3& collision_point)
		{
			CGAL::Cartesian_converter<DoubleKernel, ExactKernel> ek;
			auto bp = ek(base);

			std::vector<corner_ref> all_corners;
			for (auto pl : plan.plans)
			{
				auto pit = pl;
				auto pnd = pl;

				do
				{
					all_corners.push_back(pit);
					pit = pit->next;
				} while (pit != pnd);
			}
			
			bool has_collision_point = false;
			for (int i = 0; i < all_corners.size(); i++)
			{
				auto c = all_corners[i];
				auto cl = corner_line(c);

				for (int j = 0; j < all_corners.size(); j++)
				{
					if (i == j)
						continue;

					auto cc = all_corners[j];
					if (cc == c->next)
						continue;

					if (CGAL::squared_distance(c->point, cc->point) < epsilon)
						continue;

					CGAL::Object iobj = CGAL::intersection(cl, ek(corner_plane(cc)));
					const exact_point3* ip = CGAL::object_cast<exact_point3>(&iobj); 

					if (ip)
					{
						auto pp = *ip;
						auto d = CGAL::squared_distance(pp, bp);

						CGAL::Cartesian_converter<ExactKernel, DoubleKernel> dk;
						auto p = dk(*ip);

						//std::cout << "collision_point: " << *ip << std::endl;

						//behind the base or too close to it, ignore
						if (sign == CGAL::NEGATIVE)
						{
							if (d < 1e-12 || bp.has_on_positive_side(pp))
								continue;
						}
						else
						{
							if (d < 1e-12 || bp.has_on_negative_side(pp))
								continue;
						}

						bool is_clustered = CGAL::squared_distance(p, next_plane) < epsilon;
						bool is_better = sign == CGAL::NEGATIVE ? next_plane.has_on_positive_side(p) : next_plane.has_on_negative_side(p);
						if (!is_clustered && !is_better)
							continue;

						auto ppl = plane3(p, base.orthogonal_vector());
						auto p1 = corner_plane_intersection(cc, ppl);
						auto p2 = corner_plane_intersection(cc->prev, ppl);

						//verify the collision is within range
						if (CGAL::squared_distance(p, segment3(p1, p2)) > epsilon)
							continue;

						//std::cout << "actual: " << p << std::endl;

						bool found = true;
						if (is_clustered)
						{
							//group collisions
							if (!has_collision_point || CGAL::squared_distance(base, p) < CGAL::squared_distance(base, collision_point))
							{
								has_collision_point = true;
								collision_point = p;
							}

							results.push_back(collision(p, c->id, cc->id, d));
						}
						else if (is_better)
						{
							//a new candidate
							has_collision_point = true;
							collision_point = p;

							results.clear();
							results.push_back(collision(p, c->id, cc->id, d));
						}
						else
							found = false;

						if (found)
							next_plane = plane3(collision_point, base.orthogonal_vector());
					}
				}
			}

			return !results.empty();
		}

		void update_skirts(plan_container& plan, plane3 plane)
		{
			for (auto& c : plan.plans)
			{
				//make sire we don't start on a colinear
				auto pit = c;
				auto pnd = c;
				do
				{
					if (!is_colinear(pit->prev, nullptr))
						break;

					pit = pit->next;
				} while (pit != pnd);

				pnd = pit;

				do
				{
					plane3 sp;
					if (is_skirt(pit, plane, sp))
					{
						auto skirt = create_skirt(pit, sp);
						skirt->vertex = vertex_from_corner(pit);
						skirt->next = pit->next;
						skirt->prev = pit;
						pit->next->prev = skirt;
						pit->next = skirt;

						plan.register_corner(skirt);
					}

					pit = pit->next;
				} while (pit != pnd);
			}
		}

		//bool overlay_collisions(plan_container& plan, plane3 base, plane3& next_plane, extrusion_collider::collision_list& collisions, point3& collision_point)
		//{
		//	extrusion_collider collider(next_plane, last_plane, sign);
		//	for (auto& c : plan.plans)
		//	{
		//		//make sire we don't start on a colinear
		//		auto pit = c;
		//		auto pnd = c;
		//		do
		//		{
		//			if (!is_colinear(pit->prev, nullptr))
		//				break;

		//			pit = pit->next;
		//		} while (pit != pnd);

		//		pnd = pit;

		//		auto last_seg = collision_segment(pit->prev, next_plane);
		//		auto prev_seg = last_seg;
		//		do
		//		{
		//			plane3 sp;
		//			if (is_skirt(pit, next_plane, sp))
		//			{
		//				auto skirt = create_skirt(pit, sp);
		//				skirt->vertex = vertex_from_corner(pit);
		//				skirt->next = pit->next;
		//				skirt->prev = pit;
		//				pit->next = skirt;

		//				plan.register_corner(skirt);
		//			}

		//			bool last = pit->next == pnd;

		//			exact_segment2 seg;
		//			if (pit->is_skirt())
		//				seg = add_skirt_collision(collider, pit, prev_seg, next_plane);
		//			else
		//			{
		//				seg = last ? last_seg : collision_segment(pit, next_plane);
		//				add_to_collider(collider, pit, prev_seg, seg);
		//			}

		//			prev_seg = seg;
		//			pit = pit->next;
		//		} while (pit != pnd);
		//	}

		//	extrusion_collider::collision_eval_fn eval_fn = [this, plan, base](int c1, int c2, point3& result)
		//	{
		//		auto cc1 = plan.get(c1); assert(cc1);
		//		auto cc2 = plan.get(c2); assert(cc2);

		//		if (!valid_intersection(cc1, cc2))
		//			return false;

		//		if (cc1 == cc2)
		//			cc2 = cc1->prev; //self intersection

		//		if (!corner_intersects_plane(cc1, corner_plane(cc2)))
		//			return false;

		//		result = corner_plane_intersection(cc1, corner_plane(cc2));

		//		//check if the collision is in the target's range
		//		auto pl = plane3(result, base.orthogonal_vector());
		//		auto p1 = corner_plane_intersection(cc2, pl);
		//		auto p2 = corner_plane_intersection(cc2->prev, pl);
		//		auto d = CGAL::squared_distance(p1, p2);

		//		if ((CGAL::squared_distance(p1, result) > epsilon) && (CGAL::squared_distance(p2, result) > epsilon))
		//		{
		//			if ((CGAL::squared_distance(p1, result) > d) || (CGAL::squared_distance(p2, result) > d))
		//				return false;
		//		}


		//		return true;
		//	};

		//	collider.get_collisions(eval_fn, collisions, collision_point);
		//	return !collisions.empty();
		//}
		
		//bool degenerate_line(corner_ref c, exact_line3& l)
		//{
		//	CGAL::Cartesian_converter<DoubleKernel, ExactKernel> ek;
		//	CGAL::Cartesian_converter<ExactKernel, DoubleKernel> dk;
		//	CGAL::Object iobj = CGAL::intersection(ek(corner_plane(c)), ek(corner_plane(c->next)));
		//	const exact_line3* iline = CGAL::object_cast<exact_line3>(&iobj);

		//	if (iline)
		//	{
		//		l = *iline;


		//		//vec3 v1 = vec3(c->point, dk(l.point(1)));
		//		//vec3 v2 = vec3(c->point, c->point + last_plane.orthogonal_vector())
		//		auto p1 = l.projection(ek(c->prev->point));
		//		auto p2 = l.projection(ek(c->next->point));

		//		std::cout << "p1: " << p1 << " p2: " << p2 << std::endl;

		//		return CGAL::squared_distance(last_plane, dk(p1)) < epsilon
		//			&& CGAL::squared_distance(last_plane, dk(p2)) < epsilon;
		//	}

		//	return true;
		//}

		//void update_plan(plan_container& plan, plane3& next_plane)
		//{
		//	for (auto& c : plan.plans)
		//	{
		//		//make sire we don't start on a colinear
		//		auto pit = c;
		//		auto pnd = c;
		//		do
		//		{
		//			exact_line3 l;
		//			if (is_colinear(pit, nullptr))
		//			{
		//				if (degenerate_line(pit, l))
		//				{
		//					std::cout << "colinear degenerate" << std::endl;
		//				}
		//				else
		//				{
		//					std::cout << "colinear non - degenerate" << std::endl;
		//				}
		//			}

		//			if (degenerate_line(pit, l))
		//			{
		//				plane3 ortho;
		//				if (is_colinear(pit, &ortho))
		//				{
		//					std::cout << "degenerate colinear" << std::endl;
		//				}
		//				//else if (is_antenna(pit))
		//				//{
		//				//}
		//				else
		//				{
		//					std::cout << "non colinear degenerate" << std::endl;
		//					//td: why
		//				}
		//			}

		//			pit->line = l;
		//			pit = pit->next;
		//		} while (pit != pnd);
		//	}
		//}

		bool advance_plan(plan_container& plan, plane3& next_plane, plan_container& next_plan, bool& abort)
		{
			bool has_events = !events.empty() || !hevents.empty();

			//check plan integrity
			update_skirts(plan, next_plane);

			//collide
			collision_list collisions;
			point3 collision_point;
			auto collided = brute_force_collisions(plan, last_plane, next_plane, collisions, collision_point);
			if (collided)
			{
				if (!has_events)
				{
					//we are closing a plan, sometimes it extrudes to infinity and we want to avoid that
					//when we get a sensible use case we can make this configurable
					auto d = CGAL::squared_distance(last_plane, collision_point);
					if (d > EXTRUDE_MAX_EDGE_LEN2) //100m arbitrary
						collided = false;
				}
				else
				{
					//std::cout << "collision_point: " << collision_point << std::endl;
					next_plane = plane3(collision_point, next_plane.orthogonal_vector());
				}
			}

			if (!create_new_plan(plan, next_plane, next_plan, collisions))
			{
				abort = true;
				return false;
			}

			if (collided)
				apply_collisions(next_plan, collisions, next_plane);

			process_events(next_plan, next_plane, abort);

			if (horizontal && hevents.empty())
			{
				if (events.empty())
				{
					//nothing else to do, close
					abort = true;
				}
				else
				{
					horizontal = false;

					//after we switch from horizontal we need to check for existing events at the location
					process_events(next_plan, horizontal_plane, abort);

					project_plan(next_plan);
				}
			}

			if (!collided && !has_events)
			{
				//nothing left to do
				next_plan.clear();
			}

			return true;
		}

		void project_plan(plan_container& plan)
		{
			for (auto& c : plan.plans)
			{
				auto pit = c;
				auto pnd = c;

				do
				{
					pit->point = horizontal_plane.projection(pit->point);
					for (auto& cc : pit->collisions)
					{
						cc.second.point = horizontal_plane.projection(cc.second.point);
					}

					pit = pit->next;
				} while (pit != pnd);
			}
		}

		bool is_colinear(corner_ref c, plane3* plane)
		{
			if (same_point(c->point, c->next->point) || same_point(c->point, c->prev->point))
				return false; //this should not typically happen, but it does with skirts
			
			static const double colinear_epsilon = 1e-7;
			line3 l(c->prev->point, c->next->point);

			//std::cout << "colinear distance: " << std::setprecision(10) << CGAL::squared_distance(c->point, l) << " for: " << c->id << std::endl;
			auto d = CGAL::squared_distance(c->point, l);
			if (d > colinear_epsilon)
				return false;

			auto angle = geometry_math::angle_between(vec3(c->point, c->prev->point), vec3(c->point, c->next->point));
			if (std::abs(3.14159265358979323846 - angle) > 1e-2)
				return false;

			if (plane)
				*plane = plane3(c->point, c->next->point - c->point);

			return true;
		}

		plane3 corner_plane(corner_ref c)
		{
			//in horizontal mode non-horizontal corners behave like vertical corners
			if (horizontal && !c->is_horizontal() && !c->is_vertical())
				return plane3(c->point, c->prev->point, c->point + base.orthogonal_vector());

			return c->plane;
		}

		exact_line3 corner_line(corner_ref c)
		{
			plane3 ortho;
			plane3 tc = corner_plane(c->next);
			if (is_colinear(c, &ortho))
			{
				tc = ortho;
			}

			CGAL::Cartesian_converter<DoubleKernel, ExactKernel> ek;
			CGAL::Object iobj = CGAL::intersection(ek(corner_plane(c)), ek(tc));
			const exact_line3* iline = CGAL::object_cast<exact_line3>(&iobj); 
			if (!iline)
			{
				throw kernel_failure(KernelErrorCode::unhandled_edge_case, "an unhandled edge case has been found.");
			}
			
			return *iline;
		}

		bool corner_intersects_plane(corner_ref c, plane3 plane)
		{
			auto l = corner_line(c);

			CGAL::Cartesian_converter<DoubleKernel, ExactKernel> ek;
			CGAL::Object iobj = CGAL::intersection(l, ek(plane));
			const exact_point3* ipoint = CGAL::object_cast<exact_point3>(&iobj);
			return ipoint != 0;
		}

		point3 corner_plane_intersection(corner_ref c, plane3 plane)
		{
			CGAL::Cartesian_converter<DoubleKernel, ExactKernel> ek;

			auto l = corner_line(c);
			CGAL::Object iobj = CGAL::intersection(l, ek(plane));
			const exact_point3* ipoint = CGAL::object_cast<exact_point3>(&iobj); 
			if (!ipoint)
			{
				throw kernel_failure(KernelErrorCode::unhandled_edge_case, "an unhandled edge case has been found.");
			}

			CGAL::Cartesian_converter<ExactKernel, DoubleKernel> dk;
			return dk(*ipoint);
		}

		//void add_corner_to_collider(extrusion_collider& collider, corner_ref c, exact_segment2 prev, exact_segment2 seg)
		//{
		//	auto p1 = prev.source();
		//	auto p2 = seg.source();
		//	auto p3 = seg.target();
		//	auto p4 = prev.target();

		//	collider.add(p1, p2, p3, p4, c->id);
		//}

		//void add_line_to_collider(extrusion_collider& collider, corner_ref c, exact_segment2 prev, exact_segment2 seg)
		//{
		//	auto p1 = prev.source();
		//	auto p2 = seg.target();

		//	if (CGAL::squared_distance(p1, p2) > epsilon)
		//		collider.add(p1, p2, c->id);
		//	else
		//		std::cout << "why" << std::endl;
		//}

		//void add_to_collider(extrusion_collider& collider, corner_ref c, exact_segment2 prev, exact_segment2 seg)
		//{
		//	if (horizontal)
		//	{
		//		if (c->is_horizontal())
		//			add_corner_to_collider(collider, c, prev, seg);
		//		else
		//			add_line_to_collider(collider, c, prev, seg);
		//	}
		//	else
		//	{
		//		if (c->is_vertical())
		//			add_line_to_collider(collider, c, prev, seg);
		//		else
		//			add_corner_to_collider(collider, c, prev, seg);
		//	}
		//}

		int create_vertex(point3 p)
		{
			if (horizontal)
				p = horizontal_plane.projection(p);

			builder.add_vertex({CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z())});

			//std::cout << "VERTEX: " << vertex_count << " at: " << p << std::endl;
			return vertex_count++;
		}

		bool same_point(point3 p1, point3 p2)
		{
			return CGAL::squared_distance(p1, p2) < epsilon;
		}

		void create_collision_vertex(corner_ref source, corner_ref target, corner_ref third, point3& p)
		{
			if (source->vertex >= 0)
			{
				create_collision_vertex(source, target, p);
				create_collision_vertex(source, third, p);
			}
			else
			{
				create_collision_vertex(target, third, p);
				source->vertex = target->vertex;
				source->point = target->point;

			}
		}

		bool corners_overlap(corner_ref source, corner_ref target)
		{
			CGAL::Cartesian_converter<DoubleKernel, ExactKernel> ek;
			auto p = ek(source->point);
			auto p1 = ek(source->prev->point);
			auto p2 = ek(target->prev->point);

			if (CGAL::squared_distance(p, p1) < epsilon)
				return false;

			auto l = exact_line3(p, p1);
			if (CGAL::squared_distance(p2, l) > epsilon)
				return false;

			auto pl = exact_plane3(p, exact_vec3(p, p1));
			return pl.has_on_positive_side(p2);
		}

		void create_collision_vertex(corner_ref source, corner_ref target, point3& p)
		{
			if (target->vertex >= 0 && target->vertex == source->vertex)
				return; //nothing to do

			if (corners_overlap(source, target))
			{
				source->set(CF_COMPLEX, true);
				target->set(CF_COMPLEX, true);
				target->point = source->point;
				return;
			}


			if (source->vertex >= 0)
			{
				target->vertex = source->vertex;
				target->point = source->point;
				p = source->point;
			}
			else if (target->vertex >= 0)
			{
				source->vertex = target->vertex;
				source->point = target->point;
				p = target->point;
			}
			else
			{
				int result = create_vertex(p);
				source->vertex = result;
				source->point = p;
				target->vertex = result;
				target->point = p;
			}
		}

		void validate_collisions(collision_list& collisions, collision_list& result, plane3 plane)
		{
			//CGAL::Cartesian_converter<DoubleKernel, ExactKernel> ek;
			std::map<int, ExactKernel::FT> mindist;
			for (auto& c : collisions)
			{
				auto it = mindist.find(c.source);
				if (it == mindist.end() || it->second > c.distance)
				{
					mindist[c.source] = c.distance;
				}
			}

			for (auto& c : collisions)
			{
				auto dds = mindist[c.source];
				if (c.distance <= dds)
					result.push_back(c);
			}
		}

		void apply_collisions(plan_container& plan, collision_list& collisions, plane3& plane)
		{
			collision_list final_collisions;
			validate_collisions(collisions, final_collisions, last_plane);


			typedef std::tuple<corner_ref, corner_collision> collision_item;
			typedef std::vector<collision_item> collision_item_list;

			collision_item_list to_apply;
			for (auto& coll : final_collisions)
			{
				corner_ref source = plan.get(coll.source);
				corner_ref target = plan.get(coll.target);
				point3 p = plane.projection(coll.p);
				int vertex = source->vertex;

				//std::cout << "collision: " << source->id << " => " << target->id << " p: " << p << std::endl;

				bool same_as_prev = same_point(p, target->prev->point);
				bool same_as_corner = same_point(p, target->point);
				if (same_as_prev && same_as_corner)
				{
					create_collision_vertex(source, target, target->prev, p);
				}
				else if (same_as_prev)
				{
					create_collision_vertex(source, target->prev, p);
				}
				else if (same_as_corner)
				{
					create_collision_vertex(source, target, p);
				}
				else
				{
					//regular collision
					if (source->vertex < 0)
					{
						source->vertex = create_vertex(p);
						source->point = p;
					}

					corner_collision cc(source->point, source->vertex);
					to_apply.push_back(collision_item(target, cc));
				}
			}

			for (auto ci : to_apply)
			{
				auto  target = std::get<0>(ci);
				auto& cc = std::get<1>(ci);
				if (cc.vertex != target->vertex && cc.vertex != target->prev->vertex)
					add_collision(target, cc);
			}
		}

		bool is_zero(double d)
		{
			return CGAL::abs(d) < epsilon;
		}

		void add_collision(corner_ref c, corner_collision& coll)
		{
			auto prev_dist = CGAL::squared_distance(c->prev->point, coll.point);
			auto dist = CGAL::squared_distance(c->point, coll.point);

			if (!is_zero(prev_dist) && !is_zero(dist))
			{
				bool found = false;
				for (auto cc : c->collisions)
				{
					auto dd = CGAL::squared_distance(coll.point, cc.second.point);
					if (is_zero(dd - dist))
					{
						found = true;
						break;
					}
				}

				if (!found)
				{
					assert(coll.vertex >= 0);
					c->add_collision(dist, coll);
				}
			}
		}

		int vertex_from_corner(corner_ref c)
		{
			if (c->vertex < 0)
				c->vertex = create_vertex(c->point);

			return c->vertex;
		}

		void update_vertex(corner_ref c)
		{
			if (c->has(CF_COMPLEX))
			{
				c->set(CF_COMPLEX, false);
				c->vertex = create_vertex(c->point);
			}
		}

		void add_face(corner_ref c, corner_ref nc)
		{
			update_vertex(c);
			update_vertex(c->prev);

			int vsouth = vertex_from_corner(c);
			int veast = vertex_from_corner(nc);
			int vnorth = vertex_from_corner(nc->prev);
			int vwest = vertex_from_corner(c->prev);

			if (vsouth == vwest && vnorth == veast)
			{
				//we've seen this with bad skirts and will just remove them
				assert(c->is_skirt()); 
				nc->prev->next = nc->next;
				nc->next->prev = nc->prev;
				return;
			}


			//std::cout << "creating face: " << vsouth << " " << veast << " " << vnorth << " " << vwest << std::endl;

			face3 f = builder.begin_facet();
			builder.add_vertex_to_facet(vsouth);
			builder.add_vertex_to_facet(veast);
			
			if (vnorth != veast)
			{
				for (auto coll : nc->collisions) 
				{
					auto index = coll.second.vertex;
					builder.add_vertex_to_facet(index);
				}
				
				builder.add_vertex_to_facet(vnorth);
			}

			if (vwest != vsouth)
				builder.add_vertex_to_facet(vwest);
			
			builder.end_facet();

			//mark as side
			if ((horizontal && c->is_horizontal()) || !horizontal)
				builder.set_face_tag(f, SIDE_TAG);

			//assign labels
			builder.set_face_label(f, LabelId{c->labels.label});
			builder.set_halfedge_label_by_target(f, vsouth, LabelId{c->labels.bottom_label});
			builder.set_halfedge_label_by_target(f, veast, LabelId{c->labels.right_label});
			builder.set_halfedge_label_by_target(f, vwest, LabelId{c->labels.left_label});
			for (auto vertex : {vnorth})
				builder.set_halfedge_label_by_target(f, vertex, LabelId{c->labels.top_label});
			for (auto coll : nc->collisions)
				builder.set_halfedge_label_by_target(f, coll.second.vertex, LabelId{c->labels.top_label});
		}

		const int CAP_TAG = -872348234;
		const int SIDE_TAG = CAP_TAG + 2;
		void close_plan(corner_ref c)
		{
			std::map<int, int> labels;

			face3 f = builder.begin_facet();

			corner_ref curr = c;
			corner_ref cend = c;
			do
			{
				if (!curr->is_skirt()) //ignore skirts when closing the plan
				{
					if (curr->vertex < 0)
						curr->vertex = create_vertex(curr->point);

					auto vertex = curr->vertex; assert(vertex >= 0);
					builder.add_vertex_to_facet(vertex);
					labels[vertex] = curr->labels.cap_label;
				}

				curr = curr->next;
			} while (curr != cend);
			
			builder.end_facet();

			builder.set_face_label(f, LabelId{default_cap});
			builder.set_face_tag(f, CAP_TAG);
			for (const auto& label : labels)
				builder.set_halfedge_label_by_target(f, label.first, LabelId{label.second});
		}

		bool is_thin(corner_ref c)
		{
			auto cc = c->next->next->next;
			if (cc == c)
			{
				//triangle
				DoubleKernel::Triangle_3 t(

					c->point,
					c->next->point,
					c->prev->point);

				return t.squared_area() < 1e-8;
			}
			else if (cc->next == c)
			{
				 // quad
				DoubleKernel::Triangle_3 t1(
					c->point,
					c->next->point,
					c->next->next->point);

				auto a = t1.squared_area();
				if (a > 1e-8)
					return false;

				DoubleKernel::Triangle_3 t2(
					c->next->point,
					c->next->next->point,
					c->prev->point);

				return a + t2.squared_area() < 1e-8;
			}

			return false;
		}

		void tesselate(plan_container& old_plan, plan_container& new_plan)
		{
			for (auto& c : old_plan.plans)
			{
				if (!new_plan.get(c->id))
				{
					//the plan was stopped
					close_plan(c);
					continue;
				}
				
				if (is_thin(c))
				{
					//the plan was thin and therefore we can close it
					close_plan(c);
					continue;
				}

				auto pit = c;
				auto pnd = c;
				do
				{
					auto nc = new_plan.get(pit->id); assert(nc);
					if (same_point(nc->point, nc->next->point) && nc->vertex != nc->next->vertex)
					{
						assert(nc->vertex < 0 && nc->next->vertex >= 0);
						nc->vertex = nc->next->vertex;
						nc->point = nc->next->point;
					}

					if (same_point(nc->point, nc->prev->point) && nc->vertex != nc->prev->vertex)
					{
						assert(nc->vertex < 0 && nc->prev->vertex >= 0);
						nc->vertex = nc->prev->vertex;
						nc->point = nc->prev->point;
					}

					add_face(pit, nc);
					pit = pit->next;
				} while (pit != pnd);
			}
		}

		corner_ref create_corner(point3 p, plane3 pl, int flags, corner_labels& labels)
		{
			return corner_ref(new corner(current_corner++, p, pl, flags, labels));
		}

		exact_point2 builder_point(int vertex, point3 p, plane3& plane, std::map<int, exact_point2>& cache, exact_projector& projector)
		{
			assert(vertex >= 0);
			auto it = cache.find(vertex);
			if (it == cache.end())
			{
				CGAL::Cartesian_converter<DoubleKernel, ExactKernel> ek;

				auto result = projector.to_2d(ek(p));
				cache[vertex] = result;
				return result;
			}

			return it->second;
		}

		void duplicate_events(int corner, int new_corner)
		{
			std::vector<event_ref> new_events;
			for (auto ev : events)
			{
				if (ev.second->corner_id == corner)
				{
					auto new_ev = ev.second->clone();
					new_ev->corner_id = new_corner;
					new_events.push_back(new_ev);
				}
			}

			std::vector<event_ref> new_hevents;
			for (auto ev : hevents)
			{
				if (ev.second->corner_id == corner)
				{
					auto new_ev = ev.second->clone();
					new_ev->corner_id = new_corner;
					new_hevents.push_back(new_ev);
				}
			}

			for (auto e : new_events)
			{
				auto d = CGAL::squared_distance(base, e->point);
				events.insert(std::pair<double, event_ref>(d, e));
			}

			for (auto e : new_hevents)
			{
				auto d = CGAL::squared_distance(base, e->point);
				hevents.insert(std::pair<double, event_ref>(d, e));
			}
		}

		void add_builder_corner(plan_container& plan, extrude_plan_builder& builder, corner_ref c, plane3& plane, std::map<int, exact_point2>& cache, exact_projector& projector)
		{
			if (CGAL::squared_distance(c->point, c->prev->point) < epsilon)
			{
				assert(c->vertex == c->prev->vertex);
				return; //degenerate
			}

			auto cp = builder_point(c->prev->vertex, c->prev->point, plane, cache, projector);
			auto it = c->collisions.rbegin();
			auto nd = c->collisions.rend();
			while (it != nd)
			{
				auto p = it->second.point;
				auto nc = create_corner(p, c->plane, c->flags, c->labels);

				duplicate_events(c->id, nc->id);

				nc->vertex = it->second.vertex;
				plan.register_corner(nc);

				auto ccp = builder_point(it->second.vertex, p, plane, cache, projector);
				builder.add(cp, ccp, nc->id, c->id);

				cp = ccp;

				it++;
			}

			auto p = builder_point(c->vertex, c->point, plane, cache, projector);
			builder.add(cp, p, c->id, c->prev->id);
		}

		void build_next_plan(plan_container& plan, plane3& plane)
		{
			CGAL::Cartesian_converter<DoubleKernel, ExactKernel> ek;
			exact_projector projector(ek(plane), true);

			extrude_plan_builder builder;
			extrude_plan_builder::Plans results;
			std::map<int, exact_point2> cache;

			for (auto& c : plan.plans)
			{
				auto pit = c;
				auto pnd = c;
				do
				{
					add_builder_corner(plan, builder, pit, plane, cache, projector);
					pit = pit->next;
				} while (pit != pnd);
			}

			if (!builder.collisions().empty())
			{
				for (auto c : builder.collisions())
				{
					auto c1 = plan.get(c.first);
					auto c2 = plan.get(c.second);
					c1->point = c2->point;
				}
			}

			plan_container final;
			std::set<int> complexes;
			builder.get_plan(results, complexes);
			
			if (!complexes.empty())
			{
				//when complex collisions appear, sometimes corners can get suplicated in different plans
				//this code duplicates those corners
				auto max_id = complexes.empty() ? 0 : plan.max_id();
				std::set<int> used;

				for (auto& rp : results)
				{
					for (auto& rc : rp)
					{
						if (used.find(rc) == used.end())
						{
							used.insert(rc);
						}
						else
						{
							auto c = plan.get(rc);
							c = clone_corner(c);
							c->id = (int)max_id++;
							c->vertex = -1;
							plan.register_corner(c);
							rc = c->id;
						}
					}
				}
			}

			//build the final plan with "results"
			for (auto& rp : results)
			{
				corner_ref head;
				corner_ref last;
				for (auto rc : rp)
				{
					corner_ref curr = plan.get(rc); assert(curr);
					//print_corner(curr);
					if (complexes.find(rc) != complexes.end())
					{
						curr->set(CF_COMPLEX, true);
					}

					final.register_corner(curr);

					if (!head)
					{
						head = curr;
						head->prev = corner_ref();
					}
					else
					{
						last->next = curr;
						curr->prev = last;
					}

					last = curr;
				}

				if (head && last)
				{
					last->next = head;
					head->prev = last;

					final.plans.push_back(head);
				}
			}

			plan = final;
		}

		bool advance_profile(profile_ref profile, int index, point3 prev_point, point3 point, advance_profile_result& result)
		{
			if (profile->size() <= index)
				return false;

			if (CGAL::squared_distance(prev_point, point) < epsilon)
				return false;

			const auto direction = profile->direction(index);
			const auto& segment = profile->segment(index);
			result.labels.label = segment.face_label.value();
			result.labels.bottom_label = segment.bottom_label.value();
			result.labels.top_label = segment.top_label.value();
			result.labels.left_label = segment.left_label.value();
			result.labels.right_label = segment.right_label.value();
			result.labels.skirt_label = segment.skirt_label.value();
			default_labels(result.labels);

			double dx = direction.first;
			double dy = direction.second;

			bool is_horizontal = false;
			if (dx == 0)
				result.flags |= CF_VERTICAL;
			else if (CGAL::abs(dy) < 1e-4)
			{
				result.flags |= CF_HORIZONTAL;
				is_horizontal = true;
				dy = 1; // CGAL::abs(dx); //create 45 degrees profiles for horizontal segments

				if (profile->sign() == CGAL::NEGATIVE)
					dy *= -1;

				if (profile->size() == (index + 1))
				{
					//backwards compatibility, close the plan on horizontal segment when it ends the profile
					result.abort = true;
				}
			}

			if (horizontal && !is_horizontal)
			{
				prev_point = horizontal_plane.projection(prev_point);
				point = horizontal_plane.projection(point);
			}

			vec3 seg(prev_point, point);
			vec3 normal = base.orthogonal_vector();
			vec3 depth = CGAL::cross_product(seg, normal);

			normal = normal / CGAL::sqrt(CGAL::to_double(normal.squared_length()));
			depth = depth / CGAL::sqrt(CGAL::to_double(depth.squared_length()));


			result.p = point + depth*dx + normal*dy;
			result.plane = plane3(prev_point, point, result.p);
			return true;
		}

		void process_events(plan_container& plan, plane3& ev_plane, bool& abort)
		{
			abort = false;

			std::vector<event_ref> new_events;
			std::vector<event_ref> new_hevents;

			bool horizontal_found = false;
			auto& evs = horizontal ? hevents : events;
			while(!evs.empty())
			{
				const auto& e = evs.begin();
				auto ev = e->second;
				if (CGAL::squared_distance(ev_plane, ev->point) > 1e-8)
					break;

				auto c = plan.get(ev->corner_id);
				if (c)
				{
					switch (ev->id)
					{
					case EV_PROFILE:
						profile_event* pev = (profile_event*)ev.get();

						advance_profile_result apr(c->labels.label, c->labels.cap_label);
						if (advance_profile(pev->profile, pev->index, c->prev->point, c->point, apr))
						{
							c->plane = apr.plane;
							c->flags = apr.flags;
							c->labels = apr.labels;

							if (apr.flags & CF_HORIZONTAL)
							{
								horizontal_found = true;
								new_hevents.push_back(event_ref(new profile_event(pev->profile, apr.p, c->id, pev->index + 1)));
							}
							else
								new_events.push_back(event_ref(new profile_event(pev->profile, apr.p, c->id, pev->index + 1)));

							if (apr.abort)
								abort = true;
						}
						break;
					}
				}

				evs.erase(e);
			}

			if (horizontal_found)
				horizontal = true;

			for (auto e : new_events)
			{
				auto d = CGAL::squared_distance(base, e->point);
				events.insert(std::pair<double, event_ref>(d, e));
			}

			for (auto e : new_hevents)
			{
				auto d = CGAL::squared_distance(base, e->point);
				hevents.insert(std::pair<double, event_ref>(d, e));
			}
		}
	};
};

} // namespace phoenix::extrusion::detail

namespace phoenix::extrusion {
namespace {

std::optional<KernelDiagnostic> validate_input(const KernelExtrusionInput& input)
{
	if (input.boundary.size() < 3 || input.sign == CGAL::ZERO) {
		return KernelDiagnostic{KernelErrorCode::invalid_input,
			"Extrusion requires at least three boundary points and a nonzero profile sign."};
	}
	for (const auto& corner : input.boundary) {
		if (!std::isfinite(corner.point.x) || !std::isfinite(corner.point.y)
			|| !std::isfinite(corner.point.z)) {
			return KernelDiagnostic{KernelErrorCode::invalid_input,
				"Extrusion boundary coordinates must be finite."};
		}
		if (!corner.profile) {
			return KernelDiagnostic{KernelErrorCode::invalid_input,
				"Every extrusion corner requires an immutable profile."};
		}
		if (corner.profile->sign() != input.sign) {
			return KernelDiagnostic{KernelErrorCode::inconsistent_profile_sign,
				"All corner profiles must have the extrusion input sign."};
		}
	}
	return std::nullopt;
}

std::optional<detail::plane3> input_plane(const KernelExtrusionInput& input)
{
	if (input.boundary.size() < 3) return std::nullopt;
	for (std::size_t i = 1; i + 1 < input.boundary.size(); ++i) {
		const auto& a = input.boundary[0].point;
		const auto& b = input.boundary[i].point;
		const auto& c = input.boundary[i + 1].point;
		detail::plane3 plane({a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z});
		if (!plane.is_degenerate()) return plane;
	}
	return std::nullopt;
}

} // namespace

KernelResult run_kernel(const KernelExtrusionInput& source, RunElementIdAllocator& ids)
{
	KernelResult result;
	if (const auto invalid = validate_input(source)) {
		result.diagnostics.push_back(*invalid);
		return result;
	}
	const auto plane = input_plane(source);
	if (!plane) {
		result.diagnostics.push_back({KernelErrorCode::invalid_input,
			"Extrusion requires a nondegenerate planar boundary."});
		return result;
	}

	detail::preserved_extrude::input input(source.sign,
		source.bottom_label.value(), source.right_label.value(),
		source.top_label.value(), source.left_label.value(),
		source.skirt_label.value(), source.cap_label.value());
	input.plane = *plane;
	detail::preserved_extrude::input::input_polygon polygon;
	polygon.reserve(source.boundary.size());
	for (std::size_t i = 0; i < source.boundary.size(); ++i) {
		const auto& corner = source.boundary[i];
		const auto& incoming = source.boundary[
			i == 0 ? source.boundary.size() - 1 : i - 1];
		polygon.emplace_back(detail::point3{corner.point.x, corner.point.y, corner.point.z},
			corner.profile, corner.cap_label.value(),
			static_cast<detail::uniqueid>(corner.source_vertex_id.value()),
			static_cast<detail::uniqueid>(incoming.source_halfedge_id.value()),
			static_cast<detail::uniqueid>(incoming.source_edge_id.value()));
	}
	input.polygons.push_back(std::move(polygon));

	OutputAdapter output(ids);
	try {
		detail::preserved_extrude::run(input, output);
		auto built = output.build();
		if (!built.success) {
			result.diagnostics.push_back({KernelErrorCode::malformed_output,
				built.diagnostics.empty() ? "Extrusion produced malformed geometry."
				: built.diagnostics.front().message});
			return result;
		}
		result.working = std::move(built.working);
		result.success = true;
	} catch (const detail::kernel_failure& failure) {
		result.diagnostics.push_back({failure.code, failure.what()});
	} catch (const std::exception& failure) {
		result.diagnostics.push_back({KernelErrorCode::unhandled_edge_case, failure.what()});
	}
	return result;
}

std::string to_string(KernelErrorCode code)
{
	switch (code) {
	case KernelErrorCode::invalid_input: return "invalid_input";
	case KernelErrorCode::inconsistent_profile_sign: return "inconsistent_profile_sign";
	case KernelErrorCode::unhandled_edge_case: return "unhandled_edge_case";
	case KernelErrorCode::excessive_edge_length: return "excessive_edge_length";
	case KernelErrorCode::malformed_output: return "malformed_output";
	}
	return "unknown";
}

} // namespace phoenix::extrusion
