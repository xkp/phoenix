#pragma once

#include <boost/property_tree/ptree.hpp>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <stdexcept>

#include "phoenix/partition/ported/randomizer.h"

using property_tree = boost::property_tree::ptree;
using uniqueid = long;

namespace vm {

struct icontext {
    std::unordered_map<std::string, double> values;
    mutable int next_vertex_id{0};
    mutable int next_edge_id{0};

    [[nodiscard]] const icontext* fctx() const noexcept { return this; }
    [[nodiscard]] int vertex_id() const noexcept { return ++next_vertex_id; }
    [[nodiscard]] int edge_id() const noexcept { return ++next_edge_id; }

    template <typename Arrangement, typename Halfedge, typename Point>
    Halfedge split_edge(Arrangement& arrangement, Halfedge source,
                        const Point& point) const {
        using Segment = typename Arrangement::Geometry_traits_2::Curve_2;
        const Segment first(source->source()->point(), point);
        const Segment second(point, source->target()->point());
        Halfedge result;
        try {
            result = arrangement.split_edge(source, first, second);
        } catch (...) {
            throw std::logic_error("splitting edge");
        }
        result->next()->data() = result->data();
        result->twin()->prev()->data() = result->twin()->data();
        result->target()->data().id = vertex_id();
        return result;
    }
};

using const_icontext_ref = std::shared_ptr<const icontext>;

class variable_value {
public:
    variable_value() = default;
    explicit variable_value(double value) noexcept
        : empty_(false), literal_(value), default_value_(0.0) {}

    variable_value(const property_tree& data, const std::string& name,
                   double default_value) {
        load(data, name, default_value);
    }

    variable_value(const std::string& name_or_value, double default_value)
        : default_value_(default_value) {
        set(name_or_value);
    }

    explicit operator bool() const noexcept { return !empty_; }

    [[nodiscard]] double operator()(const_icontext_ref context) const {
        return value(std::move(context));
    }

    bool load(const property_tree& data, const std::string& name,
              double default_value) {
        default_value_ = default_value;
        const auto raw = data.get_optional<std::string>(name);
        if (!raw || raw->empty()) {
            empty_ = true;
            raw_.clear();
            name_.clear();
            return false;
        }
        set(*raw);
        return true;
    }

    [[nodiscard]] double value(const_icontext_ref context) const {
        if (literal_) {
            return *literal_;
        }
        if (context) {
            const auto found = context->values.find(name_);
            if (found != context->values.end()) {
                return found->second;
            }
        }
        return default_value_;
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& raw() const noexcept { return raw_; }

private:
    bool empty_{true};
    std::optional<double> literal_;
    std::string name_;
    std::string raw_;
    double default_value_{0.0};

    void set(const std::string& input) {
        raw_ = input;
        empty_ = input.empty();
        literal_.reset();
        name_.clear();
        if (empty_) {
            return;
        }

        char* end = nullptr;
        const double parsed = std::strtod(input.c_str(), &end);
        if (end != input.c_str() && *end == '\0') {
            literal_ = parsed;
            return;
        }

        if (input.size() >= 2 && input.front() == '[' && input.back() == ']') {
            name_ = input.substr(1, input.size() - 2);
        } else {
            name_ = input;
        }
    }
};

}  // namespace vm
