#pragma once

#include "phoenix/execution.hpp"
#include "phoenix/scripting/contract.hpp"
#include "phoenix/scripting/invocation_host.hpp"

#include <memory>

namespace phoenix::scripting {

struct InstructionConfig {
    Program program{"phoenix-js-script",1,{}};
    Bindings bindings;
    LabelBindings labels;
    std::vector<ScriptRequest::LibraryAsset> libraries;
    std::vector<ScriptOutputSpec> outputs;
    Limits limits;
    std::shared_ptr<const ScriptEngine> engine;
};

[[nodiscard]] InstructionHandler make_instruction_handler(InstructionConfig config);

} // namespace phoenix::scripting
