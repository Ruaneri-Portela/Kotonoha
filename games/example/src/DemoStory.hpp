#pragma once

#include <Kotonoha/script/VNScript.hpp>
#include <functional>

namespace KotonohaExample {

struct DemoState {
  std::string playerName;
  bool hasTorch = false;
  bool hasMap = false;
  int trustLevel = 0;
};

namespace StoryNode {
constexpr int Opening = 100;
constexpr int Camp = 0;
constexpr int AskName = 2;
constexpr int Greeting = 3;
constexpr int Equipment = 4;
constexpr int Storm = 30;
constexpr int Route = 10;
constexpr int Cave = 11;
constexpr int Mountain = 12;
constexpr int Escape = 40;
constexpr int GoodEnding = 20;
constexpr int BadEnding = 21;
} // namespace StoryNode

std::vector<Kotonoha::ScriptNode>
buildDemoStory(DemoState &state, std::function<void()> requestName);

} // namespace KotonohaExample
