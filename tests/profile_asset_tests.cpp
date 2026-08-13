#include "phoenix/profiles/asset.hpp"

#include <cstdlib>
#include <iostream>

namespace {

phoenix::profiles::SegmentDefinition segment(double x, double y, int label)
{
    phoenix::profiles::SegmentDefinition result;
    result.delta = {x, y};
    result.labels.face_label = phoenix::LabelId{label};
    result.labels.left_label = phoenix::LabelId{label + 1};
    result.labels.bottom_label = phoenix::LabelId{label + 2};
    result.labels.right_label = phoenix::LabelId{label + 3};
    result.labels.top_label = phoenix::LabelId{label + 4};
    result.labels.skirt_label = phoenix::LabelId{label + 5};
    return result;
}

} // namespace

int main()
{
    using namespace phoenix::profiles;
    Definition definition;
    definition.id = "profile:cornice";
    definition.name = "Cornice";
    definition.version = 3;
    definition.segments = {segment(0, 2, 10), segment(1, 0, 20)};
    definition.interpolation_target = {segment(0, 4, 10), segment(3, 0, 20)};
    const auto asset = Asset::create(definition);
    const auto same = Asset::create(definition);
    definition.version = 4;
    const auto changed = Asset::create(definition);
    const bool identity = asset && same && changed
        && asset->fingerprint() == same->fingerprint()
        && asset->fingerprint() != changed->fingerprint();
    Registry registry;
    std::string conflict;
    const bool ownership = registry.register_asset(asset)
        && registry.register_asset(same)
        && !registry.register_asset(changed, &conflict)
        && registry.resolve("profile:cornice") == asset
        && registry.fingerprint() != 0 && !conflict.empty();

    const auto interpolated = evaluate({asset, 0.5, 0.0, {}, 7});
    const EvaluationRequest cache_a{asset, 0.5, 0.0, {}, 7};
    const EvaluationRequest cache_b{asset, 0.5, 0.0, {}, 8};
    const bool cache_identity = evaluation_fingerprint(cache_a)
        != evaluation_fingerprint(cache_b);
    const bool interpolation = interpolated.success()
        && interpolated.profile->size() == 2
        && interpolated.profile->segment(0).delta_y == 3.0
        && interpolated.profile->segment(1).delta_x == 2.0
        && interpolated.profile->segment(1).face_label == phoenix::LabelId{20};

    Definition repeat_definition;
    repeat_definition.id = "profile:repeat";
    auto repeated_segment = segment(0, 1, 30);
    repeated_segment.repeat = RepeatMarker::start;
    repeated_segment.repeat_amount = 2;
    repeat_definition.segments = {repeated_segment};
    const auto repeat_asset = Asset::create(repeat_definition);
    const auto repeated = evaluate({repeat_asset, 0.0, 0.0, {}, 19});
    const bool repeat = repeated.success() && repeated.profile->size() == 2;

    Definition bezier_definition;
    bezier_definition.id = "profile:bezier";
    auto curve = segment(2, 2, 40);
    curve.bezier = BezierDefinition{{0, 1}, {1, 2}, 0.1, 4};
    bezier_definition.segments = {curve};
    const auto bezier_asset = Asset::create(bezier_definition);
    const auto bezier = evaluate({bezier_asset});
    const bool tessellation = bezier.success() && bezier.profile->size() == 4;

    Definition variable_definition;
    variable_definition.id = "profile:variables";
    auto variable = segment(1, 1, 50);
    variable.variables.height = "height";
    variable.variables.width = "width";
    variable_definition.segments = {variable};
    const auto variable_asset = Asset::create(variable_definition);
    EvaluationRequest variable_request;
    variable_request.asset = variable_asset;
    variable_request.variables = {{"height", 5.0}, {"width", 2.0}};
    const auto variables = evaluate(variable_request);
    const bool bindings = variables.success()
        && variables.profile->segment(0).delta_x == 2.0
        && variables.profile->segment(0).delta_y == 5.0;

    Definition horizontal_definition;
    horizontal_definition.id = "profile:horizontal";
    horizontal_definition.segments = {segment(1, 0, 60)};
    std::string horizontal_error;
    const bool explicit_sign_required = !Asset::create(
        horizontal_definition, &horizontal_error) && !horizontal_error.empty();
    horizontal_definition.sign = CGAL::NEGATIVE;
    const auto horizontal = Asset::create(horizontal_definition);
    const bool explicit_sign = horizontal
        && evaluate({horizontal}).profile->sign() == CGAL::NEGATIVE;

    std::cout << "profile immutable identity: " << identity << '\n'
              << "profile registry ownership: " << ownership << '\n'
              << "profile evaluation cache identity: " << cache_identity << '\n'
              << "profile interpolation: " << interpolation << '\n'
              << "profile repeat expansion: " << repeat << '\n'
              << "profile Bezier tessellation: " << tessellation << '\n'
              << "profile variable bindings: " << bindings << '\n'
              << "profile explicit horizontal sign: "
              << (explicit_sign_required && explicit_sign) << '\n';
    return identity && ownership && cache_identity && interpolation
        && repeat && tessellation && bindings
        && explicit_sign_required && explicit_sign ? EXIT_SUCCESS : EXIT_FAILURE;
}
