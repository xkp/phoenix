#include "phoenix/actors.hpp"
#include "phoenix/execution.hpp"
#include "phoenix/graph.hpp"
#include "phoenix/randomness.hpp"

#include <iostream>

int main()
{
    const phoenix::FunctionDescriptor function{
        "smoke_test_function",
        {
            {"input", "geometry", phoenix::PortDirection::input},
        },
        {
            {"result", "geometry", phoenix::PortDirection::output},
        },
        {
            {
                1,
                "sample_instruction",
                {{"input", "geometry", phoenix::PortDirection::input}},
                {
                    {"output", "geometry", phoenix::PortDirection::output},
                    {"else", "geometry", phoenix::PortDirection::output},
                },
                false,
                false,
                true,
                std::nullopt,
            },
        },
        {},
        false,
    };

    const phoenix::GraphIndex index(function);
    const auto* instruction = index.find_instruction(1);
    const phoenix::GraphValidator validator;
    const auto validation = validator.validate(function);

    phoenix::SeedDerivationInput seed_input;
    seed_input.global_seed = 42;
    seed_input.call_path = {"root", "smoke"};
    seed_input.node_id = 1;

    const phoenix::SeedDeriver deriver;
    const auto derived_seed = deriver.derive(seed_input);

    phoenix::ActorNode root_actor;
    root_actor.id = "root";
    root_actor.name = "Smoke Root";

    std::cout << "Phoenix Phase 1 interfaces compile." << '\n';
    std::cout << "Function id: " << function.id << '\n';
    std::cout << "Instruction found: " << (instruction != nullptr ? "yes" : "no") << '\n';
    std::cout << "Validation ok: " << (validation.ok() ? "yes" : "no") << '\n';
    std::cout << "Derived seed: " << derived_seed << '\n';
    std::cout << "Root actor id: " << root_actor.id << '\n';

    return 0;
}
