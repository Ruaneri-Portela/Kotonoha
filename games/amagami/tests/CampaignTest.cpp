#include <Amagami/Campaign.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Expected original Amagami disc path");
    auto source = std::make_shared<Amagami::Source>(argv[1]);
    Amagami::Vm vm;
    for (size_t i = 0; i < source->classes().size(); ++i)
      vm.add(source->systemClass(i));
    // Heroine.isDropout consults the campaign penalty; the native Session
    // bootstrap supplies these same real-class objects and registry.
    auto heroines = std::make_shared<Amagami::VmObject>();
    heroines->type = "Array";
    for (int i = 0; i < 8; ++i) {
      auto heroine = vm.instance("Heroine");
      heroine.object->fields[32] = Amagami::VmValue::num(0);
      auto clear = std::make_shared<Amagami::VmObject>();
      clear->type = "Array";
      clear->fields.resize(6, Amagami::VmValue::num(0));
      heroine.object->fields[44] =
          Amagami::VmValue::ref(clear, Amagami::VmValue::Array);
      if (i == 0)
        vm.classField("K2X_Script", 17) = heroine;
      if (i == 3)
        vm.classField("K2X_Script", 20) = heroine;
      heroines->fields.push_back(heroine);
    }
    vm.classField("Heroine", 5) =
        Amagami::VmValue::ref(heroines, Amagami::VmValue::Array);
    Amagami::Campaign campaign(source);
    campaign.initialize(vm);
    require(campaign.events().size() == 2023,
            "EventNode.init builds all 2023 original campaign nodes");
    require(campaign.date(vm) == 1 && campaign.period(vm) == 2,
            "Campaign starts at original relative date and prologue period");
    const auto *prologue = campaign.event("n0dea_ev01A");
    require(prologue && prologue->x >= 0 && prologue->y >= 0,
            "Prologue uses the original node map coordinates");
    require(!campaign.event("n9xyz_ev99Z"),
            "Unknown campaign node stays absent");
    const auto scheduled = campaign.scheduled(1, 8);
    require(scheduled.kind == Amagami::Campaign::TimeEntry::EventName &&
                scheduled.name == "n6dea_ev02B",
            "Original first-day timetable identifies a real scenario");
    const auto method = campaign.scheduled(1, 7);
    require(method.kind == Amagami::Campaign::TimeEntry::ScriptMethod &&
                method.name == "SanTaku",
            "Script-method timetable entries are preserved as methods");
    require(Amagami::Campaign::holiday(7) && !Amagami::Campaign::holiday(8) &&
                Amagami::Campaign::holiday(41),
            "Original relative-day holiday rule is preserved");
    campaign.open(vm, "n0dea_ev01A");
    auto node = campaign.node("n0dea_ev01A");
    require(node.object->fields[5].number == 1 &&
                node.object->fields[6].number == 1,
            "Original openNT marks a newly opened node until period end");
    campaign.finishPeriod(vm);
    require(node.object->fields[6].number == 2,
            "Original timeEnd advances the opening marker");
    campaign.setDate(vm, 2, 5);
    campaign.open(vm, "n1dea_ev01A");
    auto available = campaign.available(vm);
    bool heroineAvailable = false;
    for (const auto &event : available)
      heroineAvailable |= event.name == "n1dea_ev01A";
    require(heroineAvailable,
            "Original isExecutable offers opened first-day heroine event");
    std::stringstream save(std::ios::in | std::ios::out | std::ios::binary);
    campaign.save(vm, save);
    campaign.pass(vm, "n0dea_ev01A");
    require(node.object->fields[5].number == 2,
            "Original pass changes only the selected node state");
    save.seekg(0);
    campaign.load(vm, save);
    require(
        node.object->fields[5].number == 1 && campaign.date(vm) == 2 &&
            campaign.period(vm) == 5,
        "Original Shiori packing restores node state and campaign position");
    campaign.recordBlock(vm, "n1dea_ev01A", 0);
    campaign.applyRequests(vm);
    require(
        campaign.node("n1dea_ev04A").object->fields[5].number == 1 &&
            campaign.node("n1dea_ev01B").object->fields[6].kind ==
                Amagami::VmValue::Number &&
            campaign.node("n1dea_ev01B").object->fields[6].number == 0,
        "Original blockRead opens and breaks exactly the table transitions");
    campaign.capture(vm);
    vm.reset();
    campaign.attach(vm);
    require(campaign.date(vm) == 2 && campaign.period(vm) == 5 &&
                campaign.node("n0dea_ev01A").object == node.object,
            "Campaign survives scenario interpreter resets");
    vm.classField("Heroine", 5) =
        Amagami::VmValue::ref(heroines, Amagami::VmValue::Array);
    vm.classField("K2X_Script", 17) = heroines->fields[0];
    vm.classField("K2X_Script", 20) = heroines->fields[3];
    campaign.setDate(vm, 1, 2);
    campaign.completePrologue(vm);
    require(campaign.date(vm) == 1 && campaign.period(vm) == 5 &&
                campaign.node("n2dea_ev01A").object->fields[5].number == 1,
            "Original NodeOpenLV1 and prologue setup unlock the first campaign "
            "day");
    campaign.beginEvent(vm, "n2dea_ev01A");
    require(vm.classField("EventNode", 19).number == 2,
            "Beginning a map event preserves the original selected category");
    std::cout
        << "Original campaign catalog, scheduling, nodes and saves passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
