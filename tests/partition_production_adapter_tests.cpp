#include "phoenix/partition/production_adapter.hpp"
#include "phoenix/partition/ported/production/partition_solver.h"
#include "phoenix/partition/ported/production/partition_solver_constraints.h"

#include <cstdlib>
#include <iostream>
#include <set>

namespace {

phoenix::CanonicalGeometryRef rectangle()
{
    using namespace phoenix;
    std::vector<RuntimeVertex> vertices{
        {{1, 2, 3}, VertexId{1}, 0},
        {{5, 2, 3}, VertexId{2}, 1},
        {{5, 5, 6}, VertexId{3}, 2},
        {{1, 5, 6}, VertexId{4}, 3},
    };
    std::vector<RuntimeHalfedge> halfedges{
        {0, 0, 1, 3, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{10}, EdgeId{20}, LabelId{30}},
        {1, 0, 2, 0, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{11}, EdgeId{21}, LabelId{31}},
        {2, 0, 3, 1, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{12}, EdgeId{22}, LabelId{32}},
        {3, 0, 0, 2, invalid_geometry_index, invalid_geometry_index,
            HalfedgeId{13}, EdgeId{23}, LabelId{33}},
    };
    return CanonicalGeometry::create(std::move(vertices), std::move(halfedges),
        {{0, FaceId{40}, LabelId{41}}});
}

phoenix::partition::ProductionPartitionResult run_model(
    phoenix::CanonicalGeometryRef source, partition_model& model,
    phoenix::RunElementIdAllocator& ids)
{
    phoenix::partition::ProductionPartitionRequest request;
    request.source = {std::move(source), 0, phoenix::FaceId{40}};
    request.model = &model;
    request.random_seed = 17;
    request.base_segment_labels = {
        {0, phoenix::LabelId{30}}, {1, phoenix::LabelId{32}}};
    return phoenix::partition::run_production_partition(request, ids);
}

void configure_root_cut(partition_cut& cut)
{
    cut.id = 0;
    cut.segment = cut_segment_id(2);
    cut.src = cut_segment_id(0);
    cut.dst = cut_segment_id(1);
    cut.faceLeft = 50;
    cut.faceRight = 51;
    cut.cutLeft = 60;
    cut.cutRight = 61;
}

} // namespace

int main()
{
    using namespace phoenix;
    std::cout << std::unitbuf;
    const auto source = rectangle();
    partition_model model(nullptr, 0);
    RunElementIdAllocator ids(1000);
    partition::ProductionPartitionRequest request;
    request.source = {source, 0, FaceId{40}};
    request.model = &model;
    request.random_seed = 17;

    const auto result = partition::run_production_partition(request, ids);
    const bool round_trip = result.success()
        && result.consumed_face_id == FaceId{40}
        && result.geometry->faces().size() == 1
        && result.geometry->faces()[0].label == LabelId{41}
        && result.geometry->halfedges().size() == 4;
    std::cout << "production partition canonical round trip: " << round_trip << '\n';

    partition_cut cut(nullptr);
    configure_root_cut(cut);
    partition_model cut_model(&cut, 2);
    RunElementIdAllocator cut_ids(2000);
    request.model = &cut_model;
    request.base_segment_labels = {{0, LabelId{30}}, {1, LabelId{32}}};
    const auto cut_result = partition::run_production_partition(request, cut_ids);
    std::set<std::int32_t> face_labels;
    std::set<std::int32_t> edge_labels;
    if (cut_result.geometry) {
        for (const auto& face : cut_result.geometry->faces())
            face_labels.insert(face.label.value());
        for (const auto& edge : cut_result.geometry->halfedges())
            edge_labels.insert(edge.label.value());
    }
    const bool one_cut = cut_result.success()
        && cut_result.consumed_face_id == FaceId{40}
        && cut_result.geometry->faces().size() == 2
        && face_labels == std::set<std::int32_t>{50, 51}
        && edge_labels.count(60) == 1
        && edge_labels.count(61) == 1;
    std::cout << "production partition deterministic one cut: " << one_cut;
    if (!one_cut) std::cout << " (" << cut_result.error << ")";
    std::cout << '\n';

    GeometryPublicationLedger ledger;
    const ActorId actor{"partition-test-actor"};
    ledger.set_actor_source(actor, source);
    const auto commit = ledger.replace_scope(
        {FunctionId{"partition-test"}, {}, NodeId{2}}, actor, true,
        {cut_result.publication_effect(7, actor)});
    const auto assembled = ledger.assemble_actor(actor);
    const bool replacement = assembled && assembled->faces().size() == 2
        && ledger.is_consumed({actor, FaceId{40}})
        && commit.diagnostics.empty();
    std::cout << "production partition consumes and replaces source: "
              << replacement << '\n';

    partition_cut recursive_root(nullptr);
    configure_root_cut(recursive_root);
    partition_cut recursive_child(&recursive_root);
    recursive_child.id = 1;
    recursive_child.segment = cut_segment_id(7);
    recursive_child.src = cut_segment_id(3); // root SOURCE_LEFT
    recursive_child.dst = cut_segment_id(2); // root CUT_SEGMENT
    recursive_child.faceLeft = 70;
    recursive_child.faceRight = 71;
    recursive_child.cutLeft = 72;
    recursive_child.cutRight = 73;
    recursive_root.left = &recursive_child;
    partition_model recursive_model(&recursive_root, 2);
    RunElementIdAllocator recursive_ids(3000);
    const auto recursive_result = run_model(source, recursive_model, recursive_ids);
    std::set<std::int32_t> recursive_face_labels;
    if (recursive_result.geometry) {
        for (const auto& face : recursive_result.geometry->faces())
            recursive_face_labels.insert(face.label.value());
    }
    const bool recursive = recursive_result.success()
        && recursive_result.geometry->faces().size() == 3
        && recursive_face_labels == std::set<std::int32_t>{51, 70, 71};
    std::cout << "production partition recursive cuts: " << recursive;
    if (!recursive) std::cout << " (" << recursive_result.error << ")";
    std::cout << '\n';

    partition_cut repeat_cut(nullptr);
    configure_root_cut(repeat_cut);
    repeat_cut.repeat.kind = REPEAT_N_TIMES;
    repeat_cut.repeat.count = vm::variable_value(3.0);
    repeat_cut.repeat.secondary = vm::variable_value(0.25);
    repeat_cut.repeat.faceLabel = 80;
    repeat_cut.repeat.primaryEdgeLabel = 81;
    partition_model repeat_model(&repeat_cut, 2);
    RunElementIdAllocator repeat_ids(4000);
    const auto repeat_result = run_model(source, repeat_model, repeat_ids);
    std::set<std::int32_t> repeat_face_labels;
    if (repeat_result.geometry) {
        for (const auto& face : repeat_result.geometry->faces())
            repeat_face_labels.insert(face.label.value());
    }
    const bool repeated = repeat_result.success()
        && repeat_result.geometry->faces().size() == 3
        && repeat_face_labels.count(80) == 1;
    std::cout << "production partition repeat distribution: " << repeated;
    if (!repeated) std::cout << " (" << repeat_result.error << ", faces="
        << (repeat_result.geometry ? repeat_result.geometry->faces().size() : 0) << ")";
    std::cout << '\n';

    partition_cut bezier_cut(nullptr);
    configure_root_cut(bezier_cut);
    bezier_cut.control_points.enabled = true;
    bezier_cut.control_points.cp1_pct_x = 0.25;
    bezier_cut.control_points.cp1_pct_y = 0.20;
    bezier_cut.control_points.cp2_pct_x = 0.75;
    bezier_cut.control_points.cp2_pct_y = -0.20;
    bezier_cut.control_points.bez_options.step_length = vm::variable_value(0.35);
    partition_model bezier_model(&bezier_cut, 2);
    RunElementIdAllocator bezier_ids(5000);
    const auto bezier_result = run_model(source, bezier_model, bezier_ids);
    const bool bezier = bezier_result.success()
        && bezier_result.geometry->faces().size() == 2
        && bezier_result.geometry->halfedges().size() > cut_result.geometry->halfedges().size();
    std::cout << "production partition Bezier cut: " << bezier;
    if (!bezier) std::cout << " (" << bezier_result.error << ")";
    std::cout << '\n';

    partition_cut constrained_cut(nullptr);
    configure_root_cut(constrained_cut);
    partition_model constrained_model(&constrained_cut, 2);
    constrained_model.add_constraint(partition_solver_constraint_ref(
        new segment_distance_constraint(0, 0, cut_segment_id(2),
            cut_segment_id(0), true, vm::variable_value(0.0),
            true, vm::variable_value(100.0))));
    RunElementIdAllocator constrained_ids(6000);
    const auto constrained_result = run_model(source, constrained_model, constrained_ids);
    const bool constrained = constrained_result.success()
        && constrained_model.plan().instruction_count() > cut_model.plan().instruction_count()
        && constrained_result.geometry->faces().size() == 2;
    std::cout << "production partition constraint execution: " << constrained;
    if (!constrained) std::cout << " (" << constrained_result.error << ")";
    std::cout << '\n';

    return round_trip && one_cut && replacement && recursive && repeated
        && bezier && constrained ? EXIT_SUCCESS : EXIT_FAILURE;
}
