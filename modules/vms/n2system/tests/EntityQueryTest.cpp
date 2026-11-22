// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Adapted behavioral cases from NitroSharp.Tests.QueryParsingTests /
// QueryExecutionTests (CommitteeOfZero/nitrosharp, branch rebuild).
#include <N2System/EntityQuery.hpp>
#include <N2System/SceneGraph.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
std::vector<std::string> names(std::vector<N2System::Entity *> entities) {
  std::vector<std::string> out;
  for (const auto *entity : entities)
    out.push_back(entity->name);
  return out;
}
void matches(N2System::SceneGraph &scene, std::string_view query,
             std::vector<std::string> expected, const char *message) {
  require(names(scene.queryEntities(query)) == expected, message);
}
} // namespace

int main() {
  using N2System::EntityQuery;
  using N2System::EntityQueryScope;
  for (const char *valid : {
           "foo", "*", "foo/bar/baz", "@foo/bar", "foo/@bar",
           "@fo*/@b*r", "<@foo/bar", "<@foo/bar>", "<@foo/@bar",
           "<foo/bar", "foo>", "foo/bar>"}) {
    const auto query = EntityQuery::tryParse(valid);
    require(query.has_value(), "NitroSharp valid entity query was rejected");
  }
  for (const char *invalid : {
           "", "//", "foo/", "f@o", "f<o", "<", ">", "@",
           "<@", "<@>", "<foo>/bar", "foo/<bar", "@foo/<bar",
           "<@foo/<bar", "<foo/@bar", "<foo/<bar", "<foo/<@bar",
           "foo//bar", "foo/>", "foo/@>"}) {
    require(!EntityQuery::tryParse(invalid),
            "NitroSharp malformed query was accepted");
  }
  const auto parsed = EntityQuery::tryParse("<@foo/bar>");
  require(parsed && parsed->parts().size() == 2 &&
              parsed->parts()[0].scope == EntityQueryScope::AllAliases &&
              parsed->parts()[1].scope == EntityQueryScope::Current &&
              parsed->parts()[1].forceEnd,
          "Query scopes and terminal markers do not match NitroSharp");
  for (const char *validPath : {"foo", "foo/bar", "@foo/bar",
                                 "foo/@bar", "foo>"}) {
    require(N2System::EntityPath::tryParse(validPath).has_value(),
            "NitroSharp valid EntityPath rejected");
  }
  for (const char *invalidPath : {"*", "foo/*", "<foo", "<@foo",
                                   "foo/<bar", "foo/@b*r", "foo/"}) {
    require(!N2System::EntityPath::tryParse(invalidPath),
            "Wildcard/scoped query was accepted as an EntityPath");
  }
  require(EntityQuery::matchPattern("", "*") &&
              EntityQuery::matchPattern("foobar", "f*r") &&
              EntityQuery::matchPattern("bar", "b**") &&
              !EntityQuery::matchPattern("foo", "f?o") &&
              !EntityQuery::matchPattern("foobar", "foo"),
          "NitroSharp only supports asterisk wildcard (question mark literal)");

  N2System::SceneGraph scene;
  scene.createEntity("foo1");
  scene.createEntity("foo2");
  scene.createEntity("foo1/bar");
  scene.createEntity("foo2/bar");
  require(scene.setAlias("foo1", "cat1") &&
              scene.setAlias("foo2/bar", "cat2"),
          "Test aliases must initialize");
  matches(scene, "foo1", {"foo1"}, "Exact root lookup");
  matches(scene, "foo1/bar", {"foo1/bar"}, "Absolute child lookup");
  matches(scene, "*", {"foo1", "foo2"}, "Wildcard root query");
  matches(scene, "*/*", {"foo1/bar", "foo2/bar"},
          "Wildcard must traverse one child level");
  matches(scene, "@*", {"foo1", "foo2/bar"},
          "Aliases must resolve globally within current process");
  matches(scene, "cat1", {"foo1"},
          "Legacy bare aliases remain supported in native NSB scripts");
  matches(scene, "foo1/@*", {"foo1", "foo2/bar"},
          "Current aliases reset the query scope as in NitroSharp");
  matches(scene, "<@cat1/bar", {"foo1/bar"},
          "Global alias followed by child lookup");
  require(scene.findEntity("@cat1/bar") &&
              scene.findEntity("@cat1/bar")->name == "foo1/bar",
          "Exact alias + child path must resolve like NitroSharp.World.Get");
  require(scene.findEntity("foo1/@cat2") &&
              scene.findEntity("foo1/@cat2")->name == "foo2/bar",
          "Alias path components switch to process alias scope");
  require(!scene.findEntity("@cat1/noSuchChild"),
          "Missing aliases or child paths must not resolve");
  matches(scene, "foo1/b*", {"foo1/bar"},
          "Component wildcard lookup");
  matches(scene, "foo1/b*/missing", {}, "No implicit missing descendants");
  matches(scene, "foo1/*/*", {}, "Wildcards do not consume slashes");
  matches(scene, "foo1//bar", {}, "Malformed path cannot match");

  // Legacy names may encode a complete absolute path with no container
  // objects. Wildcards still match exactly one path component.
  scene.createEntity("loose/container/leaf");
  matches(scene, "loose/container/*", {"loose/container/leaf"},
          "Legacy absolute child path must resolve without materialized parents");
  matches(scene, "loose/*", {},
          "Legacy wildcard must never swallow a slash");

  // The native VM also creates explicit parent relationships rather than
  // slash-qualified names. Both representations must support the same query.
  scene.createEntity("bar3");
  require(scene.setParent("bar3", "foo1"), "Attach explicit parent");
  matches(scene, "foo1/bar3", {"bar3"}, "Explicit child path resolution");
  matches(scene, "foo2/bar3", {}, "Child not found in unrelated branch");
  scene.createEntity("foo3");
  require(scene.setParent("bar3", "foo3"), "Reparenting must succeed");
  const auto *oldParent = scene.findEntity("foo1");
  const auto *newParent = scene.findEntity("foo3");
  require(oldParent && newParent && oldParent->children.empty() &&
              newParent->children == std::vector<std::string>{"bar3"},
          "Reparenting must remove stale child links");
  matches(scene, "foo3/bar3", {"bar3"}, "New parent owns moved child");
  matches(scene, "foo1/bar3", {}, "Old parent no longer owns moved child");
  require(!scene.setParent("foo3", "bar3"),
          "Cycles must be rejected before modifying the scene graph");
  require(!scene.setParent("foo3", "foo3"),
          "An entity may not be its own parent");
  require(scene.destroyEntity("foo3"), "Destroy parent with explicit child");
  require(!scene.findEntity("bar3") && !scene.findEntity("foo3"),
          "Destroy must cascade to explicitly attached children");

  scene.createEntity("foo2/bar/deep");
  require(scene.destroyEntity("foo2"), "Destroy parent with qualified child");
  require(!scene.findEntity("foo2/bar") &&
              !scene.findEntity("foo2/bar/deep") &&
              scene.findEntity("foo1/bar"),
          "Destroy must cascade through qualified child paths only");
  matches(scene, "*", {"foo1"},
          "Remaining root order must be deterministic after deletion");

  // World.AddEntity semantics: recreating an existing named node must not
  // inherit the previous sprite's alpha, alias, animation, or descendants.
  auto *original = scene.createEntity("reused", N2System::EntityType::Sprite);
  require(original != nullptr, "Initial entity creation");
  original->opacity = 0.25f;
  original->isMoving = true;
  require(scene.setAlias("reused", "stale"),
          "Initial entity alias");
  scene.createEntity("reused/child", N2System::EntityType::Text);
  auto *fresh = scene.createEntity("reused", N2System::EntityType::Movie);
  require(fresh && fresh->type == N2System::EntityType::Movie &&
              fresh->opacity == 1.0f && !fresh->isMoving &&
              fresh->alias.empty() && fresh->children.empty() &&
              !scene.findEntity("reused/child"),
          "Duplicate Create* must atomically replace old scene subtree");
  matches(scene, "@stale", {}, "Replacement must clear the old alias");

  scene.clear();
  auto *first = scene.createEntity("first");
  require(first && first->creationOrder == 1,
          "Scene reset must also reset insertion ordering");
  scene.createEntity("second");
  matches(scene, "*", {"first", "second"},
          "Entity queries must preserve insertion rather than hash order");
  scene.createEntity("first");
  matches(scene, "*", {"second", "first"},
          "Replacing an entity moves the new instance to the end");
  std::cout << "NitroSharp entity-query parity regressions passed\n";
  return 0;
}
