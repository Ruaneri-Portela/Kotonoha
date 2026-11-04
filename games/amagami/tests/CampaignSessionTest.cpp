#include <Amagami/Session.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void playEvent(Amagami::Session &session) {
  for (size_t step = 0; !session.ended() && step < 10000; ++step) {
    if (!session.choices().empty())
      session.choose(0);
    else if (session.waiting())
      session.tick(1);
    else
      session.advance();
  }
  require(session.ended(), "Original event reaches its campaign handoff");
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Expected original Amagami disc path");
    auto source = std::make_shared<Amagami::Source>(argv[1]);
    Amagami::Session session(source);
    session.startCampaign();
    playEvent(session);
    require(session.history().size() > 400,
            "Campaign plays the complete original prologue");
    require(session.campaignDate() == 1 && session.campaignPeriod() == 5,
            "Prologue continues at the original first-day map phase");
    const auto events = session.availableEvents();
    require(!events.empty(), "Original unlock requests offer campaign events");
    bool firstMeeting = false;
    for (const auto &event : events)
      firstMeeting |= event.name == "n1dea_ev01A";
    require(firstMeeting, "Original NodeOpenLV1 unlocks Tsukasa's first event");
    require(!session.campaignMapEvents().empty(),
            "Campaign map exposes original node positions and icon codes");
    const auto before = session.history().size();
    session.selectCampaignEvent("n1dea_ev01A");
    playEvent(session);
    require(session.history().size() > before,
            "First selected campaign event adds original dialogue");
    const auto unlocked = session.availableEvents();
    bool nextMeeting = false, excludedMeeting = false;
    for (const auto &event : unlocked) {
      nextMeeting |= event.name == "n1dea_ev04A";
      excludedMeeting |= event.name == "n1dea_ev01B";
    }
    require(nextMeeting && !excludedMeeting,
            "Executed branch opens and excludes the original follow-up nodes");
    std::cout
        << "Campaign prologue, map selection and original follow-up passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
