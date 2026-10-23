// The standalone KTRF test projects never mount retail GPKs nor configure a
// --sd-script-dir override, so the script-directory lookup used by
// SchoolDaysKtrfSession always misses, exactly like the app without the option.
#include <Kotonoha/SchoolDaysAssetMount.hpp>

namespace Kotonoha {

bool ResolveSchoolDaysScriptDirectoryFile(std::string_view, std::string *) {
  return false;
}

} // namespace Kotonoha
