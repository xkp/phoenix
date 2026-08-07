#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/squared_distance_2.h>

#include <iostream>

using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_2 = Kernel::Point_2;

int main()
{
    const Point_2 a(0.0, 0.0);
    const Point_2 b(3.0, 4.0);

    const auto squared_distance = CGAL::squared_distance(a, b);

    std::cout << "CGAL is configured correctly." << '\n';
    std::cout << "Squared distance between A and B: " << squared_distance << '\n';

    return 0;
}
