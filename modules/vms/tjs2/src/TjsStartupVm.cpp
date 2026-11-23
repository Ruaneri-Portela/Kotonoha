// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Bounded TJS2 *source subset*, not a substitute for upstream TJS2 bytecode.
#include <Kirikiri/KirikiriAssets.hpp>
#include <Kirikiri/TjsBootstrap.hpp>
#include <Kirikiri/TjsStartupVm.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
    if (auto b = std::get_if<bool>(&data))
      return *b;
    if (auto n = std::get_if<double>(&data))
      return *n != 0.0;
    if (auto s = std::get_if<std::string>(&data))
      return !s->empty();
    if (auto object = std::get_if<ObjectPtr>(&data))
      return bool(*object);
    return false;
  }
  std::string string() const {
    if (auto s = std::get_if<std::string>(&data))
      return *s;
    if (std::holds_alternative<ObjectPtr>(data))
      return "[Dictionary]";
    if (auto b = std::get_if<bool>(&data))
      return *b ? "true" : "false";
    if (auto n = std::get_if<double>(&data)) {
      auto s = std::to_string(*n);
      if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0')
          s.pop_back();
        if (!s.empty() && s.back() == '.')
          s.pop_back();
      }
      return s;
    }
    return {};
  }
  double number() const {
    if (auto n = std::get_if<double>(&data))
      return *n;
    if (auto b = std::get_if<bool>(&data))
      return *b ? 1.0 : 0.0;
    if (auto s = std::get_if<std::string>(&data)) {
      char *end = nullptr;
      const double x = std::strtod(s->c_str(), &end);
      if (end != s->c_str() && end && *end == '\0')
        return x;
    }
    throw std::runtime_error("TJS2 subset: numeric operand expected");
  }
};

// Small retained object graph. A Layer is always attached to an existing
// Window or parent Layer, and is projected onto the Kotonoha render viewport.
// This is not a replacement for KiriKiri's platform Window/Layer classes.
struct Object {
  enum class Kind {
    Dictionary,
    Array,
    Window,
    Layer,
    ScriptClass
  } kind = Kind::Dictionary;
  std::string className;
  std::unordered_map<std::string, Value> members;
  std::weak_ptr<Object> parent;
  std::vector<Value> elements; // bounded source TJS2 Array values
  std::size_t layerId = 0;
  std::vector<TjsUiText> texts;
  std::vector<TjsUiRect> rectangles;
};

struct Token {
  enum Kind { Identifier, Number, String, Symbol, End } kind = End;
  std::string text;
  std::size_t offset = 0;
};

bool nameBegin(unsigned char c) {
  return std::isalpha(c) != 0 || c >= 0x80 || c == '_' ||
         c == '$'; // UTF-8 names
}
bool nameChar(unsigned char c) { return nameBegin(c) || std::isdigit(c) != 0; }

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
    if (std::isspace(c)) {
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
      i += 2;
      while (i < source.size() && source[i] != '\n' && source[i] != '\r')
        ++i;
      continue;
    }
    if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
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
             nameChar(static_cast<unsigned char>(source[i])))
        ++i;
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
        if (v == c) {
          closed = true;
          break;
        }
        if (v == '\\' && i < source.size()) {
          const char escaped = source[i++];
          switch (escaped) {
          case 'n':
            value += '\n';
            break;
          case 'r':
            value += '\r';
            break;
          case 't':
            value += '\t';
            break;
          case '\\':
          case '\'':
          case '"':
            value += escaped;
            break;
          default:
            value += '\\';
            value += escaped;
            break;
          }
        } else
          value += v;
      }
      if (!closed)
        throw std::runtime_error("TJS2 subset: unterminated string literal");
      add(Token::String, std::move(value), begin);
      continue;
    }
    if (i + 1 < source.size()) {
      const auto two = source.substr(i, 2);
      if (two == "==" || two == "!=" || two == "<=" || two == ">=" ||
          two == "&&" || two == "||" || two == "+=" || two == "-=" ||
          two == "++" || two == "--") {
        add(Token::Symbol, two, i);
        i += 2;
        continue;
      }
    }
    if (std::string("{}();,.=+-*/%!?<>:[]").find(source[i]) ==
        std::string::npos)
      throw std::runtime_error(
          "TJS2 subset: unsupported lexical token at byte " +
          std::to_string(i));
    add(Token::Symbol, source.substr(i++, 1), begin);
  }
  add(Token::End, {}, source.size());
  return tokens;
}

std::string safeName(std::string id) {
  if (id.empty() || id.size() > 1024 || id.front() == '/' ||
      id.find("://") != std::string::npos || id.find(':') != std::string::npos)
    throw std::runtime_error("TJS2 subset: unsafe storage ID");
  std::replace(id.begin(), id.end(), '\\', '/');
  while (id.compare(0, 2, "./") == 0)
    id.erase(0, 2);
  for (std::size_t p = 0; p < id.size();) {
    const auto slash = id.find('/', p);
    const auto part =
        id.substr(p, slash == std::string::npos ? slash : slash - p);
    if (part == "..")
      throw std::runtime_error("TJS2 subset: storage path traversal");
    if (slash == std::string::npos)
      break;
    p = slash + 1;
  }
  return id;
}

bool endsWith(const std::string &text, std::string suffix) {
  if (text.size() < suffix.size())
    return false;
  const auto begin = text.end() - static_cast<std::ptrdiff_t>(suffix.size());
  return std::equal(begin, text.end(), suffix.begin(), suffix.end(),
                    [](char a, char b) {
                      return std::tolower(static_cast<unsigned char>(a)) ==
                             std::tolower(static_cast<unsigned char>(b));
                    });
}

// Functions are represented by bounded source slices; the parser reuses its
// ordinary expression evaluator on calls, but cannot execute native TJS
// objects.
struct SourceFunction {
  std::string name;
  std::string source;
  std::string body;
  std::vector<std::string> parameters;
};
struct FunctionReturn {
  Value value;
};

struct SourceClass {
  std::string name;
  std::string base;
  std::unordered_map<std::string, SourceFunction> methods;
};

struct BoundMethod {
  ObjectPtr receiver;
  SourceFunction function;
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
  std::unordered_map<std::string, SourceClass> classes;
  std::unordered_map<std::string, BoundMethod> boundMethods;
  std::vector<ObjectPtr> instances;
  std::vector<std::unordered_map<std::string, Value>> frames;
  std::vector<std::string> active;
  std::vector<ObjectPtr> windows, layers;
  std::size_t nextLayerId = 1;
  std::size_t nextAnonymousFunction = 1;
  std::size_t nextBoundMethod = 1;
  std::size_t remaining;
  std::size_t functionDepth = 0;

  Value construct(const std::string &type, const std::vector<Value> &args);
  Value invokeSource(const SourceFunction &function,
                     const std::vector<Value> &args,
                     const ObjectPtr &receiver = {});
  bool hasCallable(const std::string &name) const {
    return functions.count(name) != 0 || boundMethods.count(name) != 0;
  }
  std::size_t callableArity(const std::string &name) const {
    if (const auto it = functions.find(name); it != functions.end())
      return it->second.parameters.size();
    if (const auto it = boundMethods.find(name); it != boundMethods.end())
      return it->second.function.parameters.size();
    throw std::runtime_error("TJS2 subset: callback not found: " + name);
  }
  void snapshotLayers();
  static std::size_t checkedIndex(const Value &value) {
    const auto n = value.number();
    if (!std::isfinite(n) || n < 0 || n >= 4096 || std::floor(n) != n)
      throw std::runtime_error("TJS2 subset: invalid Array index");
    return static_cast<std::size_t>(n);
  }
  Value indexed(const Value &array, const Value &index) const {
    const auto object = std::get_if<ObjectPtr>(&array.data);
    if (!object || !*object || (*object)->kind != Object::Kind::Array)
      throw std::runtime_error("TJS2 subset: indexed value is not Array");
    const auto n = checkedIndex(index);
    return n < (*object)->elements.size() ? (*object)->elements[n] : Value();
  }
  void setIndex(const std::string &name, const Value &index, Value value) {
    const auto array = get(name);
    const auto object = std::get_if<ObjectPtr>(&array.data);
    if (!object || !*object || (*object)->kind != Object::Kind::Array)
      throw std::runtime_error("TJS2 subset: indexed target is not Array");
    const auto n = checkedIndex(index);
    if ((*object)->elements.size() <= n)
      (*object)->elements.resize(n + 1);
    (*object)->elements[n] = std::move(value);
  }

  Value get(const std::string &name) const {
    // Exact qualified built-ins like System.title remain supported.
    for (auto frame = frames.rbegin(); frame != frames.rend(); ++frame) {
      if (auto it = frame->find(name); it != frame->end())
        return it->second;
    }
    if (auto it = variables.find(name); it != variables.end())
      return it->second;
    const auto dot = name.find('.');
    if (dot == std::string::npos)
      throw std::runtime_error("TJS2 subset: unresolved variable: " + name);
    Value current = get(name.substr(0, dot));
    for (auto first = dot + 1; first < name.size();) {
      const auto next = name.find('.', first);
      const auto key =
          name.substr(first, next == std::string::npos ? next : next - first);
      const auto ptr = std::get_if<ObjectPtr>(&current.data);
      if (!ptr || !*ptr)
        throw std::runtime_error(
            "TJS2 subset: property read on non-Dictionary");
      if ((*ptr)->kind == Object::Kind::Array && key == "length") {
        current = Value(static_cast<double>((*ptr)->elements.size()));
      } else {
        const auto found = (*ptr)->members.find(key);
        if (found == (*ptr)->members.end())
          throw std::runtime_error(
              "TJS2 subset: missing Dictionary property: " + key);
        current = found->second;
      }
      if (next == std::string::npos)
        break;
      first = next + 1;
    }
    return current;
  }
  void set(const std::string &name, Value value, bool declaration) {
    if (declaration) {
      if (!frames.empty())
        frames.back()[name] = std::move(value);
      else
        variables[name] = std::move(value);
      return;
    }
    const auto dot = name.rfind('.');
    if (dot != std::string::npos && name != "System.title") {
      const auto rootName = name.substr(0, dot);
      // If this refers to an existing Dictionary, update that shared object;
      // otherwise preserve legacy special-qualified bootstrap assignments.
      try {
        const auto owner = get(rootName);
        if (const auto obj = std::get_if<ObjectPtr>(&owner.data); obj && *obj) {
          const auto member = name.substr(dot + 1);
          if ((*obj)->members.count(member) == 0 &&
              (*obj)->members.size() >= 1024)
            throw std::runtime_error(
                "TJS2 subset: Dictionary member limit exceeded");
          (*obj)->members[member] = std::move(value);
          return;
        }
        throw std::runtime_error(
            "TJS2 subset: property assignment on non-Dictionary");
      } catch (const std::runtime_error &error) {
        const auto message = std::string(error.what());
        if (message.find("unresolved variable") == std::string::npos)
          throw;
      }
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
    if (assets.contains(normalized))
      return normalized;
    const auto slash = current.find_last_of('/');
    if (slash != std::string::npos) {
      const auto near = safeName(current.substr(0, slash + 1) + normalized);
      if (assets.contains(near))
        return near;
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
    if (look().text != text)
      return false;
    ++pc;
    return true;
  }
  void expect(const std::string &text) {
    if (!match(text))
      throw std::runtime_error("TJS2 subset: expected '" + text +
                               "' near byte " + std::to_string(look().offset));
  }
  std::string identifier() {
    if (look().kind != Token::Identifier)
      throw std::runtime_error("TJS2 subset: identifier expected");
    return tokens[pc++].text;
  }
  Value primary(bool active) {
    if (match("function")) {
      // Function expressions are useful for Layer.onClick/onMouseDown.
      // These functions share global VM state; lexical closure capture is
      // explicitly not implemented by the source subset.
      std::string functionName;
      if (look().kind == Token::Identifier && look(1).text == "(")
        functionName = identifier();
      expect("(");
      std::vector<std::string> parameters;
      if (!match(")")) {
        do {
          if (parameters.size() >= 8)
            throw std::runtime_error("TJS2 UI: too many handler parameters");
          const auto parameter = identifier();
          if (std::find(parameters.begin(), parameters.end(), parameter) !=
              parameters.end())
            throw std::runtime_error("TJS2 UI: repeated handler parameter");
          parameters.push_back(parameter);
        } while (match(","));
        expect(")");
      }
      if (look().text != "{")
        throw std::runtime_error("TJS2 UI: function expression body expected");
      const auto start = look().offset;
      std::size_t braces = 0;
      do {
        if (look().kind == Token::End)
          throw std::runtime_error("TJS2 UI: unterminated event handler");
        if (look().text == "{")
          ++braces;
        else if (look().text == "}")
          --braces;
        ++pc;
      } while (braces);
      if (!active)
        return {};
      if (machine.functions.size() >= 256)
        throw std::runtime_error("TJS2 UI: handler count limit exceeded");
      if (functionName.empty())
        functionName =
            "__tjs_handler_" + std::to_string(machine.nextAnonymousFunction++);
      const auto finish = tokens[pc - 1].offset + 1;
      machine.functions[functionName] = SourceFunction{
          functionName, sourceName, sourceCode.substr(start, finish - start),
          std::move(parameters)};
      return Value(functionName);
    }
    if (match("[")) {
      auto array = std::make_shared<Object>();
      array->kind = Object::Kind::Array;
      if (!match("]")) {
        do {
          if (array->elements.size() >= 4096)
            throw std::runtime_error("TJS2 subset: Array limit exceeded");
          auto item = expression(active);
          if (active)
            array->elements.push_back(std::move(item));
        } while (match(","));
        expect("]");
      }
      return active ? Value(std::move(array)) : Value();
    }
    if (match("new")) {
      const auto type = identifier();
      std::vector<Value> args;
      if (match("(") && !match(")")) {
        do {
          if (args.size() >= 8)
            throw std::runtime_error(
                "TJS2 subset: too many constructor arguments");
          args.push_back(expression(active));
        } while (match(","));
        expect(")");
      }
      return active ? machine.construct(type, args) : Value();
    }
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
      const double n =
          spelling.size() > 2 && spelling[0] == '0' &&
                  (spelling[1] == 'x' || spelling[1] == 'X')
              ? static_cast<double>(std::strtoull(spelling.c_str(), &end, 16))
              : std::strtod(spelling.c_str(), &end);
      if (end == spelling.c_str() || *end != '\0' || !std::isfinite(n))
        throw std::runtime_error("TJS2 subset: invalid number");
      return Value(n);
    }
    if (look().kind != Token::Identifier)
      throw std::runtime_error(
          "TJS2 subset: unexpected expression token near " +
          std::to_string(look().offset));
    auto name = identifier();
    if (name == "true")
      return Value(true);
    if (name == "false")
      return Value(false);
    if (name == "null" || name == "void")
      return {};
    while (match("."))
      name += "." + identifier();
    if (match("[")) {
      Value result = active ? machine.get(name) : Value();
      do {
        const auto index = expression(active);
        expect("]");
        if (active)
          result = machine.indexed(result, index);
      } while (match("["));
      return result;
    }
    if (match("(")) {
      std::vector<Value> args;
      if (!match(")")) {
        do {
          if (args.size() >= 8)
            throw std::runtime_error(
                "TJS2 subset: too many function arguments");
          args.push_back(expression(active));
        } while (match(","));
        expect(")");
      }
      return machine.call(name, args, sourceName, active);
    }
    // Named source functions may be assigned as Layer.onClick handlers.
    if (active && machine.functions.count(name))
      return Value(name);
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
    while (look().text == "*" || look().text == "/" || look().text == "%") {
      const auto op = tokens[pc++].text;
      const auto rhs = unary(active);
      if (!active)
        continue;
      const auto denominator = rhs.number();
      if ((op == "/" || op == "%") && denominator == 0)
        throw std::runtime_error("TJS2 subset: division by zero");
      const auto numerator = lhs.number();
      lhs = Value(op == "*"   ? numerator * denominator
                  : op == "/" ? numerator / denominator
                              : std::fmod(numerator, denominator));
    }
    return lhs;
  }
  Value sum(bool active) {
    auto lhs = product(active);
    while (look().text == "+" || look().text == "-") {
      const auto op = tokens[pc++].text;
      const auto rhs = product(active);
      if (!active)
        continue;
      if (op == "+" && (std::holds_alternative<std::string>(lhs.data) ||
                        std::holds_alternative<std::string>(rhs.data)))
        lhs = Value(lhs.string() + rhs.string());
      else
        lhs = Value(op == "+" ? lhs.number() + rhs.number()
                              : lhs.number() - rhs.number());
    }
    return lhs;
  }
  Value compare(bool active) {
    auto lhs = sum(active);
    while (look().text == "<" || look().text == "<=" || look().text == ">" ||
           look().text == ">=") {
      const auto op = tokens[pc++].text;
      const auto rhs = sum(active);
      if (active) {
        const auto a = lhs.number(), b = rhs.number();
        lhs = Value(op == "<"    ? a < b
                    : op == "<=" ? a <= b
                    : op == ">"  ? a > b
                                 : a >= b);
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
                               ? lhs.data == rhs.data
                               : lhs.string() == rhs.string();
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
      if (active)
        lhs = Value(go && rhs.truthy());
    }
    return lhs;
  }
  Value disjunction(bool active) {
    auto lhs = conjunction(active);
    while (match("||")) {
      const bool go = active && !lhs.truthy();
      auto rhs = conjunction(go);
      if (active)
        lhs = Value(!go || rhs.truthy());
    }
    return lhs;
  }
  Value expression(bool active) {
    auto test = disjunction(active);
    if (!match("?"))
      return test;
    const bool condition = active && test.truthy();
    auto whenTrue = expression(condition);
    expect(":");
    auto whenFalse = expression(active && !condition);
    return !active ? Value() : (condition ? whenTrue : whenFalse);
  }

  void statement(bool active) {
    machine.tick();
    if (match(";"))
      return;
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
    if (match("class")) {
      SourceClass definition;
      definition.name = identifier();
      if (match("extends"))
        definition.base = identifier();
      expect("{");
      while (!match("}")) {
        if (look().kind == Token::End)
          throw std::runtime_error(
              "TJS2 subset: unterminated class declaration");
        // This bounded subset deliberately rejects script class properties,
        // native dispatch/override syntax and dynamic member declarations.
        expect("function");
        const auto methodName = identifier();
        expect("(");
        std::vector<std::string> parameters;
        if (!match(")")) {
          do {
            if (parameters.size() >= 16)
              throw std::runtime_error(
                  "TJS2 subset: too many method parameters");
            const auto name = identifier();
            if (std::find(parameters.begin(), parameters.end(), name) !=
                parameters.end())
              throw std::runtime_error(
                  "TJS2 subset: duplicate method parameter");
            parameters.push_back(name);
          } while (match(","));
          expect(")");
        }
        if (look().text != "{")
          throw std::runtime_error("TJS2 subset: class method body expected");
        const auto start = look().offset;
        std::size_t braces = 0;
        do {
          if (look().kind == Token::End)
            throw std::runtime_error("TJS2 subset: unterminated class method");
          if (look().text == "{")
            ++braces;
          else if (look().text == "}")
            --braces;
          ++pc;
        } while (braces);
        if (active) {
          if (definition.methods.size() >= 64)
            throw std::runtime_error(
                "TJS2 subset: class method limit exceeded");
          const auto finish = tokens[pc - 1].offset + 1;
          if (!definition.methods
                   .emplace(
                       methodName,
                       SourceFunction{methodName, sourceName,
                                      sourceCode.substr(start, finish - start),
                                      std::move(parameters)})
                   .second)
            throw std::runtime_error("TJS2 subset: duplicate class method");
        }
        (void)match(";");
      }
      (void)match(";");
      if (active) {
        if (machine.classes.size() >= 128)
          throw std::runtime_error("TJS2 subset: class count limit exceeded");
        const auto className = definition.name;
        if (!machine.classes.emplace(className, std::move(definition)).second)
          throw std::runtime_error("TJS2 subset: duplicate class name");
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
        if (look().text == "{")
          ++braces;
        else if (look().text == "}")
          --braces;
        ++pc;
      } while (braces);
      if (active) {
        if (machine.functions.size() >= 256)
          throw std::runtime_error(
              "TJS2 subset: function count limit exceeded");
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
      if (match("else"))
        statement(active && !test.truthy());
      return;
    }
    if (match("for")) {
      expect("(");
      // The initializer uses the same bounded statement subset as ordinary
      // declarations/assignments, including its terminating semicolon.
      if (!match(";"))
        statement(active);
      const auto checkAt = pc;
      const bool hasCondition = look().text != ";";
      auto checked = hasCondition ? expression(active) : Value(true);
      expect(";");

      // Preserve the update expression text, then execute it as a statement
      // only after an active iteration. This avoids running disabled branches.
      const auto updateStart = look().offset;
      std::size_t nested = 0;
      while (look().kind != Token::End) {
        if (look().text == ")" && nested == 0)
          break;
        if (look().text == "(")
          ++nested;
        if (look().text == ")")
          --nested;
        ++pc;
      }
      if (look().kind == Token::End)
        throw std::runtime_error("TJS2 subset: unterminated for header");
      const auto updateEnd = look().offset;
      const auto updateText =
          sourceCode.substr(updateStart, updateEnd - updateStart);
      expect(")");

      const auto bodyAt = pc;
      statement(active && checked.truthy());
      const auto afterBody = pc;
      if (!active)
        return;

      unsigned iterations = checked.truthy() ? 1 : 0;
      while (checked.truthy()) {
        if (iterations++ >= 256)
          throw std::runtime_error("TJS2 subset: for iteration cap exceeded");
        if (!updateText.empty()) {
          Parser update(machine, sourceName, updateText + ";");
          update.run();
        }
        pc = checkAt;
        checked = hasCondition ? expression(true) : Value(true);
        expect(";");
        if (!checked.truthy())
          break;
        pc = bodyAt;
        statement(true);
        if (pc != afterBody)
          throw std::runtime_error("TJS2 subset: unstable for body");
      }
      pc = afterBody;
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
      if (!active)
        return;
      unsigned iterations = test.truthy() ? 1 : 0;
      while (test.truthy()) {
        if (++iterations > 256)
          throw std::runtime_error("TJS2 subset: loop iteration cap exceeded");
        pc = checkAt;
        test = expression(true);
        expect(")");
        if (!test.truthy())
          break;
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
        if (active)
          machine.set(name, value, true);
      } while (match(","));
      expect(";");
      return;
    }

    // TJS2 bootstrap assignments include System.title = ... and local vars.
    // Do not accept arbitrary expressions as an ignorable side effect.
    if (look().kind == Token::Identifier) {
      const auto saved = pc;
      auto name = identifier();
      while (match("."))
        name += "." + identifier();
      if (match("[")) {
        const auto index = expression(active);
        expect("]");
        if (match("=") || match("+=")) {
          const auto op = tokens[pc - 1].text;
          auto value = expression(active);
          expect(";");
          if (active) {
            if (op == "+=") {
              const auto before = machine.indexed(machine.get(name), index);
              if (std::holds_alternative<std::string>(before.data) ||
                  std::holds_alternative<std::string>(value.data))
                value = Value(before.string() + value.string());
              else
                value = Value(before.number() + value.number());
            }
            machine.setIndex(name, index, value);
          }
          return;
        }
        pc = saved;
      }
      if (match("++") || match("--")) {
        const auto op = tokens[pc - 1].text;
        expect(";");
        if (active) {
          machine.set(
              name,
              Value(machine.get(name).number() + (op == "++" ? 1.0 : -1.0)),
              false);
        }
        return;
      }
      if (match("=") || match("+=")) {
        const auto op = tokens[pc - 1].text;
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
          if (name == "System.title")
            machine.result.title = value.string();
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
      : machine(vm), sourceName(std::move(file)), sourceCode(std::move(text)),
        tokens(lex(sourceCode)) {}
  void run() {
    while (look().kind != Token::End)
      statement(true);
  }
};

Value Interpreter::invokeSource(const SourceFunction &function,
                                const std::vector<Value> &args,
                                const ObjectPtr &receiver) {
  if (args.size() != function.parameters.size())
    throw std::runtime_error("TJS2 subset: argument count mismatch: " +
                             function.name);
  if (functionDepth >= 16)
    throw std::runtime_error("TJS2 subset: function recursion limit exceeded");
  std::unordered_map<std::string, Value> frame;
  for (std::size_t index = 0; index < args.size(); ++index)
    frame.emplace(function.parameters[index], args[index]);
  if (receiver)
    frame["this"] = Value(receiver);
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

Value Interpreter::construct(const std::string &type,
                             const std::vector<Value> &args) {
  const auto integer = [](int value) { return Value(double(value)); };
  const auto createLayer = [&](const ObjectPtr &parent, bool primary) {
    if (layers.size() >= 256)
      throw std::runtime_error("TJS2 UI: Layer count limit exceeded");
    auto layer = std::make_shared<Object>();
    layer->kind = Object::Kind::Layer;
    layer->parent = parent;
    layer->layerId = nextLayerId++;
    layer->members["left"] = integer(0);
    layer->members["top"] = integer(0);
    layer->members["width"] = integer(primary ? 1280 : 0);
    layer->members["height"] = integer(primary ? 720 : 0);
    layer->members["opacity"] = integer(255);
    layer->members["order"] = integer(int(layer->layerId));
    layer->members["visible"] = Value(true);
    layer->members["enabled"] = Value(true);
    // KiriKiri exposes Layer.font as a mutable object. Only sizing and
    // coloring used by drawText are projected into the Kotonoha UI.
    auto font = std::make_shared<Object>();
    font->members["height"] = integer(24);
    font->members["color"] = integer(0xffffff);
    layer->members["font"] = Value(std::move(font));
    layers.push_back(layer);
    return layer;
  };
  if (auto registered = classes.find(type); registered != classes.end()) {
    // Only small source classes and their virtual Window/Layer/Dictionary
    // ancestors are supported; arbitrary native class inheritance is not.
    const SourceClass definition = registered->second;
    if (instances.size() >= 256)
      throw std::runtime_error("TJS2 subset: class instance limit exceeded");
    ObjectPtr instance;
    if (definition.base.empty()) {
      instance = std::make_shared<Object>();
      instance->kind = Object::Kind::ScriptClass;
    } else if (definition.base == "Window" || definition.base == "Dictionary") {
      const auto base = construct(definition.base, {});
      instance = std::get<ObjectPtr>(base.data);
    } else if (definition.base == "Layer") {
      const auto base = construct("Layer", args);
      instance = std::get<ObjectPtr>(base.data);
    } else {
      throw std::runtime_error("TJS2 subset: unsupported source class base: " +
                               definition.base);
    }
    instance->className = type;
    instances.push_back(instance);
    if (boundMethods.size() + definition.methods.size() > 2048)
      throw std::runtime_error("TJS2 subset: bound method limit exceeded");
    for (const auto &method : definition.methods) {
      const auto alias = "__tjs_bound_" + std::to_string(nextBoundMethod++);
      boundMethods.emplace(alias, BoundMethod{instance, method.second});
      instance->members[method.first] = Value(alias);
    }
    auto ctor = definition.methods.find(type);
    if (ctor == definition.methods.end())
      ctor = definition.methods.find("constructor");
    if (ctor != definition.methods.end()) {
      (void)invokeSource(ctor->second, args, instance);
    } else if (!args.empty() && definition.base != "Layer") {
      throw std::runtime_error(
          "TJS2 subset: constructor arguments without declared constructor");
    }
    return Value(instance);
  }
  if (type == "Array") {
    if (args.size() > 1)
      throw std::runtime_error("TJS2 subset: Array accepts 0 or 1 lengths");
    auto array = std::make_shared<Object>();
    array->kind = Object::Kind::Array;
    if (!args.empty()) {
      const auto length = checkedIndex(args.front());
      array->elements.resize(length);
    }
    return Value(std::move(array));
  }
  if (type == "Dictionary") {
    if (!args.empty())
      throw std::runtime_error("TJS2 UI: Dictionary takes no arguments");
    return Value(std::make_shared<Object>());
  }
  if (type == "Window") {
    // Virtual presentation exists, but native OS Window APIs are not
    // implemented and real KiriKiri scripts may still require them.
    result.requiresNativeRuntime = true;
    if (!args.empty())
      throw std::runtime_error(
          "TJS2 UI: Window constructor arguments unsupported");
    if (windows.size() >= 4)
      throw std::runtime_error("TJS2 UI: Window count limit exceeded");
    auto window = std::make_shared<Object>();
    window->kind = Object::Kind::Window;
    window->members["left"] = integer(0);
    window->members["top"] = integer(0);
    window->members["width"] = integer(1280);
    window->members["height"] = integer(720);
    window->members["visible"] = Value(true);
    windows.push_back(window);
    window->members["primaryLayer"] = Value(createLayer(window, true));
    return Value(window);
  }
  if (type == "Layer") {
    if (args.empty() || args.size() > 2)
      throw std::runtime_error("TJS2 UI: Layer requires Window/parent Layer");
    const auto *root = std::get_if<ObjectPtr>(&args.front().data);
    if (!root || !*root)
      throw std::runtime_error("TJS2 UI: Layer owner is not an object");
    ObjectPtr parent = *root;
    if (args.size() == 2) {
      const auto *second = std::get_if<ObjectPtr>(&args[1].data);
      if (!second || !*second || (*second)->kind != Object::Kind::Layer)
        throw std::runtime_error("TJS2 UI: invalid parent Layer");
      parent = *second;
    } else if (parent->kind == Object::Kind::Window) {
      parent = std::get<ObjectPtr>(parent->members.at("primaryLayer").data);
    }
    if (parent->kind != Object::Kind::Layer)
      throw std::runtime_error("TJS2 UI: Layer parent is not a Layer");
    if (args.size() == 2) {
      ObjectPtr ancestor = parent;
      while (ancestor && ancestor->kind == Object::Kind::Layer)
        ancestor = ancestor->parent.lock();
      if (!ancestor || ancestor != *root)
        throw std::runtime_error(
            "TJS2 UI: Layer parent belongs to another Window");
    }
    return Value(createLayer(parent, false));
  }
  throw std::runtime_error("TJS2 subset: native class not implemented: " +
                           type);
}

void Interpreter::snapshotLayers() {
  result.layers.clear();
  result.onKeyDown.clear();
  for (const auto &window : windows) {
    const auto callback = window->members.find("onKeyDown");
    if (callback == window->members.end())
      continue;
    const auto name = callback->second.string();
    if (name.empty())
      continue;
    if (name.size() > 128 || !hasCallable(name))
      throw std::runtime_error(
          "TJS2 UI: Window.onKeyDown must name a declared script function");
    result.onKeyDown = name;
  }
  const auto member = [](const ObjectPtr &object, const char *name) -> Value {
    const auto found = object->members.find(name);
    return found == object->members.end() ? Value() : found->second;
  };
  const auto bounded = [&](const ObjectPtr &object, const char *name,
                           int fallback, int low, int high) -> int {
    const auto value = member(object, name);
    if (std::holds_alternative<std::monostate>(value.data))
      return fallback;
    const auto number = value.number();
    if (!std::isfinite(number) || number < low || number > high)
      throw std::runtime_error(std::string("TJS2 UI: invalid ") + name);
    return static_cast<int>(number);
  };
  for (const auto &layer : layers) {
    TjsUiLayer frame;
    frame.id = layer->layerId;
    frame.width = bounded(layer, "width", 0, 0, 16384);
    frame.height = bounded(layer, "height", 0, 0, 16384);
    frame.order = bounded(layer, "order", int(frame.id), -10000, 10000);
    frame.opacity = bounded(layer, "opacity", 255, 0, 255);
    frame.visible = member(layer, "visible").truthy();
    frame.enabled = member(layer, "enabled").truthy();
    frame.texts = layer->texts;
    frame.rectangles = layer->rectangles;
    frame.storage = member(layer, "storage").string();
    if (!frame.storage.empty())
      frame.storage = safeName(frame.storage);
    frame.onClick = member(layer, "onClick").string();
    if (frame.onClick.empty())
      frame.onClick = member(layer, "onMouseDown").string();
    const auto callback = [&](const char *property) {
      const auto value = member(layer, property).string();
      if (!value.empty() && (value.size() > 128 || !hasCallable(value)))
        throw std::runtime_error(
            std::string(
                "TJS2 UI: event must name a declared script function: ") +
            property);
      return value;
    };
    if (!frame.onClick.empty() &&
        (frame.onClick.size() > 128 || !hasCallable(frame.onClick)))
      throw std::runtime_error(
          "TJS2 UI: onClick must name a declared script function");
    frame.onMouseEnter = callback("onMouseEnter");
    frame.onMouseLeave = callback("onMouseLeave");
    frame.onMouseUp = callback("onMouseUp");
    frame.onMouseMove = callback("onMouseMove");
    int x = 0, y = 0;
    int alpha = frame.opacity;
    auto owner = layer;
    for (std::size_t depth = 0; owner && depth < 16;
         ++depth, owner = owner->parent.lock()) {
      x += bounded(owner, "left", 0, -8192, 8192);
      y += bounded(owner, "top", 0, -8192, 8192);
      if (!member(owner, "visible").truthy())
        frame.visible = false;
      if (owner != layer)
        alpha = alpha * bounded(owner, "opacity", 255, 0, 255) / 255;
    }
    frame.left = x;
    frame.top = y;
    frame.opacity = alpha;
    result.layers.push_back(std::move(frame));
  }
  std::stable_sort(result.layers.begin(), result.layers.end(),
                   [](const TjsUiLayer &a, const TjsUiLayer &b) {
                     return a.order < b.order;
                   });
}

Value Interpreter::call(const std::string &name,
                        const std::vector<Value> &params,
                        const std::string &current, bool activeFlag) {
  if (!activeFlag)
    return {};

  if (auto it = boundMethods.find(name); it != boundMethods.end()) {
    const BoundMethod bound = it->second;
    return invokeSource(bound.function, params, bound.receiver);
  }
  if (auto it = functions.find(name); it != functions.end()) {
    const SourceFunction function = it->second;
    return invokeSource(function, params, {});
  }
  // Dotted method calls are resolved through the object member table, so
  // aliases like this.start() and controller.start() retain their receiver.
  if (name.find('.') != std::string::npos) {
    Value candidate;
    bool resolved = false;
    try {
      candidate = get(name);
      resolved = true;
    } catch (const std::runtime_error &) {
      // A native Layer method has no script member. Only lookup is
      // recoverable; method body errors must propagate to the title UI.
    }
    if (resolved) {
      if (const auto id = std::get_if<std::string>(&candidate.data)) {
        if (auto bound = boundMethods.find(*id); bound != boundMethods.end()) {
          const BoundMethod method = bound->second;
          return invokeSource(method.function, params, method.receiver);
        }
      }
    }
  }

  // A small safe subset of Layer/Window methods. All methods mutate only
  // the retained scene graph; no OS windows, plugins or native code run.
  const auto dot = name.rfind('.');
  if (dot != std::string::npos) {
    const auto method = name.substr(dot + 1);
    if (method == "push" || method == "pop") {
      const auto receiver = get(name.substr(0, dot));
      const auto object = std::get_if<ObjectPtr>(&receiver.data);
      if (!object || !*object || (*object)->kind != Object::Kind::Array)
        throw std::runtime_error("TJS2 subset: Array method receiver expected");
      auto &elements = (*object)->elements;
      if (method == "pop") {
        if (!params.empty())
          throw std::runtime_error("TJS2 subset: Array.pop takes no arguments");
        if (elements.empty())
          return {};
        Value item = std::move(elements.back());
        elements.pop_back();
        return item;
      }
      if (params.size() != 1 || elements.size() >= 4096)
        throw std::runtime_error("TJS2 subset: Array.push limit or arity");
      elements.push_back(params.front());
      return Value(static_cast<double>(elements.size()));
    }
    if (method == "loadImages" || method == "setImage" || method == "setPos" ||
        method == "setSize" || method == "show" || method == "hide" ||
        method == "drawText" || method == "clearText" || method == "fillRect") {
      const auto receiver = get(name.substr(0, dot));
      const auto ptr = std::get_if<ObjectPtr>(&receiver.data);
      if (!ptr || !*ptr ||
          ((*ptr)->kind != Object::Kind::Window &&
           (*ptr)->kind != Object::Kind::Layer))
        throw std::runtime_error(
            "TJS2 UI: method receiver is not a Layer/Window");
      auto &obj = *ptr;
      const auto integer = [&](std::size_t index) {
        const auto n = params.at(index).number();
        if (!std::isfinite(n) || n < -8192 || n > 16384)
          throw std::runtime_error("TJS2 UI: invalid coordinate/dimension");
        return Value(static_cast<double>(static_cast<int>(n)));
      };
      if (method == "show" || method == "hide") {
        if (!params.empty())
          throw std::runtime_error("TJS2 UI: show/hide take no arguments");
        obj->members["visible"] = Value(method == "show");
        return {};
      }
      if (obj->kind != Object::Kind::Layer)
        throw std::runtime_error(
            "TJS2 UI: image/position method requires Layer");
      if (method == "loadImages" || method == "setImage") {
        if (params.size() != 1)
          throw std::runtime_error("TJS2 UI: loadImages needs one storage ID");
        obj->members["storage"] = Value(safeName(params.front().string()));
        return {};
      }
      if (method == "clearText") {
        if (!params.empty())
          throw std::runtime_error("TJS2 UI: clearText takes no arguments");
        obj->texts.clear();
        return {};
      }
      if (method == "fillRect") {
        if (params.size() < 5 || params.size() > 6)
          throw std::runtime_error("TJS2 UI: fillRect needs 5-6 arguments");
        if (obj->rectangles.size() >= 256)
          throw std::runtime_error("TJS2 UI: rectangle count limit exceeded");
        TjsUiRect rect;
        rect.left = static_cast<int>(integer(0).number());
        rect.top = static_cast<int>(integer(1).number());
        rect.width = static_cast<int>(integer(2).number());
        rect.height = static_cast<int>(integer(3).number());
        if (rect.width <= 0 || rect.height <= 0)
          throw std::runtime_error("TJS2 UI: fillRect dimensions invalid");
        const auto color = params.at(4).number();
        if (!std::isfinite(color) || color < 0 || color > 4294967295.0)
          throw std::runtime_error("TJS2 UI: fillRect color invalid");
        rect.rgb = static_cast<std::uint32_t>(color) & 0xffffffu;
        if (params.size() == 6) {
          const auto opacity = params.at(5).number();
          if (!std::isfinite(opacity) || opacity < 0 || opacity > 255)
            throw std::runtime_error("TJS2 UI: fillRect opacity invalid");
          rect.opacity = static_cast<int>(opacity);
        }
        obj->rectangles.push_back(rect);
        return {};
      }
      if (method == "drawText") {
        if (params.size() < 3 || params.size() > 6)
          throw std::runtime_error("TJS2 UI: drawText needs 3-6 arguments");
        if (obj->texts.size() >= 256)
          throw std::runtime_error("TJS2 UI: text operation limit exceeded");
        TjsUiText run;
        run.left = static_cast<int>(integer(0).number());
        run.top = static_cast<int>(integer(1).number());
        run.text = params.at(2).string();
        if (run.text.size() > 4096)
          throw std::runtime_error("TJS2 UI: text size limit exceeded");
        if (params.size() >= 4) {
          const double color = params.at(3).number();
          if (!std::isfinite(color) || color < 0 || color > 4294967295.0)
            throw std::runtime_error("TJS2 UI: invalid text color");
          run.rgb = static_cast<std::uint32_t>(color) & 0xffffffu;
        }
        if (params.size() >= 5) {
          const double alpha = params.at(4).number();
          if (!std::isfinite(alpha) || alpha < 0 || alpha > 255)
            throw std::runtime_error("TJS2 UI: invalid text opacity");
          run.opacity = static_cast<int>(alpha);
        }
        const auto font = obj->members.find("font");
        if (font != obj->members.end()) {
          if (const auto ptr = std::get_if<ObjectPtr>(&font->second.data);
              ptr && *ptr) {
            if (auto height = (*ptr)->members.find("height");
                height != (*ptr)->members.end()) {
              const double points = height->second.number();
              if (!std::isfinite(points) || points < -256 || points > 256)
                throw std::runtime_error("TJS2 UI: invalid font height");
              run.size = std::max(8, static_cast<int>(std::abs(points)));
            }
          }
        }
        obj->texts.push_back(std::move(run));
        return {};
      }
      if (params.size() != 2)
        throw std::runtime_error("TJS2 UI: setPos/setSize require two values");
      obj->members[method == "setPos" ? "left" : "width"] = integer(0);
      obj->members[method == "setPos" ? "top" : "height"] = integer(1);
      return {};
    }
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
                  result.requestedPlugins.end(),
                  id) == result.requestedPlugins.end())
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
    if (result.kagEntry.empty())
      result.kagEntry = id;
    result.requiresNativeRuntime = true; // KAG is not TJS2.
    return;
  }
  if (!endsWith(id, ".tjs"))
    throw std::runtime_error("TJS2 subset: storage is not source TJS2: " + id);
  active.push_back(id);
  result.visitedScripts.push_back(id);
  const auto rawBytes = assets.readAsset(id);
  if (rawBytes.size() >= 8 &&
      std::memcmp(rawBytes.data(), "TJS2100\0", 8) == 0) {
    const auto compiledStrings = extractTjs2CompiledStrings(rawBytes);
    for (const auto &str : compiledStrings) {
      if (endsWith(str, ".ks") || endsWith(str, ".ks.scn")) {
        if (result.kagEntry.empty())
          result.kagEntry = str;
      }
    }
    result.requiresNativeRuntime = true;
    active.pop_back();
    return;
  }
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

// Retains the script variables, objects and named function bodies between
// input events. No interpreted code runs from SDL event callbacks except a
// bounded, explicitly registered onClick function on the runtime thread.
class TjsUiSession {
public:
  explicit TjsUiSession(const KirikiriAssets &assets, std::size_t budget)
      : vm(assets, budget), eventBudget(std::min<std::size_t>(budget, 4096)) {}
  Interpreter vm;
  std::size_t eventBudget = 4096;
  std::size_t hoveredLayer = 0;
};

TjsStartupResult runTjsStartupSubset(const KirikiriAssets &assets,
                                     std::size_t instructionBudget) {
  auto session = std::make_shared<TjsUiSession>(assets, instructionBudget);
  auto &vm = session->vm;
  const auto inspected = inspectTjsBootstrap(assets);
  if (inspected.entry.empty()) {
    vm.result.error = "TJS2 subset: startup.tjs not found";
  } else {
    try {
      vm.runFile(inspected.entry, 0);
      vm.snapshotLayers();
      vm.result.subsetCompleted = true;
    } catch (const std::exception &error) {
      vm.result.error = error.what();
    }
  }
  TjsStartupResult output = vm.result;
  output.uiSession = std::move(session);
  return output;
}

bool executeTjsInlineSubset(TjsStartupResult &result,
                            const KirikiriAssets &assets,
                            const std::string &source,
                            const std::string &sourceName,
                            std::size_t instructionBudget) {
  auto session = result.uiSession;
  if (!session)
    session = std::make_shared<TjsUiSession>(assets, instructionBudget);
  auto &vm = session->vm;
  vm.remaining = instructionBudget;
  vm.result.kagEntry.clear();
  vm.result.error.clear();
  bool success = false;
  try {
    Parser parser(vm, sourceName.empty() ? "inline.ks" : sourceName, source);
    parser.run();
    vm.snapshotLayers();
    vm.result.subsetCompleted = true;
    success = true;
  } catch (const std::exception &error) {
    vm.result.error = std::string("TJS2 inline: ") + error.what();
  }
  result = vm.result;
  result.uiSession = std::move(session);
  return success;
}

bool dispatchTjsUiClick(TjsStartupResult &result, float x, float y) {
  auto session = result.uiSession;
  if (!session || !result.subsetCompleted || !std::isfinite(x) ||
      !std::isfinite(y))
    return false;
  for (auto it = result.layers.rbegin(); it != result.layers.rend(); ++it) {
    const auto &layer = *it;
    if (!layer.visible || !layer.enabled || layer.opacity == 0 ||
        layer.width <= 0 || layer.height <= 0 || layer.onClick.empty())
      continue;
    if (x < layer.left || y < layer.top ||
        x >= double(layer.left) + layer.width ||
        y >= double(layer.top) + layer.height)
      continue;
    auto &vm = session->vm;
    vm.remaining = session->eventBudget;
    vm.result.kagEntry.clear();
    try {
      std::vector<Value> arguments;
      const auto arity = vm.callableArity(layer.onClick);
      if (arity > 4)
        throw std::runtime_error("TJS2 UI: unsupported pointer event arity");
      if (arity >= 1)
        arguments.emplace_back(double(x));
      if (arity >= 2)
        arguments.emplace_back(double(y));
      if (arity >= 3)
        arguments.emplace_back(1.0); // left mouse button
      if (arity >= 4)
        arguments.emplace_back(0.0); // no keyboard modifiers
      vm.call(layer.onClick, arguments, {}, true);
      vm.snapshotLayers();
    } catch (const std::exception &error) {
      vm.result.error = std::string("TJS2 UI event: ") + error.what();
    }
    result = vm.result;
    result.uiSession = std::move(session);
    return true;
  }
  return false;
}

bool dispatchTjsUiMouseUp(TjsStartupResult &result, float x, float y) {
  auto session = result.uiSession;
  if (!session || !result.subsetCompleted || !std::isfinite(x) ||
      !std::isfinite(y))
    return false;
  for (auto it = result.layers.rbegin(); it != result.layers.rend(); ++it) {
    const auto &layer = *it;
    if (!layer.visible || !layer.enabled || layer.opacity == 0 ||
        layer.width <= 0 || layer.height <= 0 || layer.onMouseUp.empty())
      continue;
    if (x < layer.left || y < layer.top ||
        x >= double(layer.left) + layer.width ||
        y >= double(layer.top) + layer.height)
      continue;
    auto &vm = session->vm;
    vm.remaining = session->eventBudget;
    vm.result.kagEntry.clear();
    try {
      const auto arity = vm.callableArity(layer.onMouseUp);
      if (arity > 4)
        throw std::runtime_error("TJS2 UI: excessive release parameters");
      std::vector<Value> args;
      if (arity >= 1)
        args.emplace_back(double(x));
      if (arity >= 2)
        args.emplace_back(double(y));
      if (arity >= 3)
        args.emplace_back(1.0);
      if (arity >= 4)
        args.emplace_back(0.0);
      vm.call(layer.onMouseUp, args, {}, true);
      vm.snapshotLayers();
    } catch (const std::exception &error) {
      vm.result.error = std::string("TJS2 UI mouse-up: ") + error.what();
    }
    result = vm.result;
    result.uiSession = std::move(session);
    return true;
  }
  return false;
}

bool dispatchTjsUiMouseMove(TjsStartupResult &result, float x, float y) {
  auto session = result.uiSession;
  if (!session || !result.subsetCompleted || !std::isfinite(x) ||
      !std::isfinite(y))
    return false;
  for (auto it = result.layers.rbegin(); it != result.layers.rend(); ++it) {
    const auto &layer = *it;
    if (!layer.visible || !layer.enabled || layer.opacity == 0 ||
        layer.width <= 0 || layer.height <= 0 || layer.onMouseMove.empty())
      continue;
    if (x < layer.left || y < layer.top ||
        x >= double(layer.left) + layer.width ||
        y >= double(layer.top) + layer.height)
      continue;
    auto &vm = session->vm;
    vm.remaining = session->eventBudget;
    vm.result.kagEntry.clear();
    try {
      const auto arity = vm.callableArity(layer.onMouseMove);
      if (arity > 4)
        throw std::runtime_error("TJS2 UI: excessive motion parameters");
      std::vector<Value> args;
      if (arity >= 1)
        args.emplace_back(double(x));
      if (arity >= 2)
        args.emplace_back(double(y));
      if (arity >= 3)
        args.emplace_back(0.0); // no mouse button change
      if (arity >= 4)
        args.emplace_back(0.0); // no modifiers
      vm.call(layer.onMouseMove, args, {}, true);
      vm.snapshotLayers();
    } catch (const std::exception &error) {
      vm.result.error = std::string("TJS2 UI mouse-move: ") + error.what();
    }
    result = vm.result;
    result.uiSession = std::move(session);
    return true;
  }
  return false;
}

bool dispatchTjsUiKeyDown(TjsStartupResult &result, int virtualKey) {
  auto session = result.uiSession;
  if (!session || !result.subsetCompleted || result.onKeyDown.empty() ||
      virtualKey < 1 || virtualKey > 255)
    return false;
  auto &vm = session->vm;
  vm.remaining = session->eventBudget;
  vm.result.kagEntry.clear();
  try {
    const auto arity = vm.callableArity(result.onKeyDown);
    if (arity > 2)
      throw std::runtime_error("TJS2 UI: excessive key event parameters");
    std::vector<Value> args;
    if (arity >= 1)
      args.emplace_back(double(virtualKey));
    if (arity >= 2)
      args.emplace_back(0.0); // keyboard modifiers
    vm.call(result.onKeyDown, args, {}, true);
    vm.snapshotLayers();
  } catch (const std::exception &error) {
    vm.result.error = std::string("TJS2 UI key-down: ") + error.what();
  }
  result = vm.result;
  result.uiSession = std::move(session);
  return true;
}

bool dispatchTjsUiHover(TjsStartupResult &result, float x, float y) {
  auto session = result.uiSession;
  if (!session || !result.subsetCompleted || !std::isfinite(x) ||
      !std::isfinite(y))
    return false;

  std::size_t next = 0;
  std::string nextHandler, previousHandler;
  for (auto it = result.layers.rbegin(); it != result.layers.rend(); ++it) {
    const auto &layer = *it;
    if (!layer.visible || !layer.enabled || layer.opacity == 0 ||
        layer.width <= 0 || layer.height <= 0 ||
        (layer.onClick.empty() && layer.onMouseEnter.empty() &&
         layer.onMouseLeave.empty()))
      continue;
    if (x < layer.left || y < layer.top ||
        x >= double(layer.left) + layer.width ||
        y >= double(layer.top) + layer.height)
      continue;
    next = layer.id;
    nextHandler = layer.onMouseEnter;
    break;
  }
  if (session->hoveredLayer == next)
    return false;
  for (const auto &layer : result.layers) {
    if (layer.id == session->hoveredLayer) {
      previousHandler = layer.onMouseLeave;
      break;
    }
  }
  session->hoveredLayer = next;
  auto &vm = session->vm;
  vm.remaining = session->eventBudget;
  bool called = false;
  try {
    const auto invoke = [&](const std::string &name) {
      if (name.empty())
        return;
      const auto arity = vm.callableArity(name);
      if (arity > 4)
        throw std::runtime_error("TJS2 UI: excessive hover parameters");
      std::vector<Value> args;
      if (arity >= 1)
        args.emplace_back(double(x));
      if (arity >= 2)
        args.emplace_back(double(y));
      if (arity >= 3)
        args.emplace_back(0.0);
      if (arity >= 4)
        args.emplace_back(0.0);
      vm.call(name, args, {}, true);
      called = true;
    };
    invoke(previousHandler);
    invoke(nextHandler);
    if (called)
      vm.snapshotLayers();
  } catch (const std::exception &error) {
    vm.result.error = std::string("TJS2 UI hover: ") + error.what();
    called = true;
  }
  if (called) {
    result = vm.result;
    result.uiSession = std::move(session);
  }
  return called;
}

} // namespace Kotonoha::Providers
