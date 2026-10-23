#pragma once
// Internal AST of the Python interpreter.
#include "Ddlc/Python.hpp"

namespace Ddlc {

struct Ex {
    enum class K { Const, Name, Attr, Sub, Slice, Call, Bin, Unary, Not, And, Or, Cmp, IfExp, List, Tuple, Dict, ListComp, Lambda } k = K::Const;
    Value v;
    std::string s;                                  // name / attribute / operator
    std::vector<ExP> a;                             // operands, items, positional args
    std::vector<std::string> ops;                   // comparison operators
    std::vector<std::pair<std::string, ExP>> kw;    // call keywords; lambda parameters
    ExP star, dstar;                                // *args / **kwargs
    ExP target, iter, cond;                         // list comprehension
};

struct St {
    enum class K { Expr, Assign, Aug, If, While, For, Try, Def, Class, Return, Pass, Break, Continue, Global, Del, Import, Raise, With } k = K::Pass;
    std::vector<ExP> targets;
    ExP value;
    std::string op, name, varargs, kwargs;
    std::vector<ExP> conds;                         // if/while conditions, except types (may be null)
    std::vector<std::vector<StP>> bodies;           // if branches, except handlers
    std::vector<std::string> names;                 // global names, except "as" names, imports
    std::vector<std::pair<std::string, ExP>> params; // def parameters with defaults
    std::vector<StP> body, orelse, finally;
    std::vector<ExP> bases;
};

ExP parseExpression(const std::string& src);
std::vector<StP> parseProgram(const std::string& src);

} // namespace Ddlc
