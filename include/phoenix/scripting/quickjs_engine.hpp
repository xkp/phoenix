#pragma once

#include "phoenix/scripting/contract.hpp"

namespace phoenix::scripting {

class QuickJsEngine final : public ScriptEngine {
public:
    [[nodiscard]] std::string engine_id() const override;
    [[nodiscard]] std::string engine_version() const override;
    [[nodiscard]] EvaluationResult evaluate(const EvaluationRequest& request,
        const CancellationToken* cancellation=nullptr) const override;
    [[nodiscard]] ScriptResult execute_script(const ScriptRequest& request,
        GeometryScriptHost& transaction,
        const CancellationToken* cancellation=nullptr) const override;
};

} // namespace phoenix::scripting
