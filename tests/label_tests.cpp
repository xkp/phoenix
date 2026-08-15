#include "phoenix/labels.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

phoenix::LabelDefinition label(const char* name, const char* color = "")
{
    return phoenix::LabelDefinition{name, color, "", false};
}

bool test_registry_deduplicates_identical_definitions()
{
    phoenix::LabelRegistry registry;
    const auto definition = label("wall", "red");
    if (!registry.add("uid-a", definition, {"root", "a"})
        || !registry.add("uid-b", definition, {"child", "b"})) return false;
    registry.freeze();
    return registry.size() == 1
        && registry.find_uid("uid-a") == registry.find_uid("uid-b");
}

bool test_conflicting_uid_fails()
{
    phoenix::LabelRegistry registry;
    phoenix::LabelDiagnostic diagnostic;
    return registry.add("same", label("first"), {"root", "one"})
        && !registry.add("same", label("second"), {"child", "two"}, &diagnostic)
        && diagnostic.code == phoenix::LabelDiagnosticCode::conflicting_definition;
}

bool test_freeze_is_immutable()
{
    phoenix::LabelRegistry registry;
    if (!registry.add("a", label("a"), {"root", "one"})) return false;
    registry.freeze();
    phoenix::LabelDiagnostic diagnostic;
    return !registry.add("b", label("b"), {"root", "two"}, &diagnostic)
        && diagnostic.code == phoenix::LabelDiagnosticCode::registry_frozen;
}

bool test_allocation_and_fingerprint_are_order_independent()
{
    phoenix::LabelRegistry left;
    if (!left.add("z", label("z"), {"root", "z"})
        || !left.add("a", label("a"), {"root", "a"})) return false;
    left.freeze();

    phoenix::LabelRegistry right;
    if (!right.add("a", label("a"), {"root", "a"})
        || !right.add("z", label("z"), {"root", "z"})) return false;
    right.freeze();
    return left.find_uid("a") == right.find_uid("a")
        && left.find_uid("z") == right.find_uid("z")
        && left.semantic_fingerprint() == right.semantic_fingerprint();
}

bool test_linker_discovers_reachable_functions_and_local_visibility()
{
    phoenix::FunctionDescriptor root;
    root.id = "root";
    phoenix::InstructionDescriptor call;
    call.id = 1;
    call.called_function_id = "child";
    call.referenced_label_uids = {"root-label"};
    root.instructions = {call};

    phoenix::FunctionDescriptor child;
    child.id = "child";
    phoenix::InstructionDescriptor use;
    use.id = 2;
    use.referenced_label_uids = {"child-label"};
    child.instructions = {use};

    phoenix::LabelFunctionLibrary functions{{"child", child}};
    phoenix::FunctionLabelDeclarations declarations{
        {"root", {{"root-label", label("root"), "root.json"}}},
        {"child", {{"child-label", label("child"), "child.json"}}},
    };

    const auto linked = phoenix::LabelLinker{}.link(root, functions, declarations);
    return linked.ok() && linked.registry.frozen() && linked.registry.size() == 2
        && linked.function_tables.at("root").visible("root-label")
        && !linked.function_tables.at("root").visible("child-label")
        && linked.function_tables.at("child").visible("child-label")
        && linked.registry.find_uid("child-label").has_value();
}

bool test_unknown_reference_is_diagnostic()
{
    phoenix::FunctionDescriptor root;
    root.id = "root";
    phoenix::InstructionDescriptor use;
    use.id = 7;
    use.referenced_label_uids = {"missing"};
    root.instructions = {use};
    const auto linked = phoenix::LabelLinker{}.link(root, {}, {});
    return !linked.ok() && linked.diagnostics.size() == 1
        && linked.diagnostics[0].code == phoenix::LabelDiagnosticCode::unresolved_reference;
}

bool test_linker_allows_cross_function_uid_color_difference()
{
    phoenix::FunctionDescriptor root;
    root.id = "root";
    phoenix::InstructionDescriptor call;
    call.id = 1;
    call.called_function_id = "child";
    root.instructions = {call};

    phoenix::FunctionDescriptor child;
    child.id = "child";
    phoenix::LabelFunctionLibrary functions{{"child", child}};
    phoenix::FunctionLabelDeclarations declarations{
        {"root", {{"shared", label("wall", "red"), "root.json"}}},
        {"child", {{"shared", label("wall", "blue"), "child.json"}}},
    };
    const auto linked = phoenix::LabelLinker{}.link(root, functions, declarations);
    return linked.ok() && linked.registry.frozen()
        && linked.registry.find_uid("shared").has_value();
}

bool test_linker_rejects_cross_function_uid_material_conflict()
{
    phoenix::FunctionDescriptor root;
    root.id = "root";
    phoenix::InstructionDescriptor call;
    call.id = 1;
    call.called_function_id = "child";
    root.instructions = {call};

    phoenix::FunctionDescriptor child;
    child.id = "child";
    phoenix::LabelFunctionLibrary functions{{"child", child}};
    phoenix::FunctionLabelDeclarations declarations{
        {"root", {{"shared", phoenix::LabelDefinition{"wall", "red", "brick", false}, "root.json"}}},
        {"child", {{"shared", phoenix::LabelDefinition{"wall", "blue", "wood", false}, "child.json"}}},
    };
    const auto linked = phoenix::LabelLinker{}.link(root, functions, declarations);
    return !linked.ok() && !linked.registry.frozen()
        && linked.diagnostics.size() == 1
        && linked.diagnostics[0].code == phoenix::LabelDiagnosticCode::conflicting_definition
        && linked.diagnostics[0].message.find("existing_name='wall'") != std::string::npos
        && linked.diagnostics[0].message.find("incoming_name='wall'") != std::string::npos;
}

} // namespace

int main()
{
    const bool ok = test_registry_deduplicates_identical_definitions()
        && test_conflicting_uid_fails()
        && test_freeze_is_immutable()
        && test_allocation_and_fingerprint_are_order_independent()
        && test_linker_discovers_reachable_functions_and_local_visibility()
        && test_unknown_reference_is_diagnostic()
        && test_linker_allows_cross_function_uid_color_difference()
        && test_linker_rejects_cross_function_uid_material_conflict();
    if (!ok) {
        std::cerr << "label tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "label tests passed\n";
    return EXIT_SUCCESS;
}
