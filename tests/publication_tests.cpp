#include "phoenix/publication.hpp"

#include <cstdlib>
#include <iostream>
#include <set>

namespace {

phoenix::CanonicalGeometryRef triangle(phoenix::FaceId face_id, double offset, bool new_ids = false)
{
    std::vector<phoenix::RuntimeVertex> vertices{
        {{offset, 0, 0}, new_ids ? phoenix::VertexId{} : phoenix::VertexId{1}, 0},
        {{offset + 1, 0, 0}, new_ids ? phoenix::VertexId{} : phoenix::VertexId{2}, 1},
        {{offset, 0, 1}, new_ids ? phoenix::VertexId{} : phoenix::VertexId{3}, 2},
    };
    std::vector<phoenix::RuntimeHalfedge> edges{
        {0, 0, 1, 2, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            new_ids ? phoenix::HalfedgeId{} : phoenix::HalfedgeId{4},
            new_ids ? phoenix::EdgeId{} : phoenix::EdgeId{7}, phoenix::LabelId{10}},
        {1, 0, 2, 0, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            new_ids ? phoenix::HalfedgeId{} : phoenix::HalfedgeId{5},
            new_ids ? phoenix::EdgeId{} : phoenix::EdgeId{8}, phoenix::LabelId{11}},
        {2, 0, 0, 1, phoenix::invalid_geometry_index, phoenix::invalid_geometry_index,
            new_ids ? phoenix::HalfedgeId{} : phoenix::HalfedgeId{6},
            new_ids ? phoenix::EdgeId{} : phoenix::EdgeId{9}, phoenix::LabelId{12}},
    };
    return phoenix::CanonicalGeometry::create(
        std::move(vertices), std::move(edges),
        {{0, new_ids ? phoenix::FaceId{} : face_id, phoenix::LabelId{20}}});
}

bool test_consuming_replaces_original_and_assigns_ids()
{
    phoenix::GeometryPublicationLedger ledger;
    ledger.set_actor_source("actor", triangle(phoenix::FaceId{10}, 0));
    const auto commit = ledger.replace_scope({"root", {}, 1}, "actor", true, {{0, true, triangle({}, 2, true), {{"actor", phoenix::FaceId{10}}}, {}}});
    (void)commit;
    const auto final = ledger.assemble_actor("actor");
    if (!final || final->faces().size() != 1 || final->faces()[0].id == phoenix::FaceId{10}) return false;
    std::set<std::uint64_t> ids;
    for (const auto& vertex : final->vertices()) ids.insert(vertex.id.value());
    for (const auto& edge : final->halfedges()) {
        ids.insert(edge.id.value());
        ids.insert(edge.edge_id.value());
    }
    ids.insert(final->faces()[0].id.value());
    return ids.size() == 10;
}

bool test_select_does_not_consume()
{
    phoenix::GeometryPublicationLedger ledger;
    auto source = triangle(phoenix::FaceId{10}, 0);
    ledger.set_actor_source("actor", source);
    phoenix::FaceReference selected{source, 0, phoenix::FaceId{10}};
    const auto commit = ledger.replace_scope({"root", {}, 2}, "actor", false, {{0, true, nullptr, {{"actor", phoenix::FaceId{10}}}, {}}});
    (void)commit;
    const auto final = ledger.assemble_actor("actor");
    return selected.valid() && final && final->faces().size() == 1
        && final->faces()[0].id == phoenix::FaceId{10};
}

bool test_failed_item_is_unconsumed_and_success_only_consumes()
{
    phoenix::GeometryPublicationLedger ledger;
    ledger.set_actor_source("actor", triangle(phoenix::FaceId{10}, 0));
    const auto commit = ledger.replace_scope({"root", {}, 3}, "actor", true, {
        {0, false, nullptr, {{"actor", phoenix::FaceId{10}}}, "failed"},
    });
    return !ledger.is_consumed({"actor", phoenix::FaceId{10}})
        && !commit.diagnostics.empty()
        && ledger.assemble_actor("actor")->faces().size() == 1;
}

bool test_two_branches_keep_both_replacements()
{
    phoenix::GeometryPublicationLedger ledger;
    ledger.set_actor_source("actor", triangle(phoenix::FaceId{10}, 0));
    const auto first_commit = ledger.replace_scope({"root", {}, 4}, "actor", true, {{0, true, triangle({}, 2, true), {{"actor", phoenix::FaceId{10}}}, {}}});
    const auto second_commit = ledger.replace_scope({"root", {}, 5}, "actor", true, {{0, true, triangle({}, 4, true), {{"actor", phoenix::FaceId{10}}}, {}}});
    (void)first_commit;
    (void)second_commit;
    const auto final = ledger.assemble_actor("actor");
    return final && final->faces().size() == 2;
}

bool test_nested_owner_and_rerun_restore()
{
    phoenix::GeometryPublicationLedger ledger;
    ledger.set_actor_source("parent", triangle(phoenix::FaceId{10}, 0));
    const phoenix::PublicationScopeKey child_scope{"child", {"root", "child"}, 6};
    const auto child_commit = ledger.replace_scope(child_scope, "parent", true, {{0, true, triangle({}, 2, true), {{"parent", phoenix::FaceId{10}}}, {}}});
    (void)child_commit;
    if (ledger.assemble_actor("parent")->faces().size() != 1
        || !ledger.is_consumed({"parent", phoenix::FaceId{10}})) return false;
    const auto restore_commit = ledger.replace_scope(child_scope, "parent", true, {});
    (void)restore_commit;
    const auto restored = ledger.assemble_actor("parent");
    return restored && restored->faces().size() == 1
        && restored->faces()[0].id == phoenix::FaceId{10}
        && !ledger.is_consumed({"parent", phoenix::FaceId{10}});
}

bool test_completion_order_is_canonical()
{
    phoenix::GeometryPublicationLedger first;
    phoenix::GeometryPublicationLedger second;
    first.set_actor_source("actor", nullptr);
    second.set_actor_source("actor", nullptr);
    std::vector<phoenix::GeometryItemEffect> forward{
        {1, true, triangle({}, 1, true), {}, {}},
        {2, true, triangle({}, 2, true), {}, {}},
    };
    auto reverse = forward;
    std::reverse(reverse.begin(), reverse.end());
    const auto first_commit = first.replace_scope({"root", {}, 1}, "actor", false, forward);
    const auto second_commit = second.replace_scope({"root", {}, 1}, "actor", false, reverse);
    (void)first_commit;
    (void)second_commit;
    return first.assemble_actor("actor")->fingerprint()
        == second.assemble_actor("actor")->fingerprint();
}

} // namespace

int main()
{
    const bool ok = test_consuming_replaces_original_and_assigns_ids()
        && test_select_does_not_consume()
        && test_failed_item_is_unconsumed_and_success_only_consumes()
        && test_two_branches_keep_both_replacements()
        && test_nested_owner_and_rerun_restore()
        && test_completion_order_is_canonical();
    if (!ok) {
        std::cerr << "publication tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "publication tests passed\n";
    return EXIT_SUCCESS;
}
