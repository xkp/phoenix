#include "phoenix/profiles/asset.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace phoenix::profiles {
namespace {

void hash_u64(std::uint64_t& hash, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        hash ^= value & 0xffU;
        hash *= 1099511628211ULL;
        value >>= 8;
    }
}

void hash_string(std::uint64_t& hash, const std::string& value)
{
    hash_u64(hash, value.size());
    for (const auto character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
}

void hash_double(std::uint64_t& hash, double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    hash_u64(hash, bits);
}

void hash_segment(std::uint64_t& hash, const SegmentDefinition& segment)
{
    hash_double(hash, segment.delta.x); hash_double(hash, segment.delta.y);
    hash_u64(hash, segment.bezier.has_value());
    if (segment.bezier) {
        hash_double(hash, segment.bezier->control1.x);
        hash_double(hash, segment.bezier->control1.y);
        hash_double(hash, segment.bezier->control2.x);
        hash_double(hash, segment.bezier->control2.y);
        hash_double(hash, segment.bezier->tolerance);
        hash_u64(hash, segment.bezier->subdivisions.value_or(0));
    }
    hash_string(hash, segment.variables.height);
    hash_string(hash, segment.variables.width);
    hash_string(hash, segment.variables.length);
    hash_string(hash, segment.variables.angle);
    hash_u64(hash, segment.variables.global_height);
    hash_u64(hash, static_cast<std::uint64_t>(segment.repeat));
    hash_u64(hash, static_cast<std::uint32_t>(segment.repeat_amount));
    hash_u64(hash, static_cast<std::uint32_t>(segment.repeat_range));
    const auto& labels = segment.labels;
    hash_u64(hash, static_cast<std::uint32_t>(labels.face_label.value()));
    hash_u64(hash, static_cast<std::uint32_t>(labels.left_label.value()));
    hash_u64(hash, static_cast<std::uint32_t>(labels.bottom_label.value()));
    hash_u64(hash, static_cast<std::uint32_t>(labels.right_label.value()));
    hash_u64(hash, static_cast<std::uint32_t>(labels.top_label.value()));
    hash_u64(hash, static_cast<std::uint32_t>(labels.skirt_label.value()));
}

bool finite(Point2 point) { return std::isfinite(point.x) && std::isfinite(point.y); }

CGAL::Sign inferred_sign(const std::vector<SegmentDefinition>& segments)
{
    for (const auto& segment : segments) {
        if (segment.delta.y > 0.0) return CGAL::POSITIVE;
        if (segment.delta.y < 0.0) return CGAL::NEGATIVE;
    }
    return CGAL::ZERO;
}

double random_unit(std::uint64_t& state)
{
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return static_cast<double>(value >> 11) / static_cast<double>(1ULL << 53);
}

Point2 lerp(Point2 left, Point2 right, double amount)
{
    return {left.x + (right.x - left.x) * amount,
        left.y + (right.y - left.y) * amount};
}

Point2 cubic(Point2 c1, Point2 c2, Point2 end, double t)
{
    const auto u = 1.0 - t;
    return {3*u*u*t*c1.x + 3*u*t*t*c2.x + t*t*t*end.x,
        3*u*u*t*c1.y + 3*u*t*t*c2.y + t*t*t*end.y};
}

std::optional<double> binding(const std::string& name,
    const EvaluationRequest& request, double fixed)
{
    if (name.empty() || name == "*adjustable") return std::nullopt;
    if (name == "*fixed") return fixed;
    const auto found = request.variables.find(name);
    return found == request.variables.end() ? std::optional<double>{} : found->second;
}

bool apply_variables(SegmentDefinition& segment, const EvaluationRequest& request,
    double accumulated_height, CGAL::Sign sign, std::vector<std::string>& diagnostics)
{
    const auto original = segment.delta;
    auto height = binding(segment.variables.height, request, std::abs(original.y));
    auto width = binding(segment.variables.width, request, std::abs(original.x));
    auto length = binding(segment.variables.length, request, std::hypot(original.x, original.y));
    auto angle = binding(segment.variables.angle, request,
        std::atan2(std::abs(original.y), std::abs(original.x)) * 180.0 / 3.14159265358979323846);
    if (!segment.variables.height.empty() && !height && segment.variables.height != "*adjustable") {
        diagnostics.push_back("Missing profile variable: " + segment.variables.height);
        return false;
    }
    if (height && segment.variables.global_height)
        *height -= request.reference_height + accumulated_height;
    const auto xsign = original.x < 0.0 ? -1.0 : 1.0;
    const auto ysign = sign == CGAL::NEGATIVE ? -1.0 : 1.0;
    if (height && width) segment.delta = {xsign * *width, ysign * *height};
    else if (height && angle) {
        if (*angle < 0.0 || *angle > 90.0) {
            diagnostics.push_back("Profile angle must be in [0, 90].");
            return false;
        }
        const auto tangent = std::tan(*angle * 3.14159265358979323846 / 180.0);
        segment.delta = {tangent == 0.0 ? original.x : xsign * *height / tangent,
            ysign * *height};
    } else if (width && length) {
        if (*length < *width) return false;
        segment.delta = {xsign * *width, ysign * std::sqrt(*length * *length - *width * *width)};
    } else if (angle && length) {
        const auto radians = *angle * 3.14159265358979323846 / 180.0;
        segment.delta = {xsign * *length * std::cos(radians),
            ysign * *length * std::sin(radians)};
    }
    return finite(segment.delta);
}

} // namespace

std::shared_ptr<const Asset> Asset::create(Definition definition, std::string* error)
{
    auto fail = [&](std::string message) -> std::shared_ptr<const Asset> {
        if (error) *error = std::move(message);
        return nullptr;
    };
    if (definition.id.empty()) return fail("Profile asset ID cannot be empty.");
    if (definition.version == 0) return fail("Profile asset version must be positive.");
    if (definition.segments.empty()) return fail("Profile requires at least one segment.");
    if (!definition.interpolation_target.empty()
        && definition.interpolation_target.size() != definition.segments.size())
        return fail("Profile interpolation targets must match segment count.");
    for (const auto& segment : definition.segments) {
        if (!finite(segment.delta) || (segment.delta.x == 0.0 && segment.delta.y == 0.0))
            return fail("Profile segments must have finite, non-zero deltas.");
        if (segment.bezier && (!finite(segment.bezier->control1)
            || !finite(segment.bezier->control2)
            || !(segment.bezier->tolerance > 0.0)))
            return fail("Profile Bezier controls and tolerance must be valid.");
    }
    const auto inferred = inferred_sign(definition.segments);
    if (definition.sign == CGAL::ZERO) definition.sign = inferred;
    if (definition.sign == CGAL::ZERO)
        return fail("Horizontal-only profiles require an explicit sign.");
    for (const auto& segment : definition.segments)
        if (segment.delta.y != 0.0 && CGAL::sign(segment.delta.y) != definition.sign)
            return fail("Profile overhang changes vertical sign.");

    std::uint64_t hash = 14695981039346656037ULL;
    hash_string(hash, definition.id); hash_u64(hash, definition.version);
    hash_u64(hash, static_cast<std::uint64_t>(definition.sign));
    hash_u64(hash, static_cast<std::uint64_t>(definition.interpolation_mode));
    for (const auto& segment : definition.segments) hash_segment(hash, segment);
    hash_u64(hash, definition.interpolation_target.size());
    for (const auto& segment : definition.interpolation_target) hash_segment(hash, segment);
    return std::shared_ptr<const Asset>(new Asset(std::move(definition), hash));
}

bool Registry::register_asset(AssetRef asset, std::string* error)
{
    if (!asset) {
        if (error) *error = "Cannot register a null profile asset.";
        return false;
    }
    const auto& id = asset->definition().id;
    const auto found = assets_.find(id);
    if (found != assets_.end()) {
        if (found->second->fingerprint() == asset->fingerprint()) return true;
        if (error) *error = "Profile asset ID already refers to a different definition: " + id;
        return false;
    }
    assets_.emplace(id, std::move(asset));
    return true;
}

AssetRef Registry::resolve(const AssetId& id) const
{
    const auto found = assets_.find(id);
    return found == assets_.end() ? nullptr : found->second;
}

std::uint64_t Registry::fingerprint() const noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& item : assets_) {
        hash_string(hash, item.first);
        hash_u64(hash, item.second->fingerprint());
    }
    return hash;
}

std::uint64_t evaluation_fingerprint(const EvaluationRequest& request) noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    hash_u64(hash, request.asset ? request.asset->fingerprint() : 0);
    hash_double(hash, request.interpolation);
    hash_double(hash, request.reference_height);
    hash_u64(hash, request.random_seed);
    for (const auto& binding : request.variables) {
        hash_string(hash, binding.first);
        hash_double(hash, binding.second);
    }
    return hash;
}

EvaluationResult evaluate(const EvaluationRequest& request)
{
    EvaluationResult result;
    if (!request.asset) {
        result.diagnostics.push_back("Profile asset is missing.");
        return result;
    }
    if (!std::isfinite(request.interpolation) || request.interpolation < 0.0
        || request.interpolation > 1.0) {
        result.diagnostics.push_back("Profile interpolation must be in [0, 1].");
        return result;
    }
    auto segments = request.asset->definition().segments;
    const auto& target = request.asset->definition().interpolation_target;
    if (!target.empty()) {
        Point2 source_position{}, target_position{}, last{};
        for (std::size_t index = 0; index < segments.size(); ++index) {
            source_position = {source_position.x + segments[index].delta.x,
                source_position.y + segments[index].delta.y};
            target_position = {target_position.x + target[index].delta.x,
                target_position.y + target[index].delta.y};
            const auto position = lerp(source_position, target_position, request.interpolation);
            segments[index].delta = {position.x - last.x, position.y - last.y};
            if (segments[index].bezier && target[index].bezier) {
                segments[index].bezier->control1 = lerp(
                    segments[index].bezier->control1,
                    target[index].bezier->control1, request.interpolation);
                segments[index].bezier->control2 = lerp(
                    segments[index].bezier->control2,
                    target[index].bezier->control2, request.interpolation);
            }
            last = position;
        }
    }

    std::vector<SegmentDefinition> repeated;
    std::uint64_t random_state = request.random_seed ^ request.asset->fingerprint();
    for (std::size_t index = 0; index < segments.size();) {
        if (segments[index].repeat_amount < 0) {
            repeated.push_back(segments[index++]);
            continue;
        }
        auto end = index;
        while (end + 1 < segments.size() && segments[end].repeat != RepeatMarker::end) ++end;
        auto count = segments[index].repeat_amount;
        if (segments[index].repeat_range >= 0)
            count += static_cast<std::int32_t>(random_unit(random_state)
                * static_cast<double>(segments[index].repeat_range + 1));
        for (std::int32_t repeat = 0; repeat < count; ++repeat)
            repeated.insert(repeated.end(), segments.begin() + index, segments.begin() + end + 1);
        index = end + 1;
    }

    std::vector<extrusion::ProfileSegment> resolved;
    double accumulated_height = 0.0;
    for (auto segment : repeated) {
        if (!apply_variables(segment, request, accumulated_height,
            request.asset->definition().sign, result.diagnostics)) return result;
        accumulated_height += segment.delta.y;
        const auto emit = [&](Point2 delta) {
            if (std::abs(delta.x) < 1e-12 && std::abs(delta.y) < 1e-12) return;
            auto value = segment.labels;
            value.delta_x = delta.x;
            value.delta_y = std::abs(delta.y) < 1e-4 ? 0.0 : delta.y;
            value.horizontal = value.delta_y == 0.0;
            resolved.push_back(value);
        };
        if (!segment.bezier) {
            emit(segment.delta);
            continue;
        }
        const auto subdivisions = segment.bezier->subdivisions.value_or(
            std::max<std::size_t>(2, static_cast<std::size_t>(
                std::ceil(std::hypot(segment.delta.x, segment.delta.y)
                    / segment.bezier->tolerance))));
        Point2 last{};
        for (std::size_t step = 1; step <= subdivisions; ++step) {
            const auto point = cubic(segment.bezier->control1,
                segment.bezier->control2, segment.delta,
                static_cast<double>(step) / subdivisions);
            emit({point.x - last.x, point.y - last.y});
            last = point;
        }
    }
    result.profile = extrusion::Profile::create(
        std::move(resolved), request.asset->definition().sign);
    if (!result.profile) result.diagnostics.push_back("Resolved profile is empty or invalid.");
    return result;
}

} // namespace phoenix::profiles
