#include "phoenix/scene_update.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

phoenix::ActorNode make_actor(
    const char* id,
    std::vector<phoenix::ActorNode> children = {})
{
    phoenix::ActorNode actor;
    actor.id = id;
    actor.children = std::move(children);
    return actor;
}

phoenix::SceneRoot make_scene()
{
    phoenix::SceneRoot scene;
    scene.root = make_actor(
        "root",
        {
            make_actor("left"),
            make_actor(
                "middle",
                {
                    make_actor("middle.child"),
                    make_actor("middle.keep"),
                }),
            make_actor("right"),
        });
    return scene;
}

bool test_replace_child_subtree_preserves_sibling_order()
{
    auto scene = make_scene();

    auto replacement = make_actor(
        "middle",
        {
            make_actor("middle.new-a"),
            make_actor("middle.new-b"),
        });
    replacement.name = "updated";

    const phoenix::SceneUpdater updater;
    const auto result = updater.replace_actor_subtree(scene, replacement);

    return result.status == phoenix::SceneUpdateStatus::applied
        && !result.replaced_root
        && scene.root.id == "root"
        && scene.root.children.size() == 3
        && scene.root.children[0].id == "left"
        && scene.root.children[1].id == "middle"
        && scene.root.children[1].name.has_value()
        && *scene.root.children[1].name == "updated"
        && scene.root.children[1].children.size() == 2
        && scene.root.children[1].children[0].id == "middle.new-a"
        && scene.root.children[1].children[1].id == "middle.new-b"
        && scene.root.children[2].id == "right";
}

bool test_replace_nested_child_preserves_ancestors_and_siblings()
{
    auto scene = make_scene();

    auto replacement = make_actor(
        "middle.child",
        {
            make_actor("middle.child.leaf"),
        });
    replacement.prototype = phoenix::ActorPrototypeRef{"prototype-a"};

    const phoenix::SceneUpdater updater;
    const auto result = updater.replace_actor_subtree(scene, replacement);

    return result.status == phoenix::SceneUpdateStatus::applied
        && !result.replaced_root
        && scene.root.id == "root"
        && scene.root.children[0].id == "left"
        && scene.root.children[1].id == "middle"
        && scene.root.children[2].id == "right"
        && scene.root.children[1].children.size() == 2
        && scene.root.children[1].children[0].id == "middle.child"
        && scene.root.children[1].children[0].children.size() == 1
        && scene.root.children[1].children[0].children[0].id == "middle.child.leaf"
        && scene.root.children[1].children[0].prototype.has_value()
        && scene.root.children[1].children[0].prototype->prototype_id == "prototype-a"
        && scene.root.children[1].children[1].id == "middle.keep";
}

bool test_replace_root()
{
    auto scene = make_scene();
    auto replacement = make_actor("root", {make_actor("new-root-child")});
    replacement.name = "new root";

    const phoenix::SceneUpdater updater;
    const auto result = updater.replace_actor_subtree(scene, replacement);

    return result.status == phoenix::SceneUpdateStatus::applied
        && result.replaced_root
        && scene.root.id == "root"
        && scene.root.name.has_value()
        && *scene.root.name == "new root"
        && scene.root.children.size() == 1
        && scene.root.children[0].id == "new-root-child";
}

bool test_missing_actor_does_not_mutate_scene()
{
    auto scene = make_scene();

    const phoenix::SceneUpdater updater;
    const auto result = updater.replace_actor_subtree(scene, make_actor("missing"));

    return result.status == phoenix::SceneUpdateStatus::actor_not_found
        && !result.replaced_root
        && scene.root.id == "root"
        && scene.root.children.size() == 3
        && scene.root.children[0].id == "left"
        && scene.root.children[1].id == "middle"
        && scene.root.children[1].children.size() == 2
        && scene.root.children[1].children[0].id == "middle.child"
        && scene.root.children[1].children[1].id == "middle.keep"
        && scene.root.children[2].id == "right";
}

bool test_empty_replacement_id_is_invalid()
{
    auto scene = make_scene();

    const phoenix::SceneUpdater updater;
    const auto result = updater.replace_actor_subtree(scene, make_actor(""));

    return result.status == phoenix::SceneUpdateStatus::invalid_request
        && !result.replaced_root
        && scene.root.id == "root"
        && scene.root.children.size() == 3;
}

bool test_status_strings_are_stable()
{
    return std::string{phoenix::to_string(phoenix::SceneUpdateStatus::applied)} == "applied"
        && std::string{phoenix::to_string(phoenix::SceneUpdateStatus::actor_not_found)}
            == "actor_not_found"
        && std::string{phoenix::to_string(phoenix::SceneUpdateStatus::invalid_request)}
            == "invalid_request";
}

bool run_test(const char* name, bool (*test_fn)())
{
    const bool passed = test_fn();
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << '\n';
    return passed;
}

} // namespace

int main()
{
    bool ok = true;

    ok = run_test("replace child subtree preserves sibling order", test_replace_child_subtree_preserves_sibling_order) && ok;
    ok = run_test("replace nested child preserves ancestors and siblings", test_replace_nested_child_preserves_ancestors_and_siblings) && ok;
    ok = run_test("replace root", test_replace_root) && ok;
    ok = run_test("missing actor does not mutate scene", test_missing_actor_does_not_mutate_scene) && ok;
    ok = run_test("empty replacement id is invalid", test_empty_replacement_id_is_invalid) && ok;
    ok = run_test("status strings are stable", test_status_strings_are_stable) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
