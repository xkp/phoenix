#pragma once

#include <vector>
#include <map>
#include <boost/function.hpp>
#include "solver_errors.h"
#include "phoenix/partition/ported/null_diagnostics.hpp"

enum SolverPlanResult
{
	FAIL,
	SUCCEED,
	CONTINUE,
	BRANCH
};

template <typename Model, typename View, typename Plan>
struct solver
{
	typedef typename std::vector<View>  view_list;

	struct context
	{
		double min_error;
		double max_error;
		int max_iterations;
		// for reporting purposes
		int error_count;
		int instruction_count;
	};

	static bool run(const Model& model, const Plan& plan, View& root, context* ctx = nullptr)
	{
		typedef typename std::multimap<double, View> candidate_list;
		typedef typename std::pair<double, View>     candidate_pair;

		candidate_list candidates;
		candidates.insert(candidate_pair(model.eval(root), root));

		context default_context;
		default_context.max_error = -1;//td: do it like this so we don;t provide any other evaluation
		default_context.min_error = 1000000;//td: yes.
		default_context.max_iterations = 100000;

		ctx = init_context(ctx, default_context);
		ctx->instruction_count = 0;
		ctx->error_count = 0;

		auto dj_instructions = dj(false)// model.get_dj()
			.add("model", model, root)
			.begin_array("<<instructions");

		int lastCandidatesCount = 0;
		int max_iterations = ctx->max_iterations;
		int instruction_index = -1;

		while (max_iterations >= 0)
		{
			max_iterations--;
			if (candidates.empty())
			{
				dj_instructions.error("NO MORE CANDIDATES");
				//std::cout << "no more candidates\n";
				break;
			}

			typename candidate_list::iterator top = candidates.begin();
			double error = top->first;
			View&  view = top->second;

			if (error > ctx->min_error)
				continue;

			if (error <= ctx->max_error)
			{
				root.copy(view);
				return true;
			}

			while (true)
			{
				//model.print(view);
				// here the instruction begins
				clock_t start = clock();
				auto dj_ = dj_instructions.begin();
				model.set_inst_dj(dj_);					// set current instruction
				dj_.add("inst_index", view.instruction_index);
				ctx->error_count += (instruction_index - view.instruction_index) + 1;
				ctx->instruction_count++;
				instruction_index = view.instruction_index;
				lastCandidatesCount = (int)candidates.size();

				view_list branches;

				SolverPlanResult pres = plan.advance(model, view, branches);
				bool done = true;
				bool keep = false;
				switch (pres)
				{
				case FAIL:
					view.notify_error();
					break;
				case SUCCEED:
					root.copy(view);
					ctx->instruction_count--; // last doesn't count
					dj_.add("instruction_count", ctx->instruction_count);
					dj_.add("errors", ctx->error_count);
					dj_.text("END (SUCCEED)");
					return true;
				case BRANCH:
					dj_.add("branchs", branches.size());
					if (branches.empty())
						view.notify_error();

					for (typename view_list::iterator it = branches.begin(); it != branches.end(); it++)
					{
						View& branch = *it;

						double branch_error = model.eval(branch);
						branch.error = branch_error;
						branch.instruction_index = view.instruction_index;
						candidates.insert(candidate_pair(branch_error, branch)); //td: reduce copying
					}
					break;
				case CONTINUE:
					done = false;
					keep = true;
					break;
				}

				clock_t finish = clock();
				double time = (start > 0) ? (double)(finish - start) / CLOCKS_PER_SEC : -1;
				dj_.add("time", time);

				if (done)
					break;
			}

			candidates.erase(top);
		}

		ctx->error_count = ctx->instruction_count;
		dj_instructions.add("instruction_count", ctx->instruction_count);
		dj_instructions.add("errors", ctx->error_count);
		dj_instructions.add("result", "FAIL");

		if (max_iterations < 0) {
			dj_instructions.error("TOO MANY ITERATIONS");
			throw solver_errors::create(SERR_TOO_MANY_ITERATIONS);
		}

		return false; //indeed
	}

private:
	static context* init_context(context* user_context, context& default_context)
	{
		if (user_context)
		{
			user_context->max_iterations = default_context.max_iterations;
			user_context->max_error = default_context.max_error;
			user_context->min_error = default_context.min_error;

			return user_context;
		}

		return &default_context;
	}
};
