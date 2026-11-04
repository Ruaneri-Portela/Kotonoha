#include <Amagami/Session.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void require(bool c, const std::string &why) {
  if (!c)
    throw std::runtime_error(why);
}
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Expected disc path");
    auto source = std::make_shared<Amagami::Source>(argv[1]);
    require(source->scenarios().size() == 2308, "All scenarios parsed");
    require(source->classes().size() == 222, "All system classes parsed");
    require(source->find("GRAPH/GRAPH1.ARC", "bg/122_10.tm2") == 723,
            "Native ARC filename hash");
    for (size_t i = 0; i < source->scenarios().size(); ++i)
      require(source->find("SCENARIO.ARC", source->scenarios()[i] + ".scf") ==
                  int(i),
              "Scenario hash round trip");
    require(source->actorAssets(7, 0, 1) ==
                std::make_pair(std::string("stand/b7_Bax.tm2"),
                               std::string("stand/f7_Bx.tm2")),
            "Native table selects uppercase pose and lowercase costume");
    require(
        source->breathDirection(7, 1) == 2 &&
            source->breathDirection(7, 2) == 1,
        "Cold-breath direction comes from the original CharParam pose table");
    require(source->actorAssets(7, 0, 3).second == "stand/f7_Ax.tm2",
            "Alternate pose reuses original face mapping");
    const auto event = source->eventPicture(6, 24, 0);
    require(event == "epi/e6_24a0.tm2" && source->graphic(event).has_value(),
            "EventPicture uses original epi path and frame suffix");
    require(source->itemPicture(170) == "item/170.tm2" &&
                source->graphic("sysgraph/baseitem.tm2").has_value(),
            "Native ItemView resolves its item and frame on the disc");
    require(!source->voice("MIY", 1858).empty(),
            "Nested AFS voice name lookup");
    require(source->voice("XYZ", 9999).empty(), "Missing voice lookup");
    auto tmp = std::filesystem::temp_directory_path() /
               ("amagami-test-" + std::to_string(SDL_GetPerformanceCounter()) +
                ".txt");
    struct Cleanup {
      std::filesystem::path p;
      ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove(p, ec);
      }
    } cleanup{tmp};
    // Scalar dispatch must preserve SCF classes and their inheritance.
    {
      Amagami::Vm probe;
      for (size_t i = 0; i < source->classes().size(); ++i)
        probe.add(source->systemClass(i));
      for (const auto value :
           {Amagami::VmValue::num(7), Amagami::VmValue::num(7.5, true)}) {
        probe.call(value, "isKindOf",
                   {Amagami::VmValue::str("Integer", Amagami::VmValue::Class)});
        probe.run();
        require(
            probe.result().kind == Amagami::VmValue::Boolean &&
                probe.result().truth() == !value.floating,
            "isKindOf distinguishes Integer from Float without empty dispatch");
        probe.call(value, "isKindOf",
                   {Amagami::VmValue::str("Number", Amagami::VmValue::Class)});
        probe.run();
        require(probe.result().truth(), "SCF numeric inheritance is respected");
      }
      probe.native = [](const auto &r, const std::string &selector,
                        const auto &) -> std::optional<Amagami::VmValue> {
        return selector == "println" ? std::optional<Amagami::VmValue>(r)
                                     : std::nullopt;
      };
      for (const auto value :
           {Amagami::VmValue::num(7), Amagami::VmValue::num(7.5, true)}) {
        probe.call(probe.instance("n0_MacroScripts"), "actTime", {value});
        probe.run();
        require(probe.result().number == value.number &&
                    probe.result().floating == value.floating,
                "Original actTime initializes its multiplier and preserves "
                "numeric type");
      }
    }
    // Exercise the actual seven-argument macro reported by the runtime.
    for (int immediate : {0, 1}) {
      Amagami::Vm probe;
      for (size_t i = 0; i < source->classes().size(); ++i)
        probe.add(source->systemClass(i));
      int positions = 0, actions = 0, moves = 0;
      Amagami::VmValue action;
      probe.native = [&](const Amagami::VmValue &receiver,
                         const std::string &selector,
                         const std::vector<Amagami::VmValue> &args)
          -> std::optional<Amagami::VmValue> {
        if (selector == "move") {
          ++moves;
          return receiver;
        }
        if (selector == "exsleep" || selector == "println")
          return receiver;
        if (selector == "setPos") {
          ++positions;
          return receiver;
        }
        if (receiver.kind == Amagami::VmValue::Class &&
            receiver.text == "Thread" && selector == "new")
          return probe.instance("Thread");
        if (receiver.object && receiver.object->type == "Thread" &&
            selector == "start") {
          require(args.size() == 1 && args[0].object &&
                      args[0].object->type == "n0_Action",
                  "laugh starts its original n0_Action coroutine");
          action = args[0];
          ++actions;
          return receiver;
        }
        return std::nullopt;
      };
      auto flag = [](bool enabled) {
        auto value = Amagami::VmValue::num(enabled);
        value.kind = Amagami::VmValue::Boolean;
        return value;
      };
      for (const auto &[slot, enabled] :
           std::vector<std::pair<int, bool>>{{5, false},
                                             {6, true},
                                             {18, false},
                                             {19, false},
                                             {20, false},
                                             {21, false}})
        probe.classField("GameParam", size_t(slot)) = flag(enabled);
      probe.call(probe.instance("n0_MacroScripts"), "laugh",
                 {probe.instance("Actor"), Amagami::VmValue::num(10),
                  Amagami::VmValue::num(20), Amagami::VmValue::num(1),
                  Amagami::VmValue::num(2),
                  Amagami::VmValue::num(immediate ? 0 : 1, true),
                  Amagami::VmValue::num(1)});
      probe.run();
      require(probe.finished() && (immediate ? positions == 1 : actions == 1),
              "Original laugh macro reaches its immediate and animated paths");
      if (!immediate) {
        auto child = probe.fork(action, "run");
        child.run();
        require(child.finished() && moves > 0,
                "Original laugh coroutine executes actor movement");
      }
    }
    Amagami::Session itemScene(source);
    itemScene.setRealtime(true);
    itemScene.start("n3nka_st01A");
    for (int i = 0; i < 100 && !itemScene.visuals().count("ITEM_ICON"); ++i) {
      itemScene.tick(1);
      if (!itemScene.waiting())
        itemScene.advance();
    }
    require(itemScene.visuals().count("ITEM_ICON") && itemScene.waiting(),
            "Retail script opens ItemView and waits for its animation");
    require(itemScene.visuals().at("ITEM_ICON").file == "item/220.tm2" &&
                itemScene.visuals().at("ITEM_ICON").scaleX == 0,
            "ItemView starts with the original small image at zero scale");
    itemScene.tick(0.1);
    require(itemScene.visuals().at("ITEM_ICON").scaleX > 0 &&
                itemScene.visuals().at("ITEM_ICON").scaleX < 1,
            "ItemView interpolates the opening over twelve frames");
    itemScene.tick(0.1);
    require(itemScene.visuals().at("ITEM_ICON").scaleX == 1 &&
                itemScene.visuals().at("ITEM_BASE").scaleX == 1.5f,
            "ItemView reaches the native icon and frame scales");
    Amagami::Session timed(source);
    timed.setRealtime(true);
    timed.start();
    require(timed.waiting() && timed.history().empty(),
            "Original opening sleep suspends VM");
    auto at = timed.location();
    timed.advance();
    require(timed.location() == at, "Confirm does not bypass scripted wait");
    timed.tick(0.5);
    require(timed.waiting() && timed.history().empty(),
            "Half-second retains opening delay");
    for (size_t i = 0; timed.waiting() && i < 100; ++i)
      timed.tick(1);
    require(timed.messageVisible() && timed.history().size() == 1,
            "Timer resumes at first dialogue");
    const auto camera = timed.visuals().at("BG");
    require(camera.scaleX > 1 && camera.scaleY > 1 && camera.angle == 0.5f,
            "Opening uses scripted zoom and quarter-turn rotation");
    timed.tick(2);
    const auto moved = timed.visuals().at("BG");
    require(
        moved.y < camera.y && moved.scaleX == camera.scaleX &&
            moved.scaleY == camera.scaleY,
        "Original camera thread pans during dialogue without collapsing scale");
    require(timed.history().size() == 1,
            "Camera movement does not advance dialogue");
    timed.advance();
    require(timed.waiting() && !timed.messageVisible(),
            "Script hides window between narration pages");
    std::vector<std::string> branchTexts;
    bool sawPoseTransition = false, sawBreath = false, sawInlinePose = false,
         sawVibration = false;
    for (size_t branch = 0; branch < 3; ++branch) {
      Amagami::Session session(source);
      session.start();
      size_t steps = 0, choices = 0;
      while (!session.ended() && steps++ < 2000) {
        if (!session.choices().empty()) {
          require(session.choices().size() == 3,
                  "Three original prologue answers");
          session.save(tmp.string());
          auto location = session.location();
          Amagami::Session restored(source);
          restored.load(tmp.string());
          require(restored.location() == location &&
                      restored.choices().size() == 3,
                  "Restore pending choice");
          session.choose(branch);
          branchTexts.push_back(session.dialogue().text);
          ++choices;
          session.save(tmp.string());
          restored.load(tmp.string());
          require(restored.dialogue().text == session.dialogue().text &&
                      restored.location() == session.location(),
                  "Restore selected branch");
        } else
          session.advance();
      }
      require(session.ended(), "Prologue reaches original EoS");
      require(choices == 1, "Original prologue choice count");
      require(session.history().size() > 400, "Complete prologue dialogue");
      Amagami::Session animated(source);
      animated.setRealtime(true);
      animated.start();
      size_t ticks = 0;
      while (!animated.ended() && ticks++ < 30000) {
        if (animated.waiting())
          animated.tick(1);
        else if (!animated.choices().empty())
          animated.choose(branch);
        else {
          animated.tick(0.2); // camera/blink continue during a dialogue pause
          animated.advance();
        }
        if (!animated.actorTransitions().empty())
          sawPoseTransition = true;
        for (const auto &cue : animated.dialogue().cues)
          if (cue.pose >= 0)
            sawInlinePose = true;
        for (const auto &[name, visual] : animated.visuals()) {
          if (visual.breathAge >= 0)
            sawBreath = true;
          if (visual.positionVibration.targetX != 0 ||
              visual.scaleVibration.targetY != 0)
            sawVibration = true;
          if (!visual.visible || visual.file.rfind("stand/b", 0) != 0)
            continue;
          require(source->find("GRAPH/GRAPH2.ARC", visual.file) >= 0 &&
                      source->find("GRAPH/GRAPH2.ARC", visual.face) >= 0,
                  "Original body/face assets: " + visual.file + " " +
                      visual.face + " at " + animated.location());
          require(visual.scaleX > 0 && visual.scaleY > 0,
                  "Actor zoom keeps both axes visible");
        }
      }
      require(animated.ended() &&
                  animated.history().size() == session.history().size(),
              "Animated original threads reach same prologue ending and "
              "dialogue count");
      std::cout << "Branch " << branch << ": " << session.history().size()
                << " dialogue pages, EoS\n";
    }
    require(sawVibration,
            "Original shiver/scale oscillation commands are scheduled");
    require(sawPoseTransition,
            "Original pose/costume transitions run over time");
    require(sawBreath, "Actor setBreath enables cold-breath particles on "
                       "original voiced lines");
    require(sawInlinePose,
            "Original inline P commands schedule body-pose changes");
    require(Amagami::DisplayText("V1897まぁFm3まぁ、W15Wv80EoFe9Fm4そんなに") ==
                "まぁまぁ、そんなに",
            "Nested original commands retain only display text");
    require(Amagami::DisplayText("V1884Mo…W15…W15F9Mc") == "……",
            "Inline mouth commands are hidden from text");
    require(branchTexts[0] != branchTexts[1] &&
                branchTexts[1] != branchTexts[2],
            "Choices execute distinct original blocks");
    require(Amagami::DisplayText("V1234helloW15／world") == "hello\nworld",
            "Text controls removed");
    require(Amagami::DisplayText("R美也R2みや") == "美也",
            "Ruby base retained");
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
