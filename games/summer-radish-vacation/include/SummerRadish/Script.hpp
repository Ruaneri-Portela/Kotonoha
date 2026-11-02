#pragma once
#include <SummerRadish/Pack.hpp>
#include <functional>
#include <map>
#include <variant>
namespace SummerRadish {
struct Node {
  std::string tag, text;
  std::map<std::string, std::string> attrs;
  std::vector<Node> children;
  std::string attr(const std::string &key) const;
};
Node parseXml(const std::string &);
using Value = std::variant<int64_t, std::string>;
int64_t number(const Value &);
std::string string(const Value &);
struct Instruction {
  Node node;
  size_t target = 0;
};
struct Script {
  std::vector<Instruction> code;
  std::map<std::string, size_t> labels;
  explicit Script(const Node &);
};
struct Position {
  std::string script;
  size_t pc = 0;
};
class Machine {
  std::map<std::string, Script> scripts;
  std::vector<Position> calls;
  void branch(const std::string &, bool call);

public:
  std::map<std::string, Value> variables;
  Position position;
  Node resource;
  bool ended = false, waiting = false;
  std::function<void(const std::string &, const std::vector<Value> &)> function;
  void add(const std::string &, const std::string &xml);
  void start(const std::string &entry = "START");
  // Indexed script flags are references identified by script name and a
  // 16-bit resource id, not literal integers (native VT 0x6002/0x6003).
  std::string referenceKey(const Node &) const;
  bool hasRead(const Node &) const;
  void markRead(const Node &);
  Value evaluate(const Node &);
  void run(size_t budget = 10000);
  void resume();
  void choose(size_t);
  const auto &programs() const { return scripts; }
  std::string save() const;
  void restore(const std::string &);
};
} // namespace SummerRadish
