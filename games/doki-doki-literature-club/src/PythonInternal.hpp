#pragma once

#include "Ddlc/Value.hpp"

namespace Ddlc {

bool pythonTruthy(const PythonValue& value);
std::string pythonString(const PythonValue& value, bool repr);
const Value* pythonAttribute(Object& object, const std::string& name);
void pythonRefreshItems(Object& object);
void pythonRefreshDict(Object& object);

} // namespace Ddlc
