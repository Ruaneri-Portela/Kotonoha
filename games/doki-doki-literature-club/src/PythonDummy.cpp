#include "Ddlc/Python.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace Ddlc {

struct Interp::State {};

Interp::Interp() : state_(std::make_unique<State>()) {}
Interp::~Interp() = default;

Value Interp::eval(const std::string &, const ScopeP &) {
  throw std::runtime_error("Python not supported in WASM");
}

void Interp::exec(const std::string &, const ScopeP &) {
  throw std::runtime_error("Python not supported in WASM");
}

Value Interp::call(const Value &, Args, Kwargs) {
  throw std::runtime_error("Python not supported in WASM");
}

Value Interp::getAttr(const Object &, const std::string &) {
  throw std::runtime_error("Python not supported in WASM");
}

void Interp::setAttr(const Object &, const std::string &, const Value &) {
  throw std::runtime_error("Python not supported in WASM");
}

Value Interp::getItem(const Object &, const std::string &) {
  throw std::runtime_error("Python not supported in WASM");
}

void Interp::setItem(const Object &, const std::string &, const Value &) {
  throw std::runtime_error("Python not supported in WASM");
}

Value Interp::lookup(const std::string &, const ScopeP &) const {
  throw std::runtime_error("Python not supported in WASM");
}

bool Interp::has(const std::string &, const ScopeP &) const { return false; }

void Interp::set(const std::string &, const Value &) {
  throw std::runtime_error("Python not supported in WASM");
}

void Interp::def(const std::string &, NativeFn) {
  throw std::runtime_error("Python not supported in WASM");
}

void Interp::registerModule(const std::string &, const Value &) {
  throw std::runtime_error("Python not supported in WASM");
}

Value Interp::nameSpace(const std::string &) {
  throw std::runtime_error("Python not supported in WASM");
}

std::vector<uint32_t> utf8Decode(const std::string &) { return {}; }

std::string utf8Encode(const std::vector<uint32_t> &) { return ""; }

size_t utf8Length(const std::string &s) { return s.length(); }

PythonThreadRelease::PythonThreadRelease() {}
PythonThreadRelease::~PythonThreadRelease() {}

} // namespace Ddlc
