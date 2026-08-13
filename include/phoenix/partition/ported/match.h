#pragma once

#include "../geometry.h"
#include "phoenix/partition/ported/vm_compat.hpp"

struct match_face2
{
  DEFAULT_ARRANGEMENT_TYPES();

  virtual bool match(face2 f) = 0;
};

struct match_edge2
{
  DEFAULT_ARRANGEMENT_TYPES();

  virtual void build(vm::icontext_ref ctx) = 0;
  virtual bool match(edge2 he) = 0;
};

typedef std::shared_ptr<match_face2> match_face2_ref;
typedef std::shared_ptr<match_edge2> match_edge2_ref;

typedef std::vector<match_edge2_ref> edge2_predicate_list;

struct composite_match : match_edge2
{
	composite_match(edge2_predicate_list& predicates): _predicates(predicates)
	{
	}

	virtual void build(vm::icontext_ref ctx)
	{
		for (auto it : _predicates)
			it->build(ctx);
	}

	virtual bool match(edge2 he)
	{
		for (auto it : _predicates)
		{
			if (!it->match(he))
				return false;
		}
		return true;
	}

private:
	edge2_predicate_list _predicates;
};
