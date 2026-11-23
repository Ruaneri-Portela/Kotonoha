#include "Ddlc/Python.hpp"
#include "PythonInternal.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace Ddlc {

struct Interp::State {};

Interp::Interp() : state_(std::make_unique<State>()) {}
Interp::~Interp() = default;

Value Interp::eval(const std::string &, const ScopeP &) {
  throw std::runtime_error("Python not available in this build");
}

void Interp::exec(const std::string &, const ScopeP &) {
  throw std::runtime_error("Python not available in this build");
}

Value Interp::call(const Value &, Args, Kwargs) {
  throw std::runtime_error("Python not available in this build");
}

Value Interp::getAttr(const Value &, const std::string &) {
  throw std::runtime_error("Python not available in this build");
}

void Interp::setAttr(const Value &, const std::string &, const Value &) {
  throw std::runtime_error("Python not available in this build");
}

Value Interp::getItem(const Value &, const Value &) {
  throw std::runtime_error("Python not available in this build");
}

void Interp::setItem(const Value &, const Value &, const Value &) {
  throw std::runtime_error("Python not available in this build");
}

Value Interp::lookup(const std::string &, const ScopeP &) const {
  throw std::runtime_error("Python not available in this build");
}

bool Interp::has(const std::string &) const { return false; }

void Interp::set(const std::string &, const Value &) {
  throw std::runtime_error("Python not available in this build");
}

void Interp::def(const std::string &, NativeFn) {
  throw std::runtime_error("Python not available in this build");
}

void Interp::registerModule(const std::string &, const Value &) {
  throw std::runtime_error("Python not available in this build");
}

Value Interp::nameSpace(const std::string &) {
  throw std::runtime_error("Python not available in this build");
}

std::vector<Value> Interp::iterate(const Value &) { return {}; }

std::vector<uint32_t> utf8Decode(const std::string &) { return {}; }

std::string utf8Encode(const std::vector<uint32_t> &) { return ""; }

size_t utf8Length(const std::string &s) { return s.length(); }

bool pythonTruthy(const PythonValue &) { return false; }

std::string pythonString(const PythonValue &, bool) { return {}; }

const Value *pythonAttribute(Object &, const std::string &) { return nullptr; }

void pythonRefreshItems(Object &) {}

void pythonRefreshDict(Object &) {}

PythonThreadRelease::PythonThreadRelease() {}
PythonThreadRelease::~PythonThreadRelease() {}

} // namespace Ddlc
