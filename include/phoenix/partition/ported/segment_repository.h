#pragma once

#include <algorithm>
#include <random>
#include "match.h"
#include "phoenix/partition/ported/null_diagnostics.hpp"

struct repo_segment_id
{
	repo_segment_id() :
		value(-1)
	{}

	explicit repo_segment_id(int segment_id) :
		value(segment_id)
	{}

	bool empty() const
	{
		return value < 0;
	}

	bool valid() const
	{
		return value >= 0;
	}

	void clear()
	{
		value = -1;
	}

	bool operator== (const repo_segment_id& rhs) const
	{
		return value == rhs.value;
	}

	bool operator < (const repo_segment_id& rhs) const {
		return value < rhs.value;
	}

	bool operator > (const repo_segment_id& rhs) const {
		return value > rhs.value;
	}

	/*operator int() const
	{
		return value;
	}*/

	int value;
};

struct repo_edge2
{
	DEFAULT_CGAL_TYPES()
	DEFAULT_ARRANGEMENT_TYPES()
	DEFAULT_UTILS();

	segment2 seg;
	edge2 he_start, he_end;

	repo_edge2() :
		seg(),
		he_start(),
		he_end()
	{}

	repo_edge2(const repo_edge2& other) :
		seg(other.seg),
		he_start(other.he_start),
		he_end(other.he_end)
	{}

	repo_edge2(segment2 seg_, edge2 he_start_, edge2 he_end_) :
		seg(seg_),
		he_start(he_start_),
		he_end(he_end_)
	{}

	explicit repo_edge2(edge2 he_) :
		seg(he_->curve()),
		he_start(he_),
		he_end(he_)
	{}

	bool operator== (const repo_edge2& other) const
	{
		return he_start == other.he_start && he_end == other.he_end;
	}

	bool has_edge() const
	{
		return he_start != edge2();
	}

	void set_label(int label)
	{
		for (auto he = he_start; he != he_end; he = he->next())
		{
			he->data().label = label;
		}
	}

	void set_opp_label(int label)
	{
		for (auto he = he_start; he != he_end; he = he->next())
		{
			he->twin()->data().label = label;
		}
	}

};

typedef std::vector<repo_edge2> repo_edge2_list;

struct segment_repository
{
	DEFAULT_CGAL_TYPES()
	DEFAULT_ARRANGEMENT_TYPES()
	DEFAULT_UTILS();

	segment_repository()
		:_is_face_concave(false)
	{}

	int size() const
	{
		return (int)_items.size();
	}

	void clear()
	{
		_is_face_concave = false;
		_items.clear();
		_edges.clear();
		_edges.shrink_to_fit();
		_known.clear();
		_unmatched.clear();
		_unmatched.shrink_to_fit();
	}

	segment2 get_colinear_range(const edge2& src, edge2& start, edge2& end) const
	{
		int label = src->data().label;
		auto line = line2(src->source()->point(), src->target()->point());

		edge2 edge = src->prev();
		while (edge->data().label == label && line.has_on(edge->source()->point()))
		{
			edge = edge->prev();
		}
		start = edge->next();
		auto source = edge->target()->point();

		edge = src->next();
		while (edge->data().label == label && line.has_on(edge->target()->point()))
		{
			edge = edge->next();
		}
		end = edge;
		auto target = edge->source()->point();

		return segment2(source, target);
	}

	void search(face2 f, const std::map<repo_segment_id, match_edge2*>& conds)
	{
		_is_face_concave = geom_utils::is_concave(f);

		//assert(_edges.empty() && _known.empty());
		for (auto it : conds)
			_known[it.first] = true;

		edge2_circulator eit = f->outer_ccb();
		edge2_circulator end = eit;
		do
		{
			bool found = false;
			edge2 he = eit;
			_edges.push_back(repo_edge2(he));
			for (auto it = conds.begin(); it != conds.end(); it++)
			{
				if (it->second->match(he))
				{
					found = true;
					add(it->first, repo_edge2(he));
				}
			}
			if (!found)
				_unmatched.push_back(repo_edge2(he));
		} while (++eit != end);
	}

	void search(face2 f, const std::map<repo_segment_id, edge2_predicate_list>& conds)
	{
		_is_face_concave = geom_utils::is_concave(f);

		assert(_edges.empty() && _known.empty());
		for (auto it : conds)
			_known[it.first] = true;

		edge2_circulator eit = f->outer_ccb();
		edge2_circulator end = eit;

		std::vector<repo_edge2> segments;
		edge2 he_start, he_end, he_init;

		auto seg = get_colinear_range(f->outer_ccb(), he_init, he_end);
		segments.push_back(repo_edge2(seg, he_init, he_end));

		while (he_end != he_init)
		{
			seg = get_colinear_range(he_end, he_start, he_end);
			segments.push_back(repo_edge2(seg, he_start, he_end));
		}

		for (auto seg : segments)
		{
			_edges.push_back(seg);
			bool found = false;
			for (auto it : conds)
			{
				bool is_match = false;
				for (auto m : it.second)
				{
					is_match = m->match(seg.he_start);
					if (!is_match)
						break;
				}

				if (is_match)
				{
					add(it.first, seg);
					found = true;
				}
			}

			if (!found)
				_unmatched.push_back(seg);
		}
	}

	void search(arrangement2& arr, const std::map<repo_segment_id, match_edge2*>& conds)
	{
		_is_face_concave = false;
		//assert(_edges.empty() && _known.empty());
		for (auto it : conds)
			_known[it.first] = true;

		for (arrangement2::Halfedge_iterator eit = arr.halfedges_begin(); eit != arr.halfedges_end(); eit++)
		{
			edge2 he = eit;
			_edges.push_back(repo_edge2(he));
			if (!is_unbounded_face(he->face()))
			{
				bool found = false;
				for (auto it = conds.begin(); it != conds.end(); it++)
				{
					if (it->second->match(he))
					{
						found = true;
						add(it->first, repo_edge2(he));
					}
				}

				if (!found)
					_unmatched.push_back(repo_edge2(he));
			}
		}
	}

	bool has(repo_segment_id id) const
	{
		auto it = _items.find(id);
		if (_items.find(id) == _items.end())
			return false;

		return !it->second.empty();
	}

	bool has_conditions(repo_segment_id id) const
	{
		//auto it = _known.find(id);
		return _items.find(id) == _items.end();
	}

	repo_edge2_list* at(int index)
	{
		int i = 0;
		for (auto it = _items.begin(); it != _items.end(); it++, i++)
		{
			if (i == index)
				return &it->second;
		}

		return nullptr;
	}

	repo_edge2 at(int index, int item) const
	{
		int i = 0;
		for (auto it = _items.begin(); it != _items.end(); it++, i++)
		{
			if (i == index)
				return it->second[item];
		}

		return repo_edge2();
	}

	const repo_edge2_list* get(repo_segment_id id) const
	{
		auto it = _items.find(id);
		if (it != _items.end())
			return &it->second;

		auto kit = _known.find(id);
		if (kit == _known.end())
			return &_edges;
			//return &_unmatched;

		return nullptr;
	}

	repo_edge2 get(repo_segment_id id, int index) const
	{
		auto it = _items.find(id);
		if (it == _items.end())
			return repo_edge2();

		return it->second[index];
	}

	void add(repo_segment_id id, repo_edge2 he)
	{
		std::map<repo_segment_id, repo_edge2_list>::iterator it = _items.find(id);
		if (it == _items.end())
			_items[id] = repo_edge2_list();

		_items[id].push_back(he);
	}

	/*bool remove(repo_segment_id id, edge2 he)
	{
		repo_edge2_list* l = get(id);
		if (l)
		{
			for (edge2_list::iterator it = l->begin(); it != l->end(); it++)
			{
				if (*it == he)
				{
					l->erase(it);
					return true;
				}
			}
		}

		return false;
	}*/

	void randomize(randomizer_ref rand)
	{
		int seed = (int)(rand->random() * 5489U);
		auto engine = std::default_random_engine(seed);
		for (auto it = _items.begin(); it != _items.end(); it++)
		{
			std::shuffle(it->second.begin(), it->second.end(), engine);
		}

		//std::shuffle(_unmatched.begin(), _unmatched.end(), engine);
		std::shuffle(_edges.begin(), _edges.end(), engine);
	}

	/*repo_edge2_list* unmatched()
	{
		return &_unmatched;
	}*/

public:
	struct iterator
	{
		iterator(std::map<repo_segment_id, repo_edge2_list>& candidates, std::vector<repo_segment_id>& indices) :
			_indices(indices),
			_candidates(candidates)
		{
		}

		bool next(repo_edge2_list& config)
		{
			if (_values.empty())
			{
				_values.resize(_indices.size());
				for (auto it = _values.begin(); it != _values.end(); it++)
					*it = 0;
			}

			config.resize(_indices.size());

			int i = 0;
			bool result = false;
			for (auto it = _indices.begin(); it != _indices.end(); it++, i++)
			{
				repo_edge2_list& list = _candidates[*it];
				int&        curr = _values[i];

				result = result || (list.size() - 1 > curr);
				if (!result)
					curr = 0;
				else
					curr++;

				config[i] = list[curr];
			}

			return result;
		}

	private:
		std::vector<repo_segment_id>           _indices;
		std::vector<int>           _values;
		std::map<repo_segment_id, repo_edge2_list>& _candidates;
	};

	iterator all_combinations()
	{
		std::vector<repo_segment_id> indices;
		for (std::map<repo_segment_id, repo_edge2_list>::iterator it = _items.begin(); it != _items.end(); it++)
			indices.push_back(it->first);

		return iterator(_items, indices);
	}

	iterator all_pairs(repo_segment_id id1, repo_segment_id id2)
	{
		std::vector<repo_segment_id> indices;
		indices.push_back(id1);
		indices.push_back(id2);
		return iterator(_items, indices);
	}
	
	repo_edge2_list* all_edges()
	{
		return &_edges;
	}

	const repo_edge2_list* all_edges() const
	{
		return &_edges;
	}

	bool is_face_concave() const
	{
		return _is_face_concave;
	}

	//void to_json(dj& dj_, const std::map<int, std::string>& segment_name_map) const
	void to_json(dj& dj_, const std::map<repo_segment_id, property_tree>& segment_name_map) const
	{
		dj_.add_type("segment_repository");

		auto dj_matches = dj_.begin("matched");
		for (auto it = _items.begin(); it != _items.end(); it++)
		{
			auto name_it = segment_name_map.find(it->first);
			if (name_it != segment_name_map.cend())
			{
				std::string seg_name = name_it->second.get<std::string>("name");
				dj_matches.add(seg_name, it->second);
			}
			else
				dj_matches.add(dj_matches.toString(it->first.value), it->second);
		}

		dj_.add("unmatched", _unmatched);
		dj_.add("edges", _edges);
	}

private:
	bool _is_face_concave;
	std::vector<repo_edge2> _edges;			// all edges, td: optimize
	std::map<repo_segment_id, repo_edge2_list> _items; // matched segments ids and their edges
	std::map<repo_segment_id, bool> _known; // ids of segments matched, necessary?
	std::vector<repo_edge2> _unmatched;		// edges not matched
};

typedef std::shared_ptr<segment_repository> segment_repository_ref;
typedef std::shared_ptr<const segment_repository> const_segment_repository_ref;
