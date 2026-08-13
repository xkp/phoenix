#pragma once

#include <CGAL/exceptions.h>
#include <CGAL/Range_segment_tree_traits.h>
#include <CGAL/Range_tree_k.h>

#include "phoenix/partition/geometry.h"
#include "phoenix/partition/ported/error.h"
#include "phoenix/partition/ported/geometry_utils.h"

#include <functional>

using vertex_id_generator = std::function<long()>;
using edge_id_generator = std::function<long()>;

template <typename K, typename A, typename P>
struct merge_borders3
{
	DECLARE_TYPES(K)
	DECLARE_MESH_TYPES(P)
	DECLARE_UTILS(K, A, P)

	typedef P polyhedron3;

	struct csegment3;
	typedef std::shared_ptr<csegment3> csegment3_ref;

	typedef std::vector<csegment3_ref> csegment3_list;

	static const int TAG_EDGE_BORDER = 1; // this edge is a border

	struct cface3
	{
		csegment3_list segments;
		face3_data data;

		cface3(face3_data& data_) :
			segments(),
			data(data_)
		{};

		void insert_after(csegment3_ref seg, csegment3_ref new_seg)
		{
			auto f = std::find(segments.begin(), segments.end(), seg);
			if (f != segments.end())
				segments.insert(++f, new_seg);
			else
				std::cerr << "bug: add_faces_to_mesh\\insert_after\n";
		}

		bool collapsable() const
		{
			int valid = 0;
			for (auto& seg : segments)
			{
				if (!seg->collapsable())
					valid++;
			}

			return valid < 3;
		}

		void print()
		{
			for (auto& seg : segments)
			{
				std::cout << seg->target_pt() << ", vid:" << seg->target->data.id << " | ";				
			}

			std::cout << "\n";
		}

	};

	typedef std::shared_ptr<cface3> cface3_ref;

	struct csegment3
	{
		csegment3(edge3 he_, cface3_ref face_):
			he(he_),
			_face(face_),
			source(he_->opposite()->vertex()),
			target(he_->vertex()),
			seg(),
			done(false),
			modified(false)
		{
			seg = segment3(source->point(), target->point());
		};

		csegment3(edge3 he_, cface3_ref face_, vertex3 vs, vertex3 vt) :
			he(he_),
			_face(face_),
			source(vs),
			target(vt),
			seg(vs->point(), vt->point()),
			done(false),
			modified(true)
		{ };

		point3 source_pt()
		{
			return source->point();
		}

		point3 target_pt()
		{
			return target->point();
		}

		bool collapsable() const
		{
			return source->point() == target->point();
		}

		double squared_length()
		{
			return CGAL::to_double(CGAL::squared_distance(source->point(), target->point()));
		}

		bool contain(const line3& edge_line, vertex3 v)
		{
			//bool res = source->point() != v->point() && target->point() != v->point() && seg.has_on(v->point());
			//return res;
			if (source->point() == v->point() || target->point() == v->point())
				return false;

			auto scalar = seg.to_vector() * edge_line.to_vector();
			auto aligned_line = scalar >= 0 ? edge_line : edge_line.opposite();
			auto pl = aligned_line.perpendicular_plane(source->point());
			//auto pl = geometry::plane3(source->point(), seg.to_vector());
			if (!pl.has_on_positive_side(v->point()))
				return false;

			pl = aligned_line.opposite().perpendicular_plane(target->point());
			//pl = geometry::plane3(target->point(), seg.opposite().to_vector());
			if (!pl.has_on_positive_side(v->point()))
				return false;

			//std::cout << source->point() << " * " << target->point() << " * " << v->point() << "\n";

			return true;
			/*auto sqd = CGAL::to_double(CGAL::squared_distance(seg, v->point()));
			//std::cout << "distance = " << sqd << "\n";
			if (sqd < 0.00001)
			{
				std::cout << "intersection!!\n";
				return true;
			}

			return false;*/
		}

		csegment3_ref split(vertex3 v)
		{
			csegment3_ref new_seg = csegment3_ref(new csegment3(he, _face, v, target));
			//_face->segments.push_back(new_seg);
			target = v;
			seg = segment3(source->point(), target->point());
			modified = true;
			return new_seg;
		}

		csegment3_ref intersect(const line3& edge_line, csegment3_ref seg)
		{
			if (contain(edge_line, seg->source))
			{
				//std::cout << "split source\n";
				//std::cout << "sqds = " << CGAL::squared_distance(seg->source->point(), source->point()) << "\n";
				//std::cout << "sqdt = " << CGAL::squared_distance(seg->source->point(), target->point()) << "\n";
				return split(seg->source);
			}
			else if (contain(edge_line, seg->target))
			{
				//std::cout << "split target\n";
				return split(seg->target);
			}
			else
				return csegment3_ref();
		}

		edge3_data& data()
		{
			return he->data;
		}

		edge3_data& twin_data()
		{
			return he->opposite()->data;
		}

		cface3_ref cface()
		{
			return _face;
		}

		face3 face()
		{
			return he->face();
		}

		void print(std::string name)
		{
			geom_utils::print(seg, name);
			std::cout << "source id: " << source->data.id << ", target id: " << target->data.id << "\n";
		}

		vertex3 source, target;
		segment3 seg;
		bool done;
		bool modified;
	private:
		edge3 he; // use only for accesing data and twin data, other info may be incorrect
		cface3_ref _face;
	};

	// traverse all faces connected to f and assign a group_id in the tag
	static void traverse_faces(geometry::face3 f, int group_id, int& nfaces)
	{
		f->data.tag = group_id;

		nfaces++;
		auto eit = f->facet_begin();
		auto end = eit;
		CGAL_For_all(eit, end)
		{
			auto opp_face = eit->opposite()->face();
			if (opp_face != face3() && opp_face->data.tag == -1)
				traverse_faces(opp_face, group_id, nfaces);
		}
	}

	typedef typename P::HalfedgeDS HDS;
	typedef CGAL::Polyhedron_incremental_builder_3<HDS> incremental_builder;
	class mesh_builder : public CGAL::Modifier_base<HDS>
	{
	public:
		vertex_id_generator _vid_gen;
		error_function _on_error;
		std::vector<cface3_ref>& _cfaces;

		mesh_builder(vertex_id_generator vid_gen, error_function on_error, std::vector<cface3_ref>& cfaces) :
			_vid_gen(vid_gen),
			_on_error(on_error),
			_cfaces(cfaces)
		{
		}

		void operator()(HDS& hds)
		{
			incremental_builder builder(hds, true);
			builder.begin_surface(_cfaces.size()*2, _cfaces.size());

			std::map<int, size_t> vertices_cache;
			int kk3 = 0;
			for (auto face : _cfaces)
			{
				if (face->collapsable())
					continue;

				//std::cout << "new face\n";
				//face->print();
				// check validity of new face
				//std::map<vertex3, edge3_data> edges_data; // the data of each edge, using the vertex as the key
				std::map<vertex3, csegment3_ref> edges_seg; // the data of each edge, using the vertex as the key
				// add vertexs and build indices
				std::vector<int> indices;
				indices.reserve(face->segments.size());
				for (csegment3_ref seg : face->segments)
				{
					auto vindex = cache_vertex(_vid_gen, builder, seg->target, vertices_cache);
					if (!indices.empty() && indices.back() == vindex)
						continue;
					indices.push_back(vindex);
					//edges_data[builder.vertex(vindex)] = seg->data();
					edges_seg[builder.vertex(vindex)] = seg;
				}

				if (!indices.empty() && indices.back() == indices.front())
					indices.pop_back();

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
					// duplicate the vertexs
					for (int i = 0; i<indices.size(); i++)
					{
						int& idx = indices[i];
						geometry::vertex3 v = builder.vertex(idx);
						//int& next_idx = indices[(i == indices.size() - 1) ? 0 : i + 1];
						//vertex3 v_next = builder.vertex(next_idx);
						//geometry::edge3 he = find_edge(v, v_next);
						//if (he != edge3() && !he->is_border_edge()) // edge has 2 faces already connected, duplicate its vertexs
						{
							kk3++;
							int new_index = cache_vertex(_vid_gen, builder, v, vertices_cache, true);
							edges_seg[builder.vertex(new_index)] = edges_seg[v];
							idx = new_index;

							/*int new_next_index = cache_vertex(_ctx, builder, v_next, vertices_cache, true);
							edges_seg[builder.vertex(new_next_index)] = edges_seg[v_next];
							next_idx = new_next_index;*/
						}
					}

					valid_face = true;
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
					    solver_error se(-1, "invalid face (merge3d)");
						_on_error(se);
						std::cout << "invalid face (merge3d)" << std::endl;
						//file_utils::saveOBJ(_faces, "c:\\dev\\add_faces_to_mesh.obj");
						continue;
					}

					/*for (auto vindex : indices)
					{
						std::cout << builder.vertex(vindex)->point() << " | ";
					}

					std::cout << "\n";*/

					//file_utils::saveOBJ(, "c:\\dev\\merge3d_invalidface.svg");
					//continue;
				}

				edge3 he = builder.add_facet(indices.begin(), indices.end());
				if (he == edge3())
					continue;

				face3 added_face = he->face();
				added_face->data = face->data;

				// set the labels and edge_id to the face halfedges
				typename polyhedron3::Halfedge_around_facet_circulator eitl = added_face->facet_begin();
				typename polyhedron3::Halfedge_around_facet_circulator endl = eitl;
				do
				{
					edge3 he = eitl;
					//auto& data = edges_data[he->vertex()];
					auto seg = edges_seg[he->vertex()];
					he->data.id = seg->data().id;

					if (he->data.label == -1)
						he->data.label = seg->data().label;

					if (seg->twin_data().label != -1 && seg->twin_data().tag == TAG_EDGE_BORDER)
						he->opposite()->data.label = seg->twin_data().label;
				} while (++eitl != endl);

			}

			//if (kk3 > 0)
			//	std::cout << "kk3: " << kk3 << std::endl;
			builder.end_surface();
		}
	};

	static edge3 find_edge(const vertex3& source, const vertex3& target)
	{
		if (target->halfedge() != edge3())
		{
			auto start = target->vertex_begin();
			auto it = start;
			do
			{
				if (it->opposite()->vertex() == source)
					return it;
			} while (++it != start);
		}

		return edge3();
	}

	static int join_vertexs(vertex_id_generator vid_gen, face3_list& faces, double tolerance = 1e-5)
	{
		typedef CGAL::Simple_cartesian<double> JV_Kernel;
		typedef CGAL::Range_tree_map_traits_3<JV_Kernel, vertex3> Traits;
		//typedef CGAL::Range_tree_map_traits_3<K, vertex3> Traits;
		typedef typename CGAL::Range_tree_3<Traits> Range_tree_3_type;
		typedef typename Traits::Key Key;
		typedef typename Traits::Interval Interval;

		CGAL::Cartesian_converter<K, JV_Kernel> converter;

		//DebugTimer::join_vertex.start();
		geom_utils::tag_vertexs(faces, -1);

		std::vector<Key> vertex_list;

		const int TAG_ORIGINAL = 0;
		const int TAG_USED = 1;

		for (auto f : faces)
		{
			auto eit = f->facet_begin();
			auto end = eit;
			CGAL_For_all(eit, end)
			{
				auto v = eit->vertex();
				if (v->data.tag != TAG_ORIGINAL)
				{
					v->data.tag = TAG_ORIGINAL;
					if (v->data.id < 0)
						v->data.id = vid_gen();

					vertex_list.push_back(Key(converter(v->point()), v));
				}
			}
		}

		Range_tree_3_type range_tree_3(vertex_list.begin(), vertex_list.end());

		CGAL::Vector_3<JV_Kernel> t3(tolerance, tolerance, tolerance);
		int joined_vertexs = 0;

		for (auto& vit : vertex_list)
		{
			auto v = vit.second;
			if (v->data.tag == TAG_USED)
				continue;

			v->data.tag = TAG_USED;
			auto min = vit.first - t3;
			auto max = vit.first + t3;
			//point3 min(v->point() - t3), max(v->point() + t3);

			//Interval win(Interval(Key(converter(min), vertex3()), Key(converter(max), vertex3())));
			Interval win(Interval(Key(min, vertex3()), Key(max, vertex3())));

			std::vector<Key> output_list;
			range_tree_3.window_query(win, std::back_inserter(output_list));
			if (output_list.size() > 1)
			{
				//std::cout << "close to: " << v->point() << ", size = " << output_list.size() << "\n";
				for (auto& current : output_list)
				{
					//if (current.second == v)
					//	std::cout << "111 ****************************************************\n";
					if (current.second->data.tag != TAG_USED)
					{
						joined_vertexs++;
						current.second->data.tag = TAG_USED;
						current.second->point() = v->point();
						current.second->data.id = v->data.id;
					}
				}
			}
		}

		/*std::cout << "vertexs after join:\n";
		for (auto& vit : vertex_list)
		{
			auto v = vit.second;
			std::cout << v->point() << " : " << v->data.id << "\n";
		}
		std::cout << "\n\n";*/

		//if (joined_vertexs > 0)
		//	std::cout << "joined_vertexs: " << joined_vertexs << std::endl;
		//DebugTimer::join_vertex.pause();
		return joined_vertexs;
	}

	static void run(polyhedron3& output_mesh, face3_list& input_faces, bool join_vertexs_, error_function on_error, vertex_id_generator vid_gen, edge_id_generator eid_gen)
	{
		//std::cout << "facelist = " << input_faces.size() << "\n";
		if (input_faces.empty())
			return;
		//std::cout << "merge start\n";

		//file_utils::saveOBJ(input_faces, "c:\\dev\\merge_input.obj");
		//if (output_mesh.size_of_facets() > 0)
		//	std::cout << "input mesh not empty on merge3d\n";
			//std::cout << "output_mesh.size = " << output_mesh.size_of_facets() << "\n";

		std::vector<cface3_ref> cfaces;
		typedef std::pair<int, csegment3_ref> csegment3_pair;
		std::multimap<int, csegment3_ref> segments_by_id;

		// initialization
		geom_utils::tag(output_mesh, 0);
		geom_utils::tag_edges(input_faces, -1);
		geom_utils::tag(input_faces, -1);

		if (join_vertexs_)
			join_vertexs(vid_gen, input_faces);

		// create groups, list of faces and map of segments by edge_id
		cfaces.reserve(input_faces.size());
		int group_id = 0;
		std::set<face3> faces_added;

		for (auto f : input_faces)
		{
			/*if (faces_added.find(f) != faces_added.end())
			{
				std::cout << "ignore multiple faces\n";
				continue; // ignore multiple faces
			}

			faces_added.insert(f);*/

			// set the group id of faces, a group is a set of adjacent faces
			if (f->data.tag == -1)
			{
				int nfaces = 0;
				traverse_faces(f, group_id++, nfaces);
				//std::cout << "group faces = " << nfaces << "\n";
			}

			// create list of faces
			auto eit = f->facet_begin();
			auto end = eit;
			cface3_ref face = cface3_ref(new cface3(f->data));
			face->segments.reserve(f->facet_degree());
			//std::cout << "input face: " << f->facet_degree() << " segments\n";
			CGAL_For_all(eit, end)
			{
				if (eit->data.id < 0)
				{
					eit->data.id = eid_gen();
					//std::cout << "invalid segment id\n";
				}

				if (eit->opposite()->face() == face3()) // mark true borders
					eit->opposite()->data.tag = TAG_EDGE_BORDER;

				if (eit->opposite()->vertex()->point() == eit->vertex()->point())
					continue;

				auto seg = csegment3_ref(new csegment3(eit, face));

				face->segments.push_back(seg);

				// create map of segments by edge_id
				segments_by_id.insert(csegment3_pair(eit->data.id, seg));
			}

			if (!face->collapsable())
				cfaces.push_back(face);
		}

		// print segments by id
		/*int edge_id = -2;
		for (auto it1 = segments_by_id.begin(); it1 != segments_by_id.end(); it1++)
		{
			if (it1->first != edge_id)
			{
				edge_id = it1->first;
				size_t count = segments_by_id.count(edge_id);
				std::cout << "edge-id: " << edge_id << ", count: " << count << "\n";
			}

			it1->second->print("");
		}

		std::cout << "faces: " << cfaces.size() << "\n";*/

		static int kkk = 0;

		auto forEachCollisionableSegments = [&segments_by_id](std::function<void(csegment3_ref, csegment3_ref)> callback)
		{
			int current_id = segments_by_id.begin()->first;
			for (auto it1 = segments_by_id.find(current_id); it1 != segments_by_id.end(); it1++)
			{
				current_id = it1->first;
				auto seg1 = it1->second;
				auto it2 = it1;
				for (it2++; it2 != segments_by_id.end() && it2->first == current_id; it2++)
				{
					auto seg2 = it2->second;
					if (seg1->face()->data.tag == seg2->face()->data.tag)
						continue;	// dont check segments of the same group

					callback(seg1, seg2);
				}
			}

		};

		/*auto joinVertexs = [](vertex3 v1, vertex3 v2)
		{
			if (v1->data.id == v2->data.id || v1->point() == v2->point())
				return;

			auto sqd = CGAL::to_double(CGAL::squared_distance(v1->point(), v2->point()));
			if (sqd < 1e-9)
				v1->point() = v2->point();
		};

		forEachCollisionableSegments([&](csegment3_ref seg1, csegment3_ref seg2)
		{
			joinVertexs(seg1->source, seg2->source);
			joinVertexs(seg1->target, seg2->target);
			joinVertexs(seg1->source, seg2->target);
			joinVertexs(seg1->target, seg2->source);
		});*/

		// find intersections
		//std::cout << "groups = " << group_id << "\n";
		int intersection_count = 0, checks = 0;
		if (group_id > 1) // there is more than one group, find the intersections
		{
			// check for the intersections of all segments of the same id between each others
			int current_id = segments_by_id.begin()->first;
			for (auto it1 = segments_by_id.find(current_id); it1 != segments_by_id.end(); it1++)
			{
				current_id = it1->first;
				//std::cout << "current_id = " << current_id << "\n";
				auto seg1 = it1->second;
				auto it2 = it1;

				// find the line for the tests
				line3 edge_line(it2->second->source_pt(), it2->second->target_pt());
				for (; it2 != segments_by_id.end() && it2->first == current_id; it2++)
				{
					auto seg = it2->second;
					auto sqlen = seg->squared_length();
					if (sqlen > 0.0001)
					{
						edge_line = line3(seg->source_pt(), seg->target_pt());
						break;
					}
				}
				it2 = it1;

				for (it2++; it2 != segments_by_id.end() && it2->first == current_id; it2++)
				{
					auto seg2 = it2->second;
					if (seg1->face()->data.tag == seg2->face()->data.tag)
						continue;	// dont check segments of the same group

					checks++;
					kkk++;
					//std::cout << "checking intersection #" << checks << "\r";
					//std::cout << "------------------------------\n";
					//seg1->print("seg1");
					//seg2->print("seg2");
					//std::cout << "------------------------------\n";
					csegment3_ref inter = seg1->intersect(edge_line, seg2);
					if (inter)
					{
						//std::cout << "-- 1 divided by 2\n";
						//seg1->print("seg1 divided");
						seg1->cface()->insert_after(seg1, inter);

						segments_by_id.insert(csegment3_pair(current_id, inter));
						intersection_count++;
						it2--; // if a split was made check seg1, seg2 again
						continue;
					}

					inter = seg2->intersect(edge_line, seg1);
					if (inter)
					{
						//std::cout << "-- 2 divided by 1\n";
						//seg2->print("seg2 divided");
						seg2->cface()->insert_after(seg2, inter);

						segments_by_id.insert(csegment3_pair(current_id, inter));
						intersection_count++;
						it2--;
					}
				}
			}
		}

		//if (intersection_count > 0)
		//	std::cout << group_id << " groups, " << checks << " checks, " << intersection_count << " intersections found\n";

		// update the mesh with the new faces and edges
		/*std::cout << "groups = " << group_id << ", total faces = " << cfaces.size() << "\n";
		for (auto face : cfaces)
			std::cout << "face: " << face->segments.size() << " segments\n";*/

		mesh_builder b(vid_gen, on_error, cfaces);
		output_mesh.delegate(b);

		//file_utils::save(output_mesh, "c:\\dev\\merge_output.off");
	}

private:
	static int cache_vertex(vertex_id_generator vid_gen, incremental_builder& B, vertex3 v, std::map<int, size_t>& vertexs_cache, bool force_add = false)
	{
		int id;
		int index = -1;
		if (force_add)
			id = vid_gen();
		else
		{
			id = v->data.id;
			if (id < 0)
				id = v->data.id = vid_gen();
			else
			{
				auto cacheit = vertexs_cache.find(id);
				if (cacheit != vertexs_cache.end())
					index = (int)cacheit->second;
			}
		}

		if (index == -1)
		{
			index = (int)vertexs_cache.size();
			vertex3 vv = B.add_vertex(v->point());

			vv->data.index = index;
			vv->data.id = id;

			vertexs_cache[id] = index;
		}

		return index;

		/*auto id = v->data.id;
		if (id < 0)
			id = v->data.id = ctx->fctx()->vertex_id();
		assert(id > 0); //vertices should always have a unique id

		size_t index;
		auto vit = vertexs_cache.find(id);
		if (vit == vertexs_cache.end())
		{
			index = vertexs_cache.size();
			auto vv = B.add_vertex(v->point());
			vv->data.index = (int)index;
			vv->data.id = id;

			vertexs_cache[id] = index;
		}
		else
		{
			index = vit->second;
		}

		return index;*/
	}
};
