#include <Amagami/Session.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
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
    // Save a partially played prologue; the journal must restore the
    // original SCF instruction position, not start the scenario again.
    const auto savePath =
        std::filesystem::temp_directory_path() /
        ("kotonoha-amagami-campaign-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".txt");
    struct RemoveSave {
      std::filesystem::path path;
      ~RemoveSave() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
      }
    } removeSave{savePath};
    for (unsigned n = 0; n < 12 && !session.ended(); ++n) {
      if (!session.choices().empty())
        session.choose(0);
      else if (session.waiting())
        session.tick(1);
      else
        session.advance();
    }
    require(!session.history().empty(),
            "Mid-prologue campaign save has actual dialogue");
    session.save(savePath.string());
    Amagami::Session resumed(source);
    resumed.load(savePath.string());
    require(resumed.campaignActive() && resumed.name() == session.name() &&
                resumed.history().size() == session.history().size() &&
                resumed.dialogue().original == session.dialogue().original &&
                resumed.campaignDate() == session.campaignDate() &&
                resumed.campaignPeriod() == session.campaignPeriod(),
            "Campaign save reloads exactly to the last prologue page");
    // A second save after loading must retain the full action journal.
    resumed.save(savePath.string());
    Amagami::Session again(source);
    again.load(savePath.string());
    require(again.history().size() == resumed.history().size() &&
                again.name() == resumed.name(),
            "Loaded campaign preserves its journal for repeated saves");
    playEvent(session);
    // Force thousands of SCF instructions *after* the temporary candidate
    // from load() has died. This exercises the rebound shouldYield callback
    // and verifies that a twice-restored save can continue playing.
    playEvent(again);
    require(again.history().size() == session.history().size() &&
                again.campaignDate() == session.campaignDate() &&
                again.campaignPeriod() == session.campaignPeriod(),
            "Post-load VM runs original prologue with live Session callbacks");
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
    // Save while the first map event is running, then restore to the
    // original story position without blindly selecting choice zero.
    for (unsigned n = 0; n < 5 && !session.ended(); ++n) {
      if (!session.choices().empty())
        session.choose(0);
      else if (session.waiting())
        session.tick(1);
      else
        session.advance();
    }
    session.save(savePath.string());
    Amagami::Session mapReload(source);
    mapReload.load(savePath.string());
    require(mapReload.campaignActive() && mapReload.name() == session.name() &&
                mapReload.history().size() == session.history().size() &&
                mapReload.campaignDate() == session.campaignDate() &&
                mapReload.campaignPeriod() == session.campaignPeriod(),
            "Reloaded map event retains all previous original campaign flags");
    // A pre-existing Session must not keep its old synthetic ControlPad or
    // presentation clocks when replaced by a different campaign checkpoint.
    // The freshly loaded and reused instances replayed the same SCF inputs.
    Amagami::Session reused(source);
    reused.startCampaign();
    reused.tick(2.0);
    require(reused.presentationSeconds() >= 2.0,
            "Existing campaign has an unrelated animation clock");
    reused.load(savePath.string());
    require(reused.history().size() == mapReload.history().size() &&
                reused.campaignDate() == mapReload.campaignDate() &&
                reused.campaignPeriod() == mapReload.campaignPeriod() &&
                std::abs(reused.presentationSeconds() -
                         mapReload.presentationSeconds()) < 0.000001,
            "Quick-load replaces previous input and presentation clocks");
    reused.tick(1.0 / 60.0);
    mapReload.tick(1.0 / 60.0);
    require(std::abs(reused.presentationSeconds() -
                     mapReload.presentationSeconds()) < 0.000001,
            "Restored campaign clocks remain synchronized after another tick");
    playEvent(session);
    playEvent(mapReload);
    require(mapReload.history().size() == session.history().size() &&
                mapReload.campaignDate() == session.campaignDate() &&
                mapReload.campaignPeriod() == session.campaignPeriod(),
            "Loaded map event runs to the same ending as uninterrupted PS2");
    require(session.history().size() > before,
            "First selected campaign event adds original dialogue");
    // n1dea_ev04A is scheduled for day 2, period 5. The prologue leaves the
    // calendar at day 1, period 5, so advance a full day (14 periods) before
    // checking that the executed branch opened the follow-up node.
    for (unsigned i = 0; i < 14; ++i) {
      const int beforeDay = session.campaignDate();
      const int beforePeriod = session.campaignPeriod();
      session.nextCampaignPeriod();
      require(session.campaignDate() > beforeDay ||
                  (session.campaignDate() == beforeDay &&
                   session.campaignPeriod() > beforePeriod),
              "Original PS2 campaign map advances its calendar period");
      require(session.campaignDate() >= 1 && session.campaignDate() <= 42 &&
                  session.campaignPeriod() >= 0 &&
                  session.campaignPeriod() < 14,
              "Campaign map dates remain inside the original six-week span");
    }
    const auto unlocked = session.availableEvents();
    bool nextMeeting = false, excludedMeeting = false;
    for (const auto &event : unlocked) {
      nextMeeting |= event.name == "n1dea_ev04A";
      excludedMeeting |= event.name == "n1dea_ev01B";
    }
    require(nextMeeting && !excludedMeeting,
            "Executed branch opens and excludes the original follow-up nodes");
    session.save(savePath.string());
    Amagami::Session calendarResume(source);
    calendarResume.load(savePath.string());
    require(calendarResume.campaignActive() &&
                calendarResume.campaignDate() == session.campaignDate() &&
                calendarResume.campaignPeriod() == session.campaignPeriod() &&
                calendarResume.history().size() == session.history().size(),
            "Calendar period checkpoint reloads into the correct original map");
    const auto eventNames = [](const Amagami::Session &current) {
      std::set<std::string> result;
      for (const auto &event : current.availableEvents())
        result.insert(event.name);
      return result;
    };
    require(eventNames(calendarResume) == eventNames(session),
            "Calendar save restores exactly the original unlockable nodes");
    // Corrupted action journals must be rejected before touching the live
    // Session, including a syntactically valid but mismatched choice.
    const auto corruptedPath =
        std::filesystem::path(savePath.string() + ".corrupted");
    RemoveSave removeCorrupted{corruptedPath};
    std::ifstream originalSave(savePath, std::ios::binary);
    require(bool(originalSave), "Open original PS2 calendar checkpoint");
    const std::string originalBytes(
        (std::istreambuf_iterator<char>(originalSave)),
        std::istreambuf_iterator<char>());
    const auto rejectsCorruption = [&](const std::string &oldAction,
                                       const std::string &replacementAction) {
      std::string damaged = originalBytes;
      const auto at = damaged.find(oldAction);
      require(at != std::string::npos, "Expected replay action exists");
      damaged.replace(at, oldAction.size(), replacementAction);
      {
        std::ofstream output(corruptedPath, std::ios::binary);
        require(bool(output), "Write intentionally corrupt replay");
        output << damaged;
        require(bool(output), "Flush corrupt replay");
      }
      const auto oldDate = calendarResume.campaignDate();
      const auto oldPeriod = calendarResume.campaignPeriod();
      const auto oldEvents = eventNames(calendarResume);
      bool rejected = false;
      try {
        calendarResume.load(corruptedPath.string());
      } catch (const std::exception &) {
        rejected = true;
      }
      require(rejected, "Reject structurally invalid PS2 campaign replay");
      require(calendarResume.campaignDate() == oldDate &&
                  calendarResume.campaignPeriod() == oldPeriod &&
                  eventNames(calendarResume) == oldEvents,
              "Invalid journal leaves the active campaign unchanged");
    };
    rejectsCorruption("\"period\" \"\"", "\"period\" \"tampered\"");
    rejectsCorruption("\"advance\" \"\"", "\"choose\" \"0\"");
    // Continue native period transitions after the temporary load session
    // has been destroyed; catch stale pointers and replay journal drift.
    for (unsigned i = 0; i < 8; ++i) {
      session.nextCampaignPeriod();
      calendarResume.nextCampaignPeriod();
      require(calendarResume.campaignDate() == session.campaignDate() &&
                  calendarResume.campaignPeriod() == session.campaignPeriod(),
              "Saved PS2 calendar stays synchronized after further periods");
    }
    require(eventNames(calendarResume) == eventNames(session),
            "Later-day available events match uninterrupted campaign state");
    std::cout
        << "Campaign prologue, map selection and original follow-up passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
