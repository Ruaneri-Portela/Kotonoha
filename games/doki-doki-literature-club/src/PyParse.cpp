#include "PyAst.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <set>

namespace Ddlc {

namespace {

struct Tok {
    enum T { Name, Num, Str, Op, NL, Indent, Dedent, End } t = End;
    std::string s;
    Value v;
};

void appendUtf8(std::string& out, uint32_t cp) { out += utf8Encode({cp}); }

std::vector<Tok> tokenize(const std::string& src) {
    std::vector<Tok> out;
    std::vector<int> indents{0};
    size_t i = 0, n = src.size();
    int depth = 0;
    bool lineStart = true;
    auto push = [&](Tok::T t, std::string s = {}, Value v = {}) { out.push_back({t, std::move(s), std::move(v)}); };
    while (i < n) {
        if (lineStart && depth == 0) {
            int col = 0;
            size_t j = i;
            while (j < n && (src[j] == ' ' || src[j] == '\t')) col = src[j++] == '\t' ? (col / 8 + 1) * 8 : col + 1;
            if (j >= n) { i = j; break; }
            if (src[j] == '\n' || src[j] == '\r' || src[j] == '#') {
                while (j < n && src[j] != '\n') ++j;
                i = j + 1;
                continue;
            }
            if (src[j] == '\\' && j + 1 < n && src[j + 1] == '\n') { i = j + 2; continue; }
            i = j;
            lineStart = false;
            if (col > indents.back()) {
                indents.push_back(col);
                push(Tok::Indent);
            } else {
                while (col < indents.back()) {
                    indents.pop_back();
                    push(Tok::Dedent);
                }
            }
        }
        const char c = src[i];
        if (c == '\n') {
            ++i;
            if (depth == 0) {
                if (!out.empty() && out.back().t != Tok::NL) push(Tok::NL);
                lineStart = true;
            }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f') { ++i; continue; }
        if (c == '#') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (c == '\\' && i + 1 < n && (src[i + 1] == '\n' || src[i + 1] == '\r')) {
            i += src[i + 1] == '\r' && i + 2 < n && src[i + 2] == '\n' ? 3 : 2;
            continue;
        }
        // String (with optional u/r/b prefixes).
        size_t q = i;
        bool raw = false;
        while (q < n && q - i < 2 && std::strchr("uUrRbB", src[q])) {
            if (src[q] == 'r' || src[q] == 'R') raw = true;
            ++q;
        }
        if (q < n && (src[q] == '"' || src[q] == '\'') && (q == i || std::isalpha((unsigned char)c))) {
            const char quote = src[q];
            const bool triple = q + 2 < n && src[q + 1] == quote && src[q + 2] == quote;
            size_t j = q + (triple ? 3 : 1);
            std::string s;
            while (j < n) {
                if (triple ? (src[j] == quote && j + 2 < n + 0 && j + 2 <= n - 1 && src[j + 1] == quote && src[j + 2] == quote)
                           : src[j] == quote) break;
                if (!triple && src[j] == '\n') break;
                if (src[j] == '\\' && j + 1 < n) {
                    const char e = src[j + 1];
                    if (raw) {
                        s += src[j];
                        s += e;
                        j += 2;
                        continue;
                    }
                    j += 2;
                    switch (e) {
                    case '\n': break;
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case '0': s += '\0'; break;
                    case 'a': s += '\a'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'v': s += '\v'; break;
                    case '\\': s += '\\'; break;
                    case '\'': s += '\''; break;
                    case '"': s += '"'; break;
                    case 'x':
                    case 'u':
                    case 'U': {
                        const size_t len = e == 'x' ? 2 : e == 'u' ? 4 : 8;
                        const uint32_t cp = uint32_t(std::strtoul(src.substr(j, len).c_str(), nullptr, 16));
                        j += len;
                        if (e == 'x') s += char(cp);
                        else appendUtf8(s, cp);
                        break;
                    }
                    default:
                        s += '\\';
                        s += e;
                    }
                    continue;
                }
                s += src[j++];
            }
            i = j + (triple ? 3 : 1);
            if (!out.empty() && out.back().t == Tok::Str) out.back().s += s; // implicit concatenation
            else push(Tok::Str, s);
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t j = i;
            while (j < n && (std::isalnum((unsigned char)src[j]) || src[j] == '_')) ++j;
            push(Tok::Name, src.substr(i, j - i));
            i = j;
            continue;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)src[i + 1]))) {
            size_t j = i;
            if (c == '0' && j + 1 < n && (src[j + 1] == 'x' || src[j + 1] == 'X')) {
                j += 2;
                while (j < n && std::isxdigit((unsigned char)src[j])) ++j;
                push(Tok::Num, {}, Value::integer(int64_t(std::strtoll(src.substr(i + 2, j - i - 2).c_str(), nullptr, 16))));
                if (j < n && (src[j] == 'L' || src[j] == 'l')) ++j;
                i = j;
                continue;
            }
            bool real = false;
            while (j < n && (std::isdigit((unsigned char)src[j]) || src[j] == '.')) real |= src[j++] == '.';
            if (j < n && (src[j] == 'e' || src[j] == 'E')) {
                real = true;
                ++j;
                if (j < n && (src[j] == '+' || src[j] == '-')) ++j;
                while (j < n && std::isdigit((unsigned char)src[j])) ++j;
            }
            const std::string text = src.substr(i, j - i);
            if (j < n && (src[j] == 'L' || src[j] == 'l')) ++j;
            push(Tok::Num, text, real ? Value::real(std::strtod(text.c_str(), nullptr)) : Value::integer(std::strtoll(text.c_str(), nullptr, 10)));
            i = j;
            continue;
        }
        static const char* ops3[] = {"**=", "//=", ">>=", "<<=", "..."};
        static const char* ops2[] = {"**", "//", "==", "!=", "<>", "<=", ">=", "+=", "-=", "*=", "/=", "%=",
                                     "&=", "|=", "^=", "<<", ">>", "->"};
        std::string op;
        for (const char* o : ops3)
            if (src.compare(i, 3, o) == 0) { op = o; break; }
        if (op.empty())
            for (const char* o : ops2)
                if (src.compare(i, 2, o) == 0) { op = o; break; }
        if (op.empty()) op = std::string(1, c);
        if (op == "(" || op == "[" || op == "{") ++depth;
        if ((op == ")" || op == "]" || op == "}") && depth > 0) --depth;
        push(Tok::Op, op);
        i += op.size();
    }
    if (!out.empty() && out.back().t != Tok::NL) push(Tok::NL);
    while (indents.size() > 1) {
        indents.pop_back();
        push(Tok::Dedent);
    }
    push(Tok::End);
    return out;
}

const std::set<std::string> kKeywords = {"and", "or", "not", "in", "is", "if", "else", "elif", "lambda", "for",
                                         "while", "def", "class", "return", "pass", "break", "continue",
                                         "global", "del", "import", "from", "raise", "try", "except", "finally",
                                         "with", "as", "print", "assert", "yield", "exec"};

class Parser {
public:
    explicit Parser(std::vector<Tok> t) : t_(std::move(t)) {}

    std::vector<StP> program() {
        std::vector<StP> out;
        while (!at(Tok::End)) {
            if (at(Tok::NL) || at(Tok::Indent) || at(Tok::Dedent)) { ++p_; continue; }
            statement(out);
        }
        return out;
    }

    ExP expressionOnly() {
        while (at(Tok::NL) || at(Tok::Indent)) ++p_;
        ExP e = testlist();
        while (at(Tok::NL) || at(Tok::Dedent)) ++p_;
        if (!at(Tok::End)) fail("trailing tokens");
        return e;
    }

private:
    const Tok& cur() const { return t_[p_]; }
    bool at(Tok::T t) const { return cur().t == t; }
    bool isOp(const char* s) const { return cur().t == Tok::Op && cur().s == s; }
    bool isKw(const char* s) const { return cur().t == Tok::Name && cur().s == s; }
    bool acceptOp(const char* s) {
        if (!isOp(s)) return false;
        ++p_;
        return true;
    }
    bool acceptKw(const char* s) {
        if (!isKw(s)) return false;
        ++p_;
        return true;
    }
    void expectOp(const char* s) {
        if (!acceptOp(s)) fail(std::string("expected '") + s + "'");
    }
    std::string name() {
        if (!at(Tok::Name)) fail("expected name");
        return t_[p_++].s;
    }
    [[noreturn]] void fail(const std::string& what) const {
        throw PyError("syntax error: " + what + " near '" + cur().s + "'");
    }

    static ExP mk(Ex::K k) {
        auto e = std::make_shared<Ex>();
        e->k = k;
        return e;
    }
    static StP mks(St::K k) {
        auto s = std::make_shared<St>();
        s->k = k;
        return s;
    }

    // ---- statements ----
    void statement(std::vector<StP>& out) {
        if (at(Tok::Name)) {
            const std::string& w = cur().s;
            if (w == "if") return out.push_back(ifStmt());
            if (w == "while") {
                ++p_;
                auto s = mks(St::K::While);
                s->conds.push_back(test());
                expectOp(":");
                s->body = suite();
                if (acceptKw("else")) { expectOp(":"); s->orelse = suite(); }
                return out.push_back(s);
            }
            if (w == "for") {
                ++p_;
                auto s = mks(St::K::For);
                s->targets.push_back(targetList());
                if (!acceptKw("in")) fail("expected in");
                s->value = testlist();
                expectOp(":");
                s->body = suite();
                if (acceptKw("else")) { expectOp(":"); s->orelse = suite(); }
                return out.push_back(s);
            }
            if (w == "try") return out.push_back(tryStmt());
            if (w == "def") return out.push_back(defStmt());
            if (w == "class") {
                ++p_;
                auto s = mks(St::K::Class);
                s->name = name();
                if (acceptOp("(")) {
                    while (!isOp(")")) {
                        s->bases.push_back(test());
                        if (!acceptOp(",")) break;
                    }
                    expectOp(")");
                }
                expectOp(":");
                s->body = suite();
                return out.push_back(s);
            }
            if (w == "with") {
                ++p_;
                auto s = mks(St::K::With);
                s->value = test();
                if (acceptKw("as")) s->targets.push_back(targetList());
                expectOp(":");
                s->body = suite();
                return out.push_back(s);
            }
            if (w == "@") fail("decorators unsupported");
        }
        simpleLine(out);
    }

    void simpleLine(std::vector<StP>& out) {
        out.push_back(smallStmt());
        while (acceptOp(";")) {
            if (at(Tok::NL) || at(Tok::End)) break;
            out.push_back(smallStmt());
        }
        if (at(Tok::NL)) ++p_;
        else if (!at(Tok::End) && !at(Tok::Dedent)) fail("expected end of line");
    }

    std::vector<StP> suite() {
        std::vector<StP> body;
        if (at(Tok::NL)) {
            ++p_;
            if (!at(Tok::Indent)) fail("expected indent");
            ++p_;
            while (!at(Tok::Dedent) && !at(Tok::End)) {
                if (at(Tok::NL)) { ++p_; continue; }
                statement(body);
            }
            if (at(Tok::Dedent)) ++p_;
        } else {
            simpleLine(body);
        }
        return body;
    }

    StP ifStmt() {
        ++p_; // if / elif
        auto s = mks(St::K::If);
        s->conds.push_back(test());
        expectOp(":");
        s->bodies.push_back(suite());
        while (isKw("elif")) {
            ++p_;
            s->conds.push_back(test());
            expectOp(":");
            s->bodies.push_back(suite());
        }
        if (acceptKw("else")) {
            expectOp(":");
            s->orelse = suite();
        }
        return s;
    }

    StP tryStmt() {
        ++p_;
        auto s = mks(St::K::Try);
        expectOp(":");
        s->body = suite();
        while (isKw("except")) {
            ++p_;
            ExP type;
            std::string as;
            if (!isOp(":")) {
                type = test();
                if (acceptKw("as") || acceptOp(",")) as = name();
            }
            expectOp(":");
            s->conds.push_back(type);
            s->names.push_back(as);
            s->bodies.push_back(suite());
        }
        if (acceptKw("else")) { expectOp(":"); s->orelse = suite(); }
        if (acceptKw("finally")) { expectOp(":"); s->finally = suite(); }
        return s;
    }

    void parameters(std::vector<std::pair<std::string, ExP>>& params, std::string& varargs, std::string& kwargs,
                    const char* close) {
        while (!isOp(close)) {
            if (acceptOp("**")) kwargs = name();
            else if (acceptOp("*")) varargs = name();
            else {
                std::string n = name();
                ExP def;
                if (acceptOp("=")) def = test();
                params.emplace_back(n, def);
            }
            if (!acceptOp(",")) break;
        }
    }

    StP defStmt() {
        ++p_;
        auto s = mks(St::K::Def);
        s->name = name();
        expectOp("(");
        parameters(s->params, s->varargs, s->kwargs, ")");
        expectOp(")");
        expectOp(":");
        s->body = suite();
        return s;
    }

    StP smallStmt() {
        if (at(Tok::Name)) {
            const std::string w = cur().s;
            if (w == "pass") { ++p_; return mks(St::K::Pass); }
            if (w == "break") { ++p_; return mks(St::K::Break); }
            if (w == "continue") { ++p_; return mks(St::K::Continue); }
            if (w == "return") {
                ++p_;
                auto s = mks(St::K::Return);
                if (!at(Tok::NL) && !isOp(";") && !at(Tok::End)) s->value = testlist();
                return s;
            }
            if (w == "global") {
                ++p_;
                auto s = mks(St::K::Global);
                do s->names.push_back(name()); while (acceptOp(","));
                return s;
            }
            if (w == "del") {
                ++p_;
                auto s = mks(St::K::Del);
                s->targets.push_back(testlist());
                return s;
            }
            if (w == "raise") {
                ++p_;
                auto s = mks(St::K::Raise);
                if (!at(Tok::NL) && !isOp(";") && !at(Tok::End)) s->value = testlist();
                return s;
            }
            if (w == "import" || w == "from") {
                auto s = mks(St::K::Import);
                // import a.b as c, d  /  from a import b as c
                std::string module;
                if (w == "from") {
                    ++p_;
                    while (!isKw("import")) module += t_[p_++].s;
                }
                ++p_;
                acceptOp("(");
                while (at(Tok::Name) || isOp("*")) {
                    std::string full = t_[p_++].s;
                    while (acceptOp(".")) full += "." + name();
                    std::string as = full.substr(0, full.find('.'));
                    if (acceptKw("as")) as = name();
                    s->names.push_back(as);
                    s->conds.push_back(nullptr);
                    s->varargs += (module.empty() ? full : module + "." + full) + ";";
                    if (!acceptOp(",")) break;
                }
                acceptOp(")");
                return s;
            }
            if (w == "exec") {
                // Python 2 exec statement: run as a call to the exec builtin.
                ++p_;
                auto s = mks(St::K::Expr);
                auto call = mk(Ex::K::Call);
                auto fn = mk(Ex::K::Name);
                fn->s = "exec";
                call->a.push_back(fn);
                call->a.push_back(test());
                s->value = call;
                return s;
            }
            if (w == "print" || w == "assert") {
                ++p_;
                auto s = mks(St::K::Pass);
                while (!at(Tok::NL) && !isOp(";") && !at(Tok::End)) ++p_;
                return s;
            }
        }
        ExP first = testlist();
        if (isOp("=")) {
            auto s = mks(St::K::Assign);
            s->targets.push_back(first);
            while (acceptOp("=")) s->targets.push_back(testlist());
            s->value = s->targets.back();
            s->targets.pop_back();
            return s;
        }
        if (cur().t == Tok::Op && cur().s.size() >= 2 && cur().s.back() == '=' && cur().s != "==" && cur().s != "!=" &&
            cur().s != "<=" && cur().s != ">=") {
            auto s = mks(St::K::Aug);
            s->op = cur().s.substr(0, cur().s.size() - 1);
            ++p_;
            s->targets.push_back(first);
            s->value = testlist();
            return s;
        }
        auto s = mks(St::K::Expr);
        s->value = first;
        return s;
    }

    // ---- expressions ----
    ExP targetList() {
        ExP e = expr();
        if (!isOp(",")) return e;
        auto t = mk(Ex::K::Tuple);
        t->a.push_back(e);
        while (acceptOp(",")) {
            if (isKw("in") || isOp("=")) break;
            t->a.push_back(expr());
        }
        return t;
    }

    ExP testlist() {
        ExP e = test();
        if (!isOp(",")) return e;
        auto t = mk(Ex::K::Tuple);
        t->a.push_back(e);
        while (acceptOp(",")) {
            if (at(Tok::NL) || isOp("=") || isOp(")") || at(Tok::End) || isOp(";") || isOp(":")) break;
            t->a.push_back(test());
        }
        return t;
    }

    ExP test() {
        if (isKw("lambda")) {
            ++p_;
            auto e = mk(Ex::K::Lambda);
            std::string va, kw;
            parameters(e->kw, va, kw, ":");
            expectOp(":");
            e->a.push_back(test());
            return e;
        }
        ExP e = orTest();
        if (isKw("if")) {
            // Not a conditional expression when followed by a comprehension's own if.
            ++p_;
            auto c = mk(Ex::K::IfExp);
            c->cond = orTest();
            if (!acceptKw("else")) fail("expected else");
            c->a = {e, test()};
            return c;
        }
        return e;
    }

    ExP orTest() {
        ExP e = andTest();
        while (acceptKw("or")) {
            auto o = mk(Ex::K::Or);
            o->a = {e, andTest()};
            e = o;
        }
        return e;
    }
    ExP andTest() {
        ExP e = notTest();
        while (acceptKw("and")) {
            auto o = mk(Ex::K::And);
            o->a = {e, notTest()};
            e = o;
        }
        return e;
    }
    ExP notTest() {
        if (acceptKw("not")) {
            auto o = mk(Ex::K::Not);
            o->a = {notTest()};
            return o;
        }
        return comparison();
    }
    ExP comparison() {
        ExP e = expr();
        ExP cmp;
        while (true) {
            std::string op;
            if (cur().t == Tok::Op && (cur().s == "<" || cur().s == ">" || cur().s == "==" || cur().s == "!=" ||
                                       cur().s == "<>" || cur().s == "<=" || cur().s == ">=")) {
                op = cur().s == "<>" ? "!=" : cur().s;
                ++p_;
            } else if (isKw("in")) {
                op = "in";
                ++p_;
            } else if (isKw("not") && t_[p_ + 1].t == Tok::Name && t_[p_ + 1].s == "in") {
                op = "not in";
                p_ += 2;
            } else if (isKw("is")) {
                ++p_;
                op = acceptKw("not") ? "is not" : "is";
            } else {
                break;
            }
            if (!cmp) {
                cmp = mk(Ex::K::Cmp);
                cmp->a.push_back(e);
            }
            cmp->ops.push_back(op);
            cmp->a.push_back(expr());
        }
        return cmp ? cmp : e;
    }

    ExP binLevel(int level) {
        static const std::vector<std::vector<std::string>> levels = {
            {"|"}, {"^"}, {"&"}, {"<<", ">>"}, {"+", "-"}, {"*", "/", "//", "%"}};
        if (level >= int(levels.size())) return factor();
        ExP e = binLevel(level + 1);
        while (cur().t == Tok::Op) {
            bool found = false;
            for (const auto& op : levels[size_t(level)])
                if (cur().s == op) found = true;
            if (!found) break;
            auto b = mk(Ex::K::Bin);
            b->s = cur().s;
            ++p_;
            b->a = {e, binLevel(level + 1)};
            e = b;
        }
        return e;
    }
    ExP expr() { return binLevel(0); }

    ExP factor() {
        if (isOp("-") || isOp("+") || isOp("~")) {
            auto u = mk(Ex::K::Unary);
            u->s = cur().s;
            ++p_;
            u->a = {factor()};
            return u;
        }
        ExP e = trailers(atom());
        if (acceptOp("**")) {
            auto b = mk(Ex::K::Bin);
            b->s = "**";
            b->a = {e, factor()};
            return b;
        }
        return e;
    }

    ExP comprehension(ExP elt) {
        // elt for target in iter [if cond]
        auto c = mk(Ex::K::ListComp);
        c->a = {elt};
        ++p_; // for
        c->target = targetList();
        if (!acceptKw("in")) fail("expected in");
        c->iter = orTest();
        if (acceptKw("if")) c->cond = orTest();
        return c;
    }

    ExP atom() {
        const Tok& t = cur();
        if (t.t == Tok::Num) {
            auto e = mk(Ex::K::Const);
            e->v = t.v;
            ++p_;
            return e;
        }
        if (t.t == Tok::Str) {
            auto e = mk(Ex::K::Const);
            e->v = Value::str(t.s);
            ++p_;
            return e;
        }
        if (t.t == Tok::Name) {
            if (t.s == "None" || t.s == "True" || t.s == "False") {
                auto e = mk(Ex::K::Const);
                if (t.s != "None") e->v = Value::boolean(t.s == "True");
                ++p_;
                return e;
            }
            if (kKeywords.count(t.s) && t.s != "print") fail("unexpected keyword");
            auto e = mk(Ex::K::Name);
            e->s = t.s;
            ++p_;
            return e;
        }
        if (acceptOp("(")) {
            if (acceptOp(")")) return mk(Ex::K::Tuple);
            ExP first = test();
            if (isKw("for")) {
                ExP c = comprehension(first);
                expectOp(")");
                return c;
            }
            if (acceptOp(")")) return first;
            auto tup = mk(Ex::K::Tuple);
            tup->a.push_back(first);
            while (acceptOp(",")) {
                if (isOp(")")) break;
                tup->a.push_back(test());
            }
            expectOp(")");
            return tup;
        }
        if (acceptOp("[")) {
            auto list = mk(Ex::K::List);
            if (acceptOp("]")) return list;
            ExP first = test();
            if (isKw("for")) {
                ExP c = comprehension(first);
                expectOp("]");
                return c;
            }
            list->a.push_back(first);
            while (acceptOp(",")) {
                if (isOp("]")) break;
                list->a.push_back(test());
            }
            expectOp("]");
            return list;
        }
        if (acceptOp("{")) {
            auto dict = mk(Ex::K::Dict);
            while (!isOp("}")) {
                ExP k = test();
                if (!acceptOp(":")) { // set literal: keep as list
                    dict->k = Ex::K::List;
                    dict->a.push_back(k);
                    if (!acceptOp(",")) break;
                    continue;
                }
                dict->a.push_back(k);
                dict->a.push_back(test());
                if (!acceptOp(",")) break;
            }
            expectOp("}");
            return dict;
        }
        fail("unexpected token");
    }

    ExP trailers(ExP e) {
        while (true) {
            if (acceptOp(".")) {
                auto a = mk(Ex::K::Attr);
                a->a = {e};
                a->s = name();
                e = a;
            } else if (acceptOp("(")) {
                auto c = mk(Ex::K::Call);
                c->a.push_back(e);
                while (!isOp(")")) {
                    if (acceptOp("**")) c->dstar = test();
                    else if (acceptOp("*")) c->star = test();
                    else if (at(Tok::Name) && t_[p_ + 1].t == Tok::Op && t_[p_ + 1].s == "=") {
                        std::string k = name();
                        ++p_;
                        c->kw.emplace_back(k, test());
                    } else {
                        ExP arg = test();
                        if (isKw("for")) arg = comprehension(arg);
                        c->a.push_back(arg);
                    }
                    if (!acceptOp(",")) break;
                }
                expectOp(")");
                e = c;
            } else if (acceptOp("[")) {
                auto s = mk(Ex::K::Sub);
                s->a = {e, subscript()};
                expectOp("]");
                e = s;
            } else {
                return e;
            }
        }
    }

    ExP subscript() {
        ExP lo, hi, step;
        if (!isOp(":")) {
            lo = test();
            if (!isOp(":")) {
                if (!isOp(",")) return lo;
                auto t = mk(Ex::K::Tuple);
                t->a.push_back(lo);
                while (acceptOp(",")) {
                    if (isOp("]")) break;
                    t->a.push_back(test());
                }
                return t;
            }
        }
        expectOp(":");
        if (!isOp("]") && !isOp(":")) hi = test();
        if (acceptOp(":") && !isOp("]")) step = test();
        auto s = mk(Ex::K::Slice);
        s->a = {lo, hi, step};
        return s;
    }

    std::vector<Tok> t_;
    size_t p_ = 0;
};

} // namespace

ExP parseExpression(const std::string& src) { return Parser(tokenize(src)).expressionOnly(); }

std::vector<StP> parseProgram(const std::string& src) { return Parser(tokenize(src)).program(); }

} // namespace Ddlc
