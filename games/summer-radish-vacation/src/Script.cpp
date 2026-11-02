#include <SummerRadish/Script.hpp>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace SummerRadish {
std::string Node::attr(const std::string &k) const {
  auto i = attrs.find(k);
  return i == attrs.end() ? "" : i->second;
}
namespace {
std::string unescape(std::string s) {
  for (auto pair : {std::pair{"&lt;", "<"},
                    {"&gt;", ">"},
                    {"&quot;", "\""},
                    {"&apos;", "'"},
                    {"&amp;", "&"}}) {
    size_t p = 0;
    while ((p = s.find(pair.first, p)) != std::string::npos) {
      s.replace(p, std::char_traits<char>::length(pair.first), pair.second);
      p += std::char_traits<char>::length(pair.second);
    }
  }
  return s;
}
class Parser {
  const std::string &s;
  size_t p = 0;
  void fail() const {
    throw std::runtime_error("Invalid script XML at byte " + std::to_string(p));
  }
  bool at(const std::string &x) const { return s.compare(p, x.size(), x) == 0; }
  void whitespace() {
    while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p])))
      ++p;
  }
  std::string name() {
    size_t first = p;
    while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) ||
                            s[p] == '_' || s[p] == '-'))
      ++p;
    if (first == p)
      fail();
    return s.substr(first, p - first);
  }
  void trivia() {
    while (true) {
      whitespace();
      if (at("<?")) {
        auto end = s.find("?>", p + 2);
        if (end == s.npos)
          fail();
        p = end + 2;
      } else if (at("<!--")) {
        auto end = s.find("-->", p + 4);
        if (end == s.npos)
          fail();
        p = end + 3;
      } else
        break;
    }
  }
  Node node(size_t depth) {
    if (depth > 128 || p >= s.size() || s[p++] != '<')
      fail();
    Node n;
    n.tag = name();
    while (true) {
      whitespace();
      if (at("/>")) {
        p += 2;
        return n;
      }
      if (at(">")) {
        ++p;
        break;
      }
      auto key = name();
      whitespace();
      if (p >= s.size() || s[p++] != '=')
        fail();
      whitespace();
      if (p >= s.size() || (s[p] != '\'' && s[p] != '"'))
        fail();
      char quote = s[p++];
      auto end = s.find(quote, p);
      if (end == s.npos)
        fail();
      if (!n.attrs.emplace(key, unescape(s.substr(p, end - p))).second)
        fail();
      p = end + 1;
    }
    while (true) {
      if (p >= s.size())
        fail();
      if (at("</")) {
        p += 2;
        if (name() != n.tag)
          fail();
        whitespace();
        if (p >= s.size() || s[p++] != '>')
          fail();
        return n;
      }
      if (at("<!--")) {
        trivia();
        continue;
      }
      if (s[p] == '<')
        n.children.push_back(node(depth + 1));
      else {
        auto end = s.find('<', p);
        if (end == s.npos)
          fail();
        n.text += unescape(s.substr(p, end - p));
        p = end;
      }
    }
  }

public:
  explicit Parser(const std::string &s) : s(s) {}
  Node parse() {
    trivia();
    Node n = node(0);
    trivia();
    if (p != s.size())
      fail();
    return n;
  }
};
void compile(Script &s, const Node &n) {
  if (n.tag == "SCRIPT" || n.tag == "BLOCK") {
    if (!n.attr("label").empty()) {
      if (!s.labels.emplace(n.attr("label"), s.code.size()).second)
        throw std::runtime_error("Duplicate script label");
    }
    for (const auto &child : n.children)
      compile(s, child);
  } else if (n.tag == "IFBLOCK") {
    std::vector<size_t> exits;
    for (const auto &branch : n.children) {
      if (branch.tag != "IF" || branch.children.empty())
        throw std::runtime_error("Invalid IFBLOCK");
      size_t guard = s.code.size();
      Node test = branch.children.front();
      test.attrs["guard"] = "1";
      s.code.push_back({test, 0});
      for (size_t i = 1; i < branch.children.size(); ++i)
        compile(s, branch.children[i]);
      exits.push_back(s.code.size());
      s.code.push_back({Node{"_BRANCH", {}, {}, {}}, 0});
      s.code[guard].target = s.code.size();
    }
    for (auto exit : exits)
      s.code[exit].target = s.code.size();
  } else
    s.code.push_back({n, 0});
}

} // namespace
Node parseXml(const std::string &s) { return Parser(s).parse(); }
int64_t number(const Value &v) {
  if (auto n = std::get_if<int64_t>(&v))
    return *n;
  try {
    size_t p;
    auto n = std::stoll(std::get<std::string>(v), &p);
    return p == std::get<std::string>(v).size() ? n : 0;
  } catch (...) {
    return 0;
  }
}
std::string string(const Value &v) {
  if (auto s = std::get_if<std::string>(&v))
    return *s;
  return std::to_string(std::get<int64_t>(v));
}
Script::Script(const Node &root) {
  if (root.tag != "SCRIPT")
    throw std::runtime_error("Expected SCRIPT root");
  compile(*this, root);
}
void Machine::add(const std::string &name, const std::string &xml) {
  scripts.insert_or_assign(canonical(name), Script(parseXml(xml)));
}
void Machine::branch(const std::string &destination, bool call) {
  auto separator = destination.find("::");
  std::string file = separator == 0
                         ? position.script
                         : canonical(destination.substr(0, separator));
  if (file.size() > 4 && file.substr(file.size() - 4) == ".XML")
    file.resize(file.size() - 4);
  auto script = scripts.find(file);
  if (script == scripts.end())
    throw std::runtime_error("Missing script: " + destination);
  size_t pc = 0;
  if (separator != std::string::npos) {
    auto label = script->second.labels.find(destination.substr(separator + 2));
    if (label == script->second.labels.end())
      throw std::runtime_error("Missing label: " + destination);
    pc = label->second;
  }
  if (call) {
    if (calls.size() >= 256)
      throw std::runtime_error("Summer script call stack overflow");
    calls.push_back(position);
  }
  position = {file, pc};
}
void Machine::start(const std::string &entry) {
  calls.clear();
  // System globals (including GSCRIPT read flags) survive a new story.
  for (auto i = variables.begin(); i != variables.end();)
    if (i->first.empty() || i->first[0] != '$')
      i = variables.erase(i);
    else
      ++i;
  resource = {};
  ended = false;
  waiting = false;
  position = {};
  branch(entry, false);
}
std::string Machine::referenceKey(const Node &n) const {
  const auto type = n.attr("type");
  if (type == "LOCAL" || type == "GLOBAL")
    return (type == "GLOBAL" ? "$" : "@") + n.text;
  if (type == "GSCRIPT" || type == "LSCRIPT")
    return (type == "GSCRIPT" ? "$" : "@") + std::string("#SCRIPT/") +
           position.script + "/" +
           std::to_string(static_cast<uint16_t>(std::stoll(n.text)));
  throw std::runtime_error("Script expression is not an assignable reference");
}
bool Machine::hasRead(const Node &n) const {
  const auto i = variables.find(referenceKey(n));
  return i != variables.end() && number(i->second) != 0;
}
void Machine::markRead(const Node &n) {
  if (n.attr("type") != "GSCRIPT" && n.attr("type") != "LSCRIPT")
    throw std::runtime_error("Expected a script read flag");
  variables[referenceKey(n)] = int64_t(1);
}
Value Machine::evaluate(const Node &n) {
  if (n.tag == "CREATE") {
    auto t = n.attr("type");
    if (t == "STR")
      return n.text;
    if (t == "BOOL")
      return int64_t(n.text == "TRUE");
    if (t == "INT")
      return int64_t(std::stoll(n.text));
    if (t == "GLOBAL" || t == "LOCAL" || t == "GSCRIPT" || t == "LSCRIPT") {
      auto i = variables.find(referenceKey(n));
      return i == variables.end() ? Value(int64_t(0)) : i->second;
    }
    throw std::runtime_error("Unknown CREATE type: " + t);
  }
  if (n.tag != "OPERATOR" || n.children.empty())
    throw std::runtime_error("Invalid script expression");
  auto op = n.attr("type");
  if (op == "MINUS")
    return -number(evaluate(n.children[0]));
  if (n.children.size() != 2)
    throw std::runtime_error("Invalid binary expression");
  if (op == "=") {
    auto value = evaluate(n.children[1]);
    variables[referenceKey(n.children[0])] = value;
    return value;
  }
  Value a = evaluate(n.children[0]);
  // SCRIPT.mod::FUN_100125d0 evaluates both operands before dispatching
  // the logical operator; these XML expressions do not short circuit.
  Value b = evaluate(n.children[1]);
  auto x = number(a), y = number(b);
  if (op == "+") {
    if (std::holds_alternative<std::string>(a) ||
        std::holds_alternative<std::string>(b))
      return string(a) + string(b);
    return x + y;
  }
  if (op == "-")
    return x - y;
  if (op == "==")
    return int64_t(a == b);
  if (op == "!=")
    return int64_t(a != b);
  if (op == ">")
    return int64_t(x > y);
  if (op == "<")
    return int64_t(x < y);
  if (op == ">=")
    return int64_t(x >= y);
  if (op == "<=")
    return int64_t(x <= y);
  if (op == "&&")
    return int64_t(x && y);
  if (op == "||")
    return int64_t(x || y);
  throw std::runtime_error("Unknown operator: " + op);
}
void Machine::run(size_t budget) {
  for (size_t step = 0; step < budget && !ended && !waiting; ++step) {
    auto &code = scripts.at(position.script).code;
    if (position.pc >= code.size()) {
      if (calls.empty()) {
        ended = true;
        break;
      }
      position = calls.back();
      calls.pop_back();
      continue;
    }
    const auto &instruction = code[position.pc++];
    const Node &n = instruction.node;
    if (n.tag == "_BRANCH") {
      position.pc = instruction.target;
      continue;
    }
    if (n.attr("guard") == "1") {
      if (!number(evaluate(n)))
        position.pc = instruction.target;
      continue;
    }
    if (n.tag == "OPERATOR")
      evaluate(n);
    else if (n.tag == "RESOURCE")
      resource = n;
    else if (n.tag == "JUMP" || n.tag == "CALL") {
      if (n.children.empty())
        throw std::runtime_error("Empty branch");
      branch(string(evaluate(n.children[0])), n.tag == "CALL");
    } else if (n.tag == "RETURN") {
      if (calls.empty())
        ended = true;
      else {
        position = calls.back();
        calls.pop_back();
      }
    } else if (n.tag == "FUNCTION") {
      std::vector<Value> args;
      for (const auto &c : n.children)
        args.push_back(evaluate(c));
      if (function)
        function(n.attr("name"), args);
    } else if (n.tag == "GAMEEND" || n.tag == "GAMESTART") {
      if (function)
        function(n.tag, {});
    } else
      throw std::runtime_error("Unknown script node: " + n.tag);
  }
  if (!ended && !waiting)
    throw std::runtime_error("Summer script instruction budget exhausted at " +
                             position.script + ":" +
                             std::to_string(position.pc));
}
void Machine::resume() {
  waiting = false;
  run();
}
void Machine::choose(size_t n) {
  variables["@RESULT"] = int64_t(n);
  resume();
}
std::string Machine::save() const {
  std::ostringstream s;
  s << "SUMMER1 " << std::quoted(position.script) << ' ' << position.pc << ' '
    << ended << ' ' << waiting << ' ' << calls.size() << '\n';
  for (auto &c : calls)
    s << std::quoted(c.script) << ' ' << c.pc << '\n';
  s << variables.size() << '\n';
  for (auto &[k, v] : variables)
    s << std::quoted(k) << ' ' << v.index() << ' ' << std::quoted(string(v))
      << '\n';
  return s.str();
}
void Machine::restore(const std::string &data) {
  Machine next = *this;
  std::istringstream s(data);
  std::string version;
  size_t count;
  if (!(s >> version >> std::quoted(next.position.script) >> next.position.pc >>
        next.ended >> next.waiting >> count) ||
      version != "SUMMER1" || count > 256)
    throw std::runtime_error("Invalid Summer save");
  auto validate = [&](const Position &p) {
    auto i = scripts.find(p.script);
    if (i == scripts.end() || p.pc > i->second.code.size())
      throw std::runtime_error("Invalid saved script position");
  };
  validate(next.position);
  next.calls.clear();
  while (count--) {
    Position p;
    if (!(s >> std::quoted(p.script) >> p.pc))
      throw std::runtime_error("Truncated saved call stack");
    validate(p);
    next.calls.push_back(p);
  }
  if (!(s >> count) || count > 100000)
    throw std::runtime_error("Invalid saved variables");
  next.variables.clear();
  while (count--) {
    std::string k, v;
    int type;
    if (!(s >> std::quoted(k) >> type >> std::quoted(v)) ||
        (type != 0 && type != 1))
      throw std::runtime_error("Invalid saved value");
    next.variables[k] = type ? Value(v) : Value(int64_t(std::stoll(v)));
  }
  s >> std::ws;
  if (!s.eof())
    throw std::runtime_error("Trailing Summer save data");
  // The last resource belongs to the active instruction stream; recover it for
  // message/choice resumes.
  next.resource = {};
  const auto &code = scripts.at(next.position.script).code;
  for (size_t i = 0; i < next.position.pc; ++i)
    if (code[i].node.tag == "RESOURCE")
      next.resource = code[i].node;
  *this = std::move(next);
}
} // namespace SummerRadish
