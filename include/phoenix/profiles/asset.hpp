#pragma once

#include "phoenix/extrusion/profile.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace phoenix::profiles {

using AssetId = std::string;

enum class InterpolationMode { per_face, per_vertex };
enum class RepeatMarker { none, start, repeating, end };

struct Point2 {
    double x = 0.0;
    double y = 0.0;
};

struct BezierDefinition {
    Point2 control1;
    Point2 control2;
    double tolerance = 0.1;
    std::optional<std::size_t> subdivisions;
};

struct VariableBindings {
    std::string height;
    std::string width;
    std::string length;
    std::string angle;
    bool global_height = false;
};

struct SegmentDefinition {
    Point2 delta;
    std::optional<BezierDefinition> bezier;
    VariableBindings variables;
    RepeatMarker repeat = RepeatMarker::none;
    std::int32_t repeat_amount = -1;
    std::int32_t repeat_range = -1;
    extrusion::ProfileSegment labels;
};

struct Definition {
    AssetId id;
    std::string name;
    std::uint64_t version = 1;
    CGAL::Sign sign = CGAL::ZERO;
    std::vector<SegmentDefinition> segments;
    std::vector<SegmentDefinition> interpolation_target;
    InterpolationMode interpolation_mode = InterpolationMode::per_face;
};

class Asset final {
public:
    [[nodiscard]] static std::shared_ptr<const Asset> create(
        Definition definition, std::string* error = nullptr);
    [[nodiscard]] const Definition& definition() const noexcept { return definition_; }
    [[nodiscard]] std::uint64_t fingerprint() const noexcept { return fingerprint_; }

private:
    Asset(Definition definition, std::uint64_t fingerprint)
        : definition_(std::move(definition)), fingerprint_(fingerprint) {}
    Definition definition_;
    std::uint64_t fingerprint_ = 0;
};

using AssetRef = std::shared_ptr<const Asset>;

class Registry final {
public:
    [[nodiscard]] bool register_asset(AssetRef asset, std::string* error = nullptr);
    [[nodiscard]] AssetRef resolve(const AssetId& id) const;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;

private:
    std::map<AssetId, AssetRef> assets_;
};

struct EvaluationRequest {
    AssetRef asset;
    double interpolation = 0.0;
    double reference_height = 0.0;
    std::map<std::string, double> variables;
    std::uint64_t random_seed = 0;
};

struct EvaluationResult {
    extrusion::ProfileRef profile;
    std::vector<std::string> diagnostics;
    [[nodiscard]] bool success() const noexcept { return profile != nullptr; }
};

[[nodiscard]] EvaluationResult evaluate(const EvaluationRequest& request);
[[nodiscard]] std::uint64_t evaluation_fingerprint(
    const EvaluationRequest& request) noexcept;

} // namespace phoenix::profiles
