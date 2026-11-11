#include <Kotonoha/localization/Encoding.hpp>
#include <ZeroNoTsukaima/Scene.hpp>

#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace ZeroNoTsukaima {
namespace {
struct Token {
  std::string text;
  bool string = false;
};
using Tokens = std::vector<Token>;

// Decode before lexing: a CP932 trail byte can be 0x5c (backslash).
Tokens Lex(const std::string &s) {
  Tokens out;
  size_t p = 0;
  while (p < s.size()) {
    if (std::isspace(static_cast<unsigned char>(s[p]))) {
      ++p;
      continue;
    }
    if (s.compare(p, 2, "//") == 0) {
      p = s.find('\n', p);
      if (p == std::string::npos)
        break;
      continue;
    }
    if (s.compare(p, 2, "/*") == 0) {
      const size_t end = s.find("*/", p + 2);
      if (end == std::string::npos)
        throw std::runtime_error("unterminated comment");
      p = end + 2;
      continue;
    }
    if (s[p] == '"') {
      std::string value;
      ++p;
      while (p < s.size() && s[p] != '"') {
        char c = s[p++];
        if (c == '\\') {
          if (p == s.size())
            throw std::runtime_error("unterminated escape");
          c = s[p++];
          switch (c) {
          case 'n':
            c = '\n';
            break;
          case 'r':
            c = '\r';
            break;
          case 't':
            c = '\t';
            break;
          case '\\':
          case '"':
            break;
          default:
            throw std::runtime_error("unsupported string escape");
          }
        }
        value += c;
      }
      if (p == s.size())
        throw std::runtime_error("unterminated string");
      ++p;
      out.push_back({value, true});
    } else if (std::isalnum(static_cast<unsigned char>(s[p])) || s[p] == '_') {
      size_t start = p++;
      while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) ||
                              s[p] == '_' || s[p] == '.'))
        ++p;
      out.push_back({s.substr(start, p - start), false});
    } else {
      std::string op = s.substr(p, 2);
      if (op == "==" || op == "!=" || op == "<=" || op == ">=" || op == "&&" ||
          op == "||" || op == "+=" || op == "-=")
        p += 2;
      else
        op = s.substr(p++, 1);
      out.push_back({op, false});
    }
  }
  return out;
}

std::string Value(const Tokens &t) {
  if (t.size() == 1 && t[0].string)
    return t[0].text;
  std::string out;
  for (const auto &v : t) {
    if (v.string)
      throw std::runtime_error("string in numeric expression");
    if (!out.empty())
      out += ' ';
    out += v.text;
  }
  return out;
}

// Disc references are constants of the form (0x01000000 + N), not arbitrary
// Squirrel expressions. Reject dynamic references rather than guessing an ID.
std::string DiscId(const Tokens &t, unsigned base, bool nullable = false) {
  std::string s;
  for (const auto &v : t) {
    if (v.string)
      throw std::runtime_error("string disc reference");
    if (v.text != "(" && v.text != ")")
      s += v.text;
  }
  if (nullable && s == "null")
    return {};
  size_t used = 0;
  unsigned long id = std::stoul(s, &used, 0);
  if (used < s.size() && s[used] == '+') {
    size_t n = 0;
    const auto offset = std::stoul(s.substr(used + 1), &n, 0);
    used += 1 + n;
    id += offset;
  }
  if (used != s.size() || id < base || id >= base + 0x1000000UL)
    throw std::runtime_error("invalid disc reference");
  return std::to_string(id - base);
}

std::string SceneName(const Tokens &t, bool nullable = false) {
  const auto id = DiscId(t, 0x01000000, nullable);
  if (id.empty())
    return {};
  char name[32];
  std::snprintf(name, sizeof(name), "%04lu.KS", std::stoul(id));
  return name;
}

struct Parser {
  Tokens tokens;
  size_t p = 0, serial = 0;
  std::vector<SceneTag> tags;

  bool Eat(const std::string &s) {
    if (p < tokens.size() && !tokens[p].string && tokens[p].text == s) {
      ++p;
      return true;
    }
    return false;
  }
  void Need(const std::string &s) {
    if (!Eat(s))
      throw std::runtime_error("expected " + s + " at token " +
                               std::to_string(p));
  }
  Tokens Group() {
    Need("(");
    const size_t start = p;
    int depth = 1;
    while (p < tokens.size()) {
      if (!tokens[p].string) {
        if (tokens[p].text == "(")
          ++depth;
        else if (tokens[p].text == ")" && --depth == 0) {
          Tokens result(tokens.begin() + start, tokens.begin() + p);
          ++p;
          return result;
        }
      }
      ++p;
    }
    throw std::runtime_error("unterminated argument group");
  }
  void Add(std::string name, std::map<std::string, std::string> attrs = {}) {
    tags.push_back({std::move(name), std::move(attrs)});
  }
  void Statement() {
    if (Eat(";"))
      return;
    if (Eat("{")) {
      while (!Eat("}")) {
        if (p == tokens.size())
          throw std::runtime_error("unterminated block");
        Statement();
      }
      return;
    }
    if (Eat("if")) {
      auto condition = Group();
      // Normal scene entry is not a debug/load jump. Rebuilding a saved state
      // by replay likewise runs the ordinary scene path.
      std::string expr = Value(condition);
      if (expr == "isJump ( )")
        expr = "false";
      else
        ValidateExpression(condition);
      const auto id = std::to_string(serial++);
      const auto otherwise = "*concerto_else_" + id;
      const auto done = "*concerto_end_" + id;
      Add("goto", {{"target", otherwise}, {"if", "!(" + expr + ")"}});
      Statement();
      Add("goto", {{"target", done}});
      Add("label", {{"label", otherwise}});
      if (Eat("else"))
        Statement();
      Add("label", {{"label", done}});
      return;
    }
    if (p == tokens.size() || tokens[p].string)
      throw std::runtime_error("expected call");
    const std::string name = tokens[p++].text;
    if (name == "while" || name == "for" || name == "foreach" ||
        name == "switch" || name == "function" || name == "return" ||
        name == "local" || name == "class" || name == "do" || name == "else")
      throw std::runtime_error("unsupported Squirrel statement: " + name);
    auto body = Group();
    if (p < tokens.size() && !tokens[p].string && tokens[p].text == "{")
      throw std::runtime_error("unexpected block after call");
    std::vector<Tokens> args;
    size_t start = 0;
    std::vector<std::string> brackets;
    for (size_t i = 0; i < body.size(); ++i) {
      if (body[i].string)
        continue;
      const auto &v = body[i].text;
      if (v == "(" || v == "{" || v == "[")
        brackets.push_back(v);
      else if (v == ")" || v == "}" || v == "]") {
        if (brackets.empty() || (v == ")" && brackets.back() != "(") ||
            (v == "}" && brackets.back() != "{") ||
            (v == "]" && brackets.back() != "["))
          throw std::runtime_error("mismatched argument brackets");
        brackets.pop_back();
      } else if (v == "," && brackets.empty()) {
        args.emplace_back(body.begin() + start, body.begin() + i);
        start = i + 1;
      }
    }
    if (!brackets.empty())
      throw std::runtime_error("unterminated argument brackets");
    if (!body.empty())
      args.emplace_back(body.begin() + start, body.end());
    Call(name, args);
    Eat(";"); // Several original commands intentionally omit semicolons.
  }
  static void ValidateExpression(const Tokens &t) {
    if (t.empty())
      throw std::runtime_error("empty expression");
    bool operand = true;
    int depth = 0;
    for (const auto &v : t) {
      if (v.string || v.text.empty())
        throw std::runtime_error("unsupported condition expression");
      const auto &s = v.text;
      if (operand) {
        if (s == "!" || s == "-")
          continue;
        if (s == "(") {
          ++depth;
          continue;
        }
        if (!(std::isalnum(static_cast<unsigned char>(s[0])) || s[0] == '_'))
          throw std::runtime_error("expected expression operand");
        operand = false;
      } else if (s == ")" && depth > 0) {
        --depth;
      } else if (s == "+" || s == "-" || s == "*" || s == "/" || s == "%" ||
                 s == "==" || s == "!=" || s == "<" || s == ">" || s == "<=" ||
                 s == ">=" || s == "&&" || s == "||") {
        operand = true;
      } else {
        throw std::runtime_error("unsupported expression operator");
      }
    }
    if (operand || depth != 0)
      throw std::runtime_error("incomplete expression");
  }
  static std::string Condition(const Tokens &t) {
    // next/selectItem accept either a boolean expression or its string form.
    Tokens expression = t.size() == 1 && t[0].string ? Lex(t[0].text) : t;
    ValidateExpression(expression);
    return Value(expression);
  }
  static void ValidateAssignment(const std::string &s) {
    if (s.empty())
      return;
    const auto t = Lex(s);
    if (t.size() < 3 || t[0].string ||
        !(std::isalpha(static_cast<unsigned char>(t[0].text[0])) ||
          t[0].text[0] == '_') ||
        (t[1].text != "=" && t[1].text != "+=" && t[1].text != "-="))
      throw std::runtime_error("unsupported eval assignment");
    ValidateExpression(Tokens(t.begin() + 2, t.end()));
  }
  void Call(const std::string &name, const std::vector<Tokens> &a) {
    const auto count = [&](size_t n) {
      if (a.size() != n)
        throw std::runtime_error("wrong argument count for " + name);
    };
    const auto text = [&](size_t i) {
      if (a.at(i).size() == 1 && a[i][0].text == "null" && !a[i][0].string)
        return std::string();
      if (a.at(i).size() != 1 || !a[i][0].string)
        throw std::runtime_error("expected string for " + name);
      return a[i][0].text;
    };
    if (name == "set") {
      count(2);
      const auto target = text(0);
      const Tokens &table = a[1];
      if (table.size() < 2 || table.front().text != "{" ||
          table.back().text != "}")
        throw std::runtime_error("expected set property table");
      std::map<std::string, std::string> props{{"name", target}};
      size_t i = 1;
      while (i + 1 < table.size()) {
        const auto key = table[i++].text;
        if (i >= table.size() || table[i++].text != "=")
          throw std::runtime_error("expected property assignment");
        const size_t start = i;
        int depth = 0;
        while (i + 1 < table.size()) {
          if (!table[i].string) {
            if (table[i].text == "(")
              ++depth;
            if (table[i].text == ")")
              --depth;
            if (depth < 0)
              throw std::runtime_error("unbalanced property expression");
            if (table[i].text == "," && depth == 0)
              break;
          }
          ++i;
        }
        if (depth != 0 || i == start)
          throw std::runtime_error("invalid property value");
        Tokens value(table.begin() + start, table.begin() + i);
        if (key == "image")
          props[key] = "scenedat/" + DiscId(value, 0x02000000);
        else if (key == "play")
          props[key] = DiscId(value, 0x03000000);
        else
          props[key] = Value(value);
        if (i + 1 < table.size())
          ++i;
      }
      Add(target == "bgm" || target.compare(0, 2, "se") == 0 ? "concerto_sound"
                                                             : "concerto_layer",
          std::move(props));
    } else if (name == "reset" || name == "back") {
      count(0);
      Add("concerto_" + name);
    } else if (name == "trans") {
      count(2);
      Add("concerto_trans", {{"type", text(0)}, {"time", Value(a[1])}});
    } else if (name == "waitAction") {
      count(2);
      Add("wact");
    } else if (name == "waitLoad") {
      count(1); // Picture decoding is synchronous in the provider.
    } else if (name == "voice") {
      count(2);
      Add("entryvoice", {{"voice", DiscId(a[0], 0x04000000)}});
      Add("playvoice");
    } else if (name == "waitVoice") {
      count(0);
      Add("waitvoice");
    } else if (name == "movie") {
      count(1);
      if (a[0].size() != 1 || a[0][0].string ||
          a[0][0].text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("dynamic movie references are not supported");
      Add("movie", {{"no", a[0][0].text}});
    } else if (name == "talk") {
      count(4);
      const auto speaker = text(0), display = text(1), line = text(2);
      const auto voice = DiscId(a[3], 0x04000000, true);
      Add("er");
      Add("msgon");
      Add("dispname", {{"name", speaker}, {"disp", display}, {"voice", voice}});
      if (!voice.empty()) {
        Add("entryvoice", {{"voice", voice}});
        Add("playvoice");
      }
      Add("ch", {{"text", line}});
      Add("p");
    } else if (name == "next") {
      count(2);
      Add("next", {{"storage", SceneName(a[0])}, {"if", Condition(a[1])}});
    } else if (name == "selectInit") {
      count(0);
      Add("selclear");
    } else if (name == "selectItem") {
      count(4);
      const auto expression = text(2);
      ValidateAssignment(expression);
      Add("seladd", {{"text", text(0)},
                     {"storage", SceneName(a[1], true)},
                     {"exp", expression},
                     {"if", Condition(a[3])}});
    } else if (name == "select" || name == "msgon" || name == "msgoff") {
      count(0);
      Add(name);
    } else if (name == "title") {
      count(1);
      Add("title", {{"name", text(0)}});
    } else if (name == "eval") {
      count(1);
      const auto expression = text(0);
      ValidateAssignment(expression);
      Add("eval", {{"exp", expression}});
    } else if (name == "waitTime") {
      count(2);
      Add("wait", {{"time", Value(a[0])}});
    } else if (name == "beginSkip" || name == "endSkip") {
      count(0);
      Add(name == "beginSkip" ? "beginskip" : "endskip");
    } else {
      // Preserve the fact that these commands were NOT implemented. Hosts can
      // display UnsupportedCommands(); the headless runner reports it.
      Add("concerto_unsupported", {{"command", name}});
    }
  }
};
} // namespace

bool ParseConcertoScene(const std::vector<uint8_t> &source,
                        std::vector<SceneTag> *out, std::string *error) {
  try {
    if (source.empty())
      throw std::runtime_error("empty Concerto source");
    const std::string cp932(source.begin(), source.end());
    Parser parser{Lex(Kotonoha::ShiftJisToUtf8(cp932))};
    while (parser.p < parser.tokens.size())
      parser.Statement();
    *out = std::move(parser.tags);
    if (error)
      error->clear();
    return true;
  } catch (const std::exception &e) {
    if (error)
      *error = e.what();
    return false;
  }
}
} // namespace ZeroNoTsukaima
