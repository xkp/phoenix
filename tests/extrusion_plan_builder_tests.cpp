#include "phoenix/extrusion/plan_builder.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {

bool test_simple_plan()
{
    phoenix::extrusion::PlanBuilder builder;
    using Point = phoenix::extrusion::PlanBuilder::Point;
    builder.add(Point{0, 0}, Point{1, 0}, 0, 3);
    builder.add(Point{1, 0}, Point{1, 1}, 1, 0);
    builder.add(Point{1, 1}, Point{0, 1}, 2, 1);
    builder.add(Point{0, 1}, Point{0, 0}, 3, 2);
    phoenix::extrusion::PlanBuilder::Plans plans;
    std::set<int> complexes;
    builder.get_plan(plans, complexes);
    if (plans.size() != 1 || plans[0].size() != 4 || !complexes.empty()) return false;
    std::sort(plans[0].begin(), plans[0].end());
    return plans[0] == phoenix::extrusion::PlanBuilder::Plan{0, 1, 2, 3};
}

bool test_clear_and_reuse()
{
    phoenix::extrusion::PlanBuilder builder;
    using Point = phoenix::extrusion::PlanBuilder::Point;
    builder.add(Point{0, 0}, Point{1, 0}, 0, 2);
    builder.add(Point{1, 0}, Point{0, 1}, 1, 0);
    builder.add(Point{0, 1}, Point{0, 0}, 2, 1);
    builder.clear_collisions();
    builder.add(Point{0, 0}, Point{2, 0}, 10, 12);
    builder.add(Point{2, 0}, Point{0, 2}, 11, 10);
    builder.add(Point{0, 2}, Point{0, 0}, 12, 11);
    phoenix::extrusion::PlanBuilder::Plans plans;
    std::set<int> complexes;
    builder.get_plan(plans, complexes);
    return plans.size() == 1 && plans[0].size() == 3;
}

} // namespace

int main()
{
    const bool ok = test_simple_plan() && test_clear_and_reuse();
    if (!ok) {
        std::cerr << "extrusion plan builder tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "extrusion plan builder tests passed\n";
    return EXIT_SUCCESS;
}
