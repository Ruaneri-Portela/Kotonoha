#include <Kotonoha/integrations/RichPresence.hpp>

namespace Kotonoha::Integrations {
bool desktopPresenceAvailable() noexcept { return false; }
std::unique_ptr<PresenceTransport> discordDesktop(std::string, std::string) {
  return {};
}
} // namespace Kotonoha::Integrations
