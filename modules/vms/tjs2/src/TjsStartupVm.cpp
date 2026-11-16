// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Bounded TJS2 *source subset*, not a substitute for upstream TJS2 bytecode.
#include <Kirikiri/TjsStartupVm.hpp>
#include <Kirikiri/TjsBootstrap.hpp>
#include <Kirikiri/KirikiriAssets.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Kotonoha::Providers {
namespace {
struct Object;
using ObjectPtr = std::shared_ptr<Object>;
using Data = std::variant<std::monostate, bool, double, std::string, ObjectPtr>;
struct Value {
  Data data;
  Value() = default;
  explicit Value(bool x) : data(x) {}
  explicit Value(double x) : data(x) {}
  explicit Value(std::string x) : data(std::move(x)) {}
  explicit Value(ObjectPtr x) : data(std::move(x)) {}
  bool truthy() const {
    if (auto b = std::get_if<bool>(&data)) return *b;
    if (auto n = std::get_if<double>(&data)) return *n != 0.0;
    if (auto s = std::get_if<std::string>(&data)) return !s->empty();
    if (auto object = std::get_if<ObjectPtr>(&data)) return bool(*object);
    return false;
  }
  std::string string() const {
    if (auto s = std::get_if<std::string>(&data)) return *s;
    if (std::holds_alternative<ObjectPtr>(data)) return "[Dictionary]";
    if (auto b = std::get_if<bool>(&data)) return *b ? "true" : "false";
    if (auto n = std::get_if<double>(&data)) {
      auto s = std::to_string(*n);
      if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
      }
      return s;
    }
    return {};
  }
  double number() const {
    if (auto n = std::get_if<double>(&data)) return *n;
    if (auto b = std::get_if<bool>(&data)) return *b ? 1.0 : 0.0;
    if (auto s = std::get_if<std::string>(&data)) {
      char *end = nullptr;
      const double x = std::strtod(s->c_str(), &end);
      if (end != s->c_str() && end && *end == '\0')
        return x;
    }
    throw std::runtime_error("TJS2 subset: numeric operand expected");
  }
};

// Minimal heap-backed TJS Dictionary value. Copies share member state, but
// native KiriKiri Window/Layer objects and prototype dispatch are excluded.
struct Object {
  std::unordered_map<std::string, Value> members;
};

struct Token {
  enum Kind { Identifier, Number, String, Symbol, End } kind = End;
  std::string text;
  std::size_t offset = 0;
};

bool nameBegin(unsigned char c) {
  return std::isalpha(c) != 0 || c >= 0x80 || c == '_' || c == '$'; // UTF-8 names
}
bool nameChar(unsigned char c) {
  return nameBegin(c) || std::isdigit(c) != 0;
}

std::vector<Token> lex(const std::string &source) {
  if (source.size() > 256u * 1024u)
    throw std::runtime_error("TJS2 subset: script size limit exceeded");
  std::vector<Token> tokens;
  const auto add = [&](Token::Kind type, std::string value, std::size_t at) {
    if (tokens.size() >= 32768)
      throw std::runtime_error("TJS2 subset: token limit exceeded");
    tokens.push_back({type, std::move(value), at});
  };
  for (std::size_t i = 0; i < source.size();) {
    const unsigned char c = static_cast<unsigned char>(source[i]);
    if (std::isspace(c)) { ++i; continue; }
    if (c == '/' && i + 1 < source.size() && source[i+1] == '/') {
      i += 2;
      while (i < source.size() && source[i] != '\n' && source[i] != '\r')
        ++i;
      continue;
    }
    if (c == '/' && i + 1 < source.size() && source[i+1] == '*') {
      const auto end = source.find("*/", i + 2);
      if (end == std::string::npos)
        throw std::runtime_error("TJS2 subset: unterminated block comment");
      i = end + 2;
      continue;
    }
    const auto begin = i;
    if (nameBegin(c)) {
      ++i;
      while (i < source.size() &&
             nameChar(static_cast<unsigned char>(source[i]))) ++i;
      add(Token::Identifier, source.substr(begin, i - begin), begin);
      continue;
    }
    if (std::isdigit(c)) {
      if (c == '0' && i + 2 < source.size() &&
          (source[i + 1] == 'x' || source[i + 1] == 'X')) {
        i += 2;
        const auto firstDigit = i;
        while (i < source.size() &&
               std::isxdigit(static_cast<unsigned char>(source[i])))
          ++i;
        if (firstDigit == i)
          throw std::runtime_error("TJS2 subset: invalid hex literal");
      } else {
        ++i;
        while (i < source.size() &&
               (std::isdigit(static_cast<unsigned char>(source[i])) ||
                source[i] == '.'))
          ++i;
      }
      add(Token::Number, source.substr(begin, i - begin), begin);
      continue;
    }
    if (c == '\'' || c == '"') {
      ++i;
      std::string value;
      bool closed = false;
      while (i < source.size()) {
        const char v = source[i++];
        if (v == c) { closed = true; break; }
        if (v == '\\' && i < source.size()) {
          const char escaped = source[i++];
          switch (escaped) {
            case 'n': value += '\n'; break;
            case 'r': value += '\r'; break;
            case 't': value += '\t'; break;
            case '\\': case '\'': case '"': value += escaped; break;
            default: value += '\\'; value += escaped; break;
          }
        } else value += v;
      }
      if (!closed)
        throw std::runtime_error("TJS2 subset: unterminated string literal");
      add(Token::String, std::move(value), begin);
      continue;
    }
    if (i + 1 < source.size()) {
      const auto two = source.substr(i, 2);
      if (two == "==" || two == "!=" || two == "<=" ||
          two == ">=" || two == "&&" || two == "||" ||
          two == "+=" || two == "-=" ||
          two == "++" || two == "--") {
        add(Token::Symbol, two, i);
        i += 2;
        continue;
      }
    }
    if (std::string("{}();,.=+-*/!<>").find(source[i]) ==
        std::string::npos)
      throw std::runtime_error("TJS2 subset: unsupported lexical token at byte " +
                               std::to_string(i));
    add(Token::Symbol, source.substr(i++, 1), begin);
  }
  add(Token::End, {}, source.size());
  return tokens;
}

std::string safeName(std::string id) {
  if (id.empty() || id.size() > 1024 ||
      id.front() == '/' || id.find("://") != std::string::npos ||
      id.find(':') != std::string::npos)
    throw std::runtime_error("TJS2 subset: unsafe storage ID");
  std::replace(id.begin(), id.end(), '\\', '/');
  while (id.compare(0, 2, "./") == 0)
    id.erase(0, 2);
  for (std::size_t p = 0; p < id.size();) {
    const auto slash = id.find('/', p);
    const auto part = id.substr(p, slash == std::string::npos
                                     ? slash : slash-p);
    if (part == "..")
      throw std::runtime_error("TJS2 subset: storage path traversal");
    if (slash == std::string::npos) break;
    p = slash + 1;
  }
  return id;
}

bool endsWith(const std::string &text, std::string suffix) {
  if (text.size() < suffix.size()) return false;
  const auto begin = text.end() - static_cast<std::ptrdiff_t>(suffix.size());
  return std::equal(begin, text.end(), suffix.begin(), suffix.end(),
                    [](char a, char b) {
                      return std::tolower(static_cast<unsigned char>(a)) ==
                             std::tolower(static_cast<unsigned char>(b));
                    });
}

// Functions are represented by bounded source slices; the parser reuses its
// ordinary expression evaluator on calls, but cannot execute native TJS objects.
struct SourceFunction {
  std::string name;
  std::string source;
  std::string body;
  std::vector<std::string> parameters;
};
struct FunctionReturn {
  Value value;
};

class Parser;
class Interpreter {
public:
  explicit Interpreter(const KirikiriAssets &source, std::size_t budget)
      : assets(source), remaining(budget) {}
  const KirikiriAssets &assets;
  TjsStartupResult result;
  std::unordered_map<std::string, Value> variables;
  std::unordered_map<std::string, SourceFunction> functions;
  std::vector<std::unordered_map<std::string, Value>> frames;
  std::vector<std::string> active;
  std::size_t remaining;
  std::size_t functionDepth = 0;

  Value get(const std::string &name) const {
    for (auto frame = frames.rbegin(); frame != frames.rend(); ++frame) {
      if (auto it = frame->find(name); it != frame->end())
        return it->second;
    }
    if (auto it = variables.find(name); it != variables.end())
      return it->second;
    throw std::runtime_error("TJS2 subset: unresolved variable: " + name);
  }
  void set(const std::string &name, Value value, bool declaration) {
    if (declaration) {
      if (!frames.empty())
        frames.back()[name] = std::move(value);
      else
        variables[name] = std::move(value);
      return;
    }
    for (auto frame = frames.rbegin(); frame != frames.rend(); ++frame) {
      if (auto it = frame->find(name); it != frame->end()) {
        it->second = std::move(value);
        return;
      }
    }
    variables[name] = std::move(value);
  }

  void tick() {
    if (!remaining--)
      throw std::runtime_error("TJS2 subset: instruction budget exhausted");
    ++result.statements;
  }
  std::string resolve(const std::string &requested,
                      const std::string &current) const {
    const auto normalized = safeName(requested);
    if (assets.contains(normalized)) return normalized;
    const auto slash = current.find_last_of('/');
    if (slash != std::string::npos) {
      const auto near = safeName(current.substr(0, slash + 1) + normalized);
      if (assets.contains(near)) return near;
    }
    return normalized;
  }
  void runFile(std::string id, std::size_t depth);
  Value call(const std::string &name, const std::vector<Value> &params,
             const std::string &current, bool activeFlag);
};

class Parser {
  Interpreter &machine;
  std::string sourceName;
  std::string sourceCode;
  std::vector<Token> tokens;
  std::size_t pc = 0;

  const Token &look(std::size_t offset = 0) const {
    return tokens[std::min(pc + offset, tokens.size() - 1)];
  }
  bool match(const std::string &text) {
    if (look().text != text) return false;
    ++pc;
    return true;
  }
  void expect(const std::string &text) {
    if (!match(text))
      throw std::runtime_error("TJS2 subset: expected '" + text +
                               "' near byte " +
                               std::to_string(look().offset));
  }
  std::string identifier() {
    if (look().kind != Token::Identifier)
      throw std::runtime_error("TJS2 subset: identifier expected");
    return tokens[pc++].text;
  }
  Value primary(bool active) {
    if (match("(")) {
      auto v = expression(active);
      expect(")");
      return v;
    }
    if (look().kind == Token::String)
      return Value(tokens[pc++].text);
    if (look().kind == Token::Number) {
      const auto spelling = tokens[pc++].text;
      char *end = nullptr;
      const double n = spelling.size() > 2 && spelling[0] == '0' &&
                       (spelling[1] == 'x' || spelling[1] == 'X')
                           ? static_cast<double>(
                                 std::strtoull(spelling.c_str(), &end, 16))
                           : std::strtod(spelling.c_str(), &end);
      if (end == spelling.c_str() || *end != '\0' || !std::isfinite(n))
        throw std::runtime_error("TJS2 subset: invalid number");
      return Value(n);
    }
    if (look().kind != Token::Identifier)
      throw std::runtime_error("TJS2 subset: unexpected expression token near " +
                               std::to_string(look().offset));
    auto name = identifier();
    if (name == "true") return Value(true);
    if (name == "false") return Value(false);
    if (name == "null" || name == "void") return {};
    while (match(".")) name += "." + identifier();
    if (match("(")) {
      std::vector<Value> args;
      if (!match(")")) {
        do {
          if (args.size() >= 8)
            throw std::runtime_error("TJS2 subset: too many function arguments");
          args.push_back(expression(active));
        } while (match(","));
        expect(")");
      }
      return machine.call(name, args, sourceName, active);
    }
    return active ? machine.get(name) : Value();
  }
  Value unary(bool active) {
    if (match("!")) {
      const auto rhs = unary(active);
      return active ? Value(!rhs.truthy()) : Value();
    }
    if (match("-")) {
      const auto rhs = unary(active);
      return active ? Value(-rhs.number()) : Value();
    }
    return primary(active);
  }
  Value product(bool active) {
    auto lhs = unary(active);
    while (look().text == "*" || look().text == "/") {
      const auto op = tokens[pc++].text;
      const auto rhs = unary(active);
      if (!active) continue;
      const auto denominator = rhs.number();
      if (op == "/" && denominator == 0)
        throw std::runtime_error("TJS2 subset: division by zero");
      lhs = Value(op == "*" ? lhs.number()*denominator
                            : lhs.number()/denominator);
    }
    return lhs;
  }
  Value sum(bool active) {
    auto lhs = product(active);
    while (look().text == "+" || look().text == "-") {
      const auto op = tokens[pc++].text;
      const auto rhs = product(active);
      if (!active) continue;
      if (op == "+" && (std::holds_alternative<std::string>(lhs.data) ||
                        std::holds_alternative<std::string>(rhs.data)))
        lhs = Value(lhs.string() + rhs.string());
      else
        lhs = Value(op == "+" ? lhs.number()+rhs.number()
                              : lhs.number()-rhs.number());
    }
    return lhs;
  }
  Value compare(bool active) {
    auto lhs = sum(active);
    while (look().text == "<" || look().text == "<=" ||
           look().text == ">" || look().text == ">=") {
      const auto op = tokens[pc++].text;
      const auto rhs = sum(active);
      if (active) {
        const auto a = lhs.number(), b = rhs.number();
        lhs = Value(op == "<" ? a < b : op == "<=" ? a <= b :
                    op == ">" ? a > b : a >= b);
      }
    }
    return lhs;
  }
  Value equality(bool active) {
    auto lhs = compare(active);
    while (look().text == "==" || look().text == "!=") {
      const auto op = tokens[pc++].text;
      const auto rhs = compare(active);
      if (active) {
        const bool equal = lhs.data.index() == rhs.data.index()
            ? lhs.data == rhs.data : lhs.string() == rhs.string();
        lhs = Value(op == "==" ? equal : !equal);
      }
    }
    return lhs;
  }
  Value conjunction(bool active) {
    auto lhs = equality(active);
    while (match("&&")) {
      const bool go = active && lhs.truthy();
      auto rhs = equality(go);
      if (active) lhs = Value(go && rhs.truthy());
    }
    return lhs;
  }
  Value disjunction(bool active) {
    auto lhs = conjunction(active);
    while (match("||")) {
      const bool go = active && !lhs.truthy();
      auto rhs = conjunction(go);
      if (active) lhs = Value(!go || rhs.truthy());
    }
    return lhs;
  }
  Value expression(bool active) { return disjunction(active); }

  void statement(bool active) {
    machine.tick();
    if (match(";")) return;
    if (match("return")) {
      const auto value = look().text == ";" ? Value() : expression(active);
      expect(";");
      if (active) {
        if (machine.functionDepth == 0)
          throw std::runtime_error("TJS2 subset: return outside function");
        throw FunctionReturn{value};
      }
      return;
    }
    if (match("function")) {
      const auto functionName = identifier();
      expect("(");
      std::vector<std::string> parameters;
      if (!match(")")) {
        do {
          if (parameters.size() >= 16)
            throw std::runtime_error("TJS2 subset: too many parameters");
          const auto name = identifier();
          if (std::find(parameters.begin(), parameters.end(), name) !=
              parameters.end())
            throw std::runtime_error("TJS2 subset: duplicate parameter name");
          parameters.push_back(name);
        } while (match(","));
        expect(")");
      }
      if (look().text != "{")
        throw std::runtime_error("TJS2 subset: function body expected");
      const auto start = look().offset;
      std::size_t braces = 0;
      do {
        if (look().kind == Token::End)
          throw std::runtime_error("TJS2 subset: unterminated function body");
        if (look().text == "{") ++braces;
        else if (look().text == "}") --braces;
        ++pc;
      } while (braces);
      if (active) {
        if (machine.functions.size() >= 256)
          throw std::runtime_error("TJS2 subset: function count limit exceeded");
        const auto finish = tokens[pc - 1].offset + 1;
        machine.functions[functionName] = SourceFunction{
            functionName, sourceName, sourceCode.substr(start, finish - start),
            std::move(parameters)};
      }
      return;
    }
    if (match("{")) {
      while (!match("}")) {
        if (look().kind == Token::End)
          throw std::runtime_error("TJS2 subset: unterminated statement block");
        statement(active);
      }
      return;
    }
    if (match("if")) {
      expect("(");
      const auto test = expression(active);
      expect(")");
      statement(active && test.truthy());
      if (match("else")) statement(active && !test.truthy());
      return;
    }
    if (match("while")) {
      expect("(");
      const auto checkAt = pc;
      // First iteration must still parse a disabled body to find its end.
      auto test = expression(active);
      expect(")");
      const auto bodyAt = pc;
      statement(active && test.truthy());
      const auto pastBody = pc;
      if (!active) return;
      unsigned iterations = test.truthy() ? 1 : 0;
      while (test.truthy()) {
        if (++iterations > 256)
          throw std::runtime_error("TJS2 subset: loop iteration cap exceeded");
        pc = checkAt;
        test = expression(true);
        expect(")");
        if (!test.truthy()) break;
        pc = bodyAt;
        statement(true);
        if (pc != pastBody)
          throw std::runtime_error("TJS2 subset: unstable loop body");
      }
      pc = pastBody;
      return;
    }
    const bool declaration = match("var") || match("const");
    if (declaration) {
      do {
        const auto name = identifier();
        const auto value = match("=") ? expression(active) : Value();
        if (active) machine.set(name, value, true);
      } while (match(","));
      expect(";");
      return;
    }

    // TJS2 bootstrap assignments include System.title = ... and local vars.
    // Do not accept arbitrary expressions as an ignorable side effect.
    if (look().kind == Token::Identifier) {
      const auto saved = pc;
      auto name = identifier();
      while (match(".")) name += "." + identifier();
      if (match("++") || match("--")) {
        const auto op = tokens[pc-1].text;
        expect(";");
        if (active) {
          machine.set(name, Value(machine.get(name).number() +
                                  (op == "++" ? 1.0 : -1.0)), false);
        }
        return;
      }
      if (match("=") || match("+=")) {
        const auto op = tokens[pc-1].text;
        auto value = expression(active);
        expect(";");
        if (active) {
          if (op == "+=") {
            const auto before = machine.get(name);
            if (std::holds_alternative<std::string>(before.data) ||
                std::holds_alternative<std::string>(value.data))
              value = Value(before.string() + value.string());
            else
              value = Value(before.number() + value.number());
          }
          machine.set(name, value, false);
          if (name == "System.title") machine.result.title = value.string();
        }
        return;
      }
      pc = saved;
    }
    (void)expression(active);
    expect(";");
  }

public:
  Parser(Interpreter &vm, std::string file, std::string text)
      : machine(vm), sourceName(std::move(file)),
        sourceCode(std::move(text)), tokens(lex(sourceCode)) {}
  void run() {
    while (look().kind != Token::End) statement(true);
  }
};

Value Interpreter::call(const std::string &name,
                        const std::vector<Value> &params,
                        const std::string &current, bool activeFlag) {
  if (!activeFlag) return {};

  if (auto it = functions.find(name); it != functions.end()) {
    // Copy the descriptor: nested declarations can rehash the function table.
    const SourceFunction function = it->second;
    if (params.size() != function.parameters.size())
      throw std::runtime_error("TJS2 subset: argument count mismatch: " + name);
    if (functionDepth >= 16)
      throw std::runtime_error("TJS2 subset: function recursion limit exceeded");
    std::unordered_map<std::string, Value> frame;
    for (std::size_t index = 0; index < params.size(); ++index)
      frame.emplace(function.parameters[index], params[index]);
    frames.push_back(std::move(frame));
    ++functionDepth;
    try {
      Parser parser(*this, function.source, function.body);
      parser.run();
    } catch (const FunctionReturn &returned) {
      --functionDepth;
      frames.pop_back();
      return returned.value;
    } catch (...) {
      --functionDepth;
      frames.pop_back();
      throw;
    }
    --functionDepth;
    frames.pop_back();
    return {};
  }

  if (params.size() != 1)
    throw std::runtime_error("TJS2 subset: exactly one argument required for " +
                             name);
  const auto requested = params.front().string();
  if (name == "Storages.isExistentStorage") {
    const auto id = resolve(requested, current);
    return Value(assets.contains(id));
  }
  if (name == "Scripts.execStorage" || name == "Scripts.evalStorage") {
    const auto id = resolve(requested, current);
    runFile(id, active.size() + 1);
    return {};
  }
  if (name == "Plugins.link") {
    const auto id = safeName(requested);
    if (std::find(result.requestedPlugins.begin(),
                  result.requestedPlugins.end(), id) ==
        result.requestedPlugins.end())
      result.requestedPlugins.push_back(id);
    result.requiresNativeRuntime = true;
    return Value(false); // No native plugin was loaded.
  }
  throw std::runtime_error("TJS2 subset: native call not implemented: " + name);
}

void Interpreter::runFile(std::string id, std::size_t depth) {
  id = safeName(std::move(id));
  if (depth > 16)
    throw std::runtime_error("TJS2 subset: script call depth exceeded");
  if (std::find(active.begin(), active.end(), id) != active.end())
    throw std::runtime_error("TJS2 subset: cyclic Scripts.execStorage");
  if (!assets.contains(id)) {
    if (std::find(result.missing.begin(), result.missing.end(), id) ==
        result.missing.end())
      result.missing.push_back(id);
    throw std::runtime_error("TJS2 subset: missing storage " + id);
  }
  if (endsWith(id, ".ks") || endsWith(id, ".ks.scn")) {
    if (result.kagEntry.empty()) result.kagEntry = id;
    result.requiresNativeRuntime = true; // KAG is not TJS2.
    return;
  }
  if (!endsWith(id, ".tjs"))
    throw std::runtime_error("TJS2 subset: storage is not source TJS2: " + id);
  active.push_back(id);
  result.visitedScripts.push_back(id);
  try {
    Parser parser(*this, id, assets.loadTextUtf8(id, "CP932"));
    parser.run();
  } catch (...) {
    active.pop_back();
    throw;
  }
  active.pop_back();
}
} // namespace

TjsStartupResult runTjsStartupSubset(const KirikiriAssets &assets,
                                    std::size_t instructionBudget) {
  Interpreter vm(assets, instructionBudget);
  const auto inspected = inspectTjsBootstrap(assets);
  if (inspected.entry.empty()) {
    vm.result.error = "TJS2 subset: startup.tjs not found";
    return vm.result;
  }
  try {
    vm.runFile(inspected.entry, 0);
    vm.result.subsetCompleted = true;
  } catch (const std::exception &error) {
    vm.result.error = error.what();
  }
  return vm.result;
}

} // namespace Kotonoha::Providers
