#include <Kotonoha/Gameplay.hpp>

namespace Kotonoha {

// The standalone bridge gate deliberately uses a tiny fake Gameplay factory
// instead of linking the complete renderer/media runtime.  Keep only the two
// out-of-line symbols now referenced by SchoolDaysKtrfGameplayBridge itself.
// Production builds always resolve these from the real runtime objects.
Prompt::~Prompt() = default;

void Gameplay::SetUserPaused(Kotonoha_Game*, bool) {}

} // namespace Kotonoha
