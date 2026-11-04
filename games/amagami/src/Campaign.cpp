#include <Amagami/Campaign.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <istream>
#include <limits>
#include <ostream>
#include <set>
#include <stdexcept>

namespace Amagami {
namespace {
VmValue klass(const std::string &name) {
  return VmValue::str(name, VmValue::Class);
}
const std::vector<VmValue> &array(const VmValue &value) {
  if (value.kind != VmValue::Array || !value.object)
    throw std::runtime_error("Malformed original campaign array");
  return value.object->fields;
}
int integer(const VmValue &value) {
  if (value.kind != VmValue::Number || !std::isfinite(value.number) ||
      value.number != std::trunc(value.number) ||
      value.number < std::numeric_limits<int>::min() ||
      value.number > std::numeric_limits<int>::max())
    throw std::runtime_error("Malformed original campaign integer");
  return int(value.number);
}
std::optional<VmValue> campaignNative(const VmValue &receiver,
                                      const std::string &selector,
                                      const std::vector<VmValue> &) {
  // These are console output and collection requests. Do not delegate to the
  // scenario presentation adapter: its generic setPos would turn EventNodes
  // into pictures instead of writing their original map positions.
  if (receiver.kind == VmValue::Class && receiver.text == "System" &&
      (selector == "print" || selector == "println" || selector == "gc" ||
       selector == "FullGC"))
    return receiver;
  return std::nullopt;
}
void write32(std::ostream &out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out.put(char(value >> (i * 8)));
}
uint32_t read32(std::istream &in) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) {
    const int byte = in.get();
    if (byte == std::char_traits<char>::eof())
      throw std::runtime_error("Truncated Amagami campaign save");
    value |= uint32_t(uint8_t(byte)) << (i * 8);
  }
  return value;
}
void writeValue(std::ostream &out, const VmValue &value, unsigned depth = 0) {
  if (depth > 16)
    throw std::runtime_error("Campaign save nesting exceeds limit");
  if (value.kind == VmValue::Nil) {
    out.put(0);
  } else if (value.kind == VmValue::Number) {
    const int n = integer(value);
    out.put(1);
    write32(out, uint32_t(int32_t(n)));
  } else if (value.kind == VmValue::Array) {
    const auto &items = array(value);
    if (items.size() > 100000)
      throw std::runtime_error("Campaign save array exceeds limit");
    out.put(2);
    write32(out, uint32_t(items.size()));
    for (const auto &item : items)
      writeValue(out, item, depth + 1);
  } else
    throw std::runtime_error("Unexpected value in original campaign save");
}
VmValue readValue(std::istream &in, size_t &budget, unsigned depth = 0) {
  if (depth > 16 || !budget--)
    throw std::runtime_error("Campaign save exceeds limits");
  const int kind = in.get();
  if (kind == 0)
    return {};
  if (kind == 1)
    return VmValue::num(int32_t(read32(in)));
  if (kind != 2)
    throw std::runtime_error("Invalid Amagami campaign save value");
  const uint32_t count = read32(in);
  if (count > budget || count > 100000)
    throw std::runtime_error("Campaign save array exceeds limit");
  auto object = std::make_shared<VmObject>();
  object->type = "Array";
  object->fields.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
    object->fields.push_back(readValue(in, budget, depth + 1));
  return VmValue::ref(std::move(object), VmValue::Array);
}
} // namespace

Campaign::Campaign(std::shared_ptr<Source> source)
    : source_(std::move(source)) {
  if (!source_)
    throw std::invalid_argument("Campaign requires the original game source");
}
VmValue Campaign::call(Vm &vm, const VmValue &receiver,
                       const std::string &selector,
                       std::vector<VmValue> arguments) {
  if (!vm.finished() || vm.paused())
    throw std::logic_error("Campaign call requires its independent idle stack");
  vm.call(receiver, selector, std::move(arguments));
  vm.run(10000000);
  if (!vm.finished() || vm.paused())
    throw std::runtime_error("Original campaign method did not finish: " +
                             vm.location());
  return vm.result();
}
Vm Campaign::worker(const Vm &live) {
  auto vm = live.fork(klass("EventNode"), "numOfNode");
  vm.native = campaignNative;
  vm.run(1000000);
  if (!vm.finished() || vm.paused())
    throw std::runtime_error("Cannot create independent campaign stack");
  return vm;
}
void Campaign::initialize(Vm &live) {
  // Precreate these class objects so forked initialization updates the same
  // fields seen by the live interpreter, including GameParam's relative day.
  live.classField("EventNode", 0);
  auto booted = VmValue::num(1);
  booted.kind = VmValue::Boolean;
  // Native Kotonoha bootstrap has already supplied the presentation objects;
  // K2X_Script.initialize must not re-enter the PS2 device setup from .new.
  live.classField("K2X_Script", 57) = booted;
  live.classField("GameParam", 12) = VmValue::num(1);
  live.classField("GameParam", 13) = VmValue::num(2);
  auto vm = live.fork(klass("EventNode"), "init");
  vm.native = campaignNative;
  vm.run(10000000);
  if (!vm.finished() || vm.paused())
    throw std::runtime_error("Original EventNode.init did not finish: " +
                             vm.location());
  initialized_ = true;
  rebuildCatalog(live);
  capture(live);
}
void Campaign::capture(Vm &vm) {
  if (!initialized_)
    return;
  fields_.clear();
  for (size_t slot = 0; slot <= 26; ++slot)
    fields_.push_back(vm.classField("EventNode", slot));
  gameFields_.clear();
  for (size_t slot = 12; slot <= 15; ++slot)
    gameFields_.push_back(vm.classField("GameParam", slot));
}
void Campaign::attach(Vm &vm) const {
  if (!initialized_)
    throw std::logic_error("Campaign has not been initialized");
  auto booted = VmValue::num(1);
  booted.kind = VmValue::Boolean;
  vm.classField("K2X_Script", 57) = booted;
  for (size_t slot = 0; slot < fields_.size(); ++slot)
    vm.classField("EventNode", slot) = fields_[slot];
  for (size_t slot = 0; slot < gameFields_.size(); ++slot)
    vm.classField("GameParam", slot + 12) = gameFields_[slot];
}
void Campaign::rebuildCatalog(Vm &vm) {
  events_.clear();
  nodes_.clear();
  std::function<void(const VmValue &)> visit = [&](const VmValue &value) {
    if (value.kind == VmValue::Array) {
      for (const auto &child : array(value))
        visit(child);
      return;
    }
    if (!value.object || value.object->type != "EventNode")
      return;
    const auto &fields = value.object->fields;
    const auto &parameters = array(fields.at(1));
    if (parameters.size() < 16 || parameters[0].kind != VmValue::Text)
      throw std::runtime_error("Malformed original EventNode parameters");
    Event event;
    event.name = parameters[0].asText();
    event.title = parameters[14].asText();
    event.message = parameters[15].asText();
    event.index = integer(fields.at(2));
    event.category = integer(fields.at(4));
    event.level = integer(fields.at(3));
    event.icon = integer(fields.at(10));
    event.firstDay = integer(parameters[6]);
    event.lastDay = integer(parameters[7]);
    if (fields[0].kind != VmValue::Nil) {
      const auto &position = array(fields[0]);
      if (position.size() != 2)
        throw std::runtime_error("Malformed original EventNode position");
      event.x = integer(position[0]);
      event.y = integer(position[1]);
    }
    if (!nodes_.emplace(event.name, value).second)
      throw std::runtime_error("Duplicate original EventNode: " + event.name);
    events_.push_back(std::move(event));
  };
  visit(vm.classField("EventNode", 5));
  schedule_ = vm.classField("EventNode", 8);
  if (array(schedule_).size() != 42)
    throw std::runtime_error("Original timetable must contain 42 days");
  for (const auto &day : array(schedule_))
    if (array(day).size() != 14)
      throw std::runtime_error("Original timetable must contain 14 periods");
}
const Campaign::Event *Campaign::event(const std::string &name) const {
  const auto found =
      std::find_if(events_.begin(), events_.end(),
                   [&](const auto &e) { return e.name == name; });
  return found == events_.end() ? nullptr : &*found;
}
VmValue Campaign::node(const std::string &name) const {
  const auto found = nodes_.find(name);
  return found == nodes_.end() ? VmValue{} : found->second;
}
std::vector<Campaign::Event> Campaign::available(const Vm &live) const {
  auto vm = worker(live);
  std::vector<Event> result;
  for (const auto &event : events_)
    if (call(vm, node(event.name), "isExecutable").truth()) {
      auto current = event;
      current.icon = integer(call(vm, node(event.name), "getIcon"));
      result.push_back(std::move(current));
    }
  return result;
}
std::vector<Campaign::Event> Campaign::mapEvents(const Vm &live) const {
  auto vm = worker(live);
  std::vector<Event> result;
  for (const auto &event : events_) {
    if (event.x < 0 || event.y < 0)
      continue;
    auto current = event;
    current.icon = integer(call(vm, node(event.name), "getIcon"));
    result.push_back(std::move(current));
  }
  return result;
}
Campaign::TimeEntry Campaign::scheduled(int day, int period) const {
  if (day < 1 || day > 42 || period < 0 || period >= 14)
    throw std::out_of_range("Invalid original campaign day/period");
  const auto &value = array(array(schedule_)[size_t(day - 1)])[size_t(period)];
  if (value.kind == VmValue::Text)
    return {TimeEntry::EventName, value.asText()};
  if (value.kind == VmValue::Symbol)
    return {TimeEntry::ScriptMethod, value.text};
  return {};
}
void Campaign::beginEvent(Vm &live, const std::string &name) {
  auto value = node(name);
  if (!value.object)
    throw std::out_of_range("Unknown campaign event: " + name);
  auto vm = worker(live);
  // K2X_Script.execEventChain sets lastNode before execEvent enables skip.
  call(vm, klass("EventNode"), "setLastNode", {value});
  call(vm, klass("GameParam"), "skipEnable");
  capture(live);
}
void Campaign::queueCompletion(Vm &live, const std::string &name) {
  auto value = node(name);
  if (!value.object)
    throw std::out_of_range("Unknown campaign event: " + name);
  auto vm = worker(live);
  auto off = VmValue::num(0);
  off.kind = VmValue::Boolean;
  call(vm, klass("GameParam"), "setSkipRead", {off});
  call(vm, klass("GameParam"), "skipDisable");
  // K2X_Script.execEvent does not mark type-0 free/viewer events as passed.
  if (integer(call(vm, value, "getType")) > 0)
    call(vm, klass("EventNode"), "pass", {value});
  capture(live);
}
void Campaign::recordSelection(Vm &live, const std::string &name) {
  if (!name.empty() && !node(name).object)
    throw std::out_of_range("Unknown campaign event: " + name);
  auto vm = worker(live);
  call(vm, klass("EventNode"), "setHistory",
       {name.empty() ? VmValue{} : VmValue::str(name)});
  capture(live);
}
void Campaign::open(Vm &live, const std::string &name) {
  auto value = node(name);
  if (!value.object)
    throw std::out_of_range("Unknown campaign event: " + name);
  auto vm = worker(live);
  call(vm, value, "openNT");
  capture(live);
}
void Campaign::pass(Vm &live, const std::string &name) {
  auto value = node(name);
  if (!value.object)
    throw std::out_of_range("Unknown campaign event: " + name);
  auto vm = worker(live);
  call(vm, value, "pass");
  capture(live);
}
void Campaign::fail(Vm &live, const std::string &name) {
  auto value = node(name);
  if (!value.object)
    throw std::out_of_range("Unknown campaign event: " + name);
  auto vm = worker(live);
  call(vm, value, "fail");
  capture(live);
}
void Campaign::finishPeriod(Vm &live) {
  auto vm = worker(live);
  call(vm, klass("EventNode"), "timeEnd");
  capture(live);
}
void Campaign::recordBlock(Vm &live, const std::string &name, int block) {
  const auto value = node(name);
  if (!value.object)
    throw std::out_of_range("Unknown campaign event: " + name);
  if (block < 0 || block > 100000)
    throw std::out_of_range("Invalid original scenario block");
  auto vm = worker(live);
  call(vm, vm.instance("GameMain"), "blockRead", {value, VmValue::num(block)});
  capture(live);
}
void Campaign::applyRequests(Vm &live) {
  auto vm = worker(live);
  // K2X_Script.updateNode processes pass, fail, then open requests. Its map
  // animation does not change those decisions; preserve every state guard.
  for (const auto &operation :
       {std::string("pass"), std::string("fail"), std::string("openNT")}) {
    const auto getter = operation == "pass"   ? "getPassReq"
                        : operation == "fail" ? "getFailReq"
                                              : "getOpenReq";
    auto queue = call(vm, klass("EventNode"), getter);
    size_t budget = 100000;
    while (integer(call(vm, queue, "length")) > 0) {
      if (!budget--)
        throw std::runtime_error(
            "Original campaign request queue exceeds limit");
      auto value = call(vm, queue, "get");
      if (!value.object || value.object->type != "EventNode")
        throw std::runtime_error("Non-node in original campaign request queue");
      if (operation == "pass") {
        if (!call(vm, value, "isOpen").truth() ||
            call(vm, value, "isBreak").truth())
          continue;
        call(vm, klass("EventNode"), "setLastPath", {value});
      } else if (operation == "fail") {
        if (call(vm, value, "isPass").truth() ||
            call(vm, value, "isLost").truth() ||
            call(vm, value, "isBreak").truth())
          continue;
        if (call(vm, value, "isOpen").truth())
          call(vm, klass("EventNode"), "setLastNode", {value});
      } else {
        if (!call(vm, value, "isClose").truth() ||
            call(vm, value, "isBreak").truth() ||
            call(vm, value, "isDrop").truth())
          continue;
        call(vm, klass("EventNode"), "setLastNode", {value});
      }
      call(vm, value, operation);
    }
  }
  capture(live);
}
void Campaign::completePrologue(Vm &live) {
  auto vm = worker(live);
  // GameMain.prolog queues NodeOpenLV1 and advances date 0 -> 1 before
  // execEventChain(prologue). initialize() already starts that event at 1;
  // never advance the relative day a second time when the event finishes.
  call(vm, vm.instance("GameMain"), "NodeOpenLV1");
  call(vm, klass("GameParam"), "setTime", {VmValue::num(5)});
  call(vm, klass("GameParam"), "setPlace", {VmValue::num(0)});
  // GameParam.setMorning serializes the full character/map state for the
  // original morning-retry feature. That checkpoint is owned by Session's
  // persistence integration; it must not serialize an incomplete bootstrap.
  VmValue flag = VmValue::num(0);
  flag.kind = VmValue::Boolean;
  call(vm, klass("GameParam"), "setProlog", {flag});
  applyRequests(live);
  capture(live);
}
void Campaign::advancePeriod(Vm &live) {
  auto vm = worker(live);
  call(vm, klass("EventNode"), "timeEnd");
  if (integer(call(vm, klass("GameParam"), "nextTime")) == 0) {
    call(vm, klass("GameParam"), "nextDay");
    call(vm, klass("GameParam"), "initMiyaFlag");
  }
  capture(live);
}
void Campaign::setDate(Vm &live, int day, int period) {
  if (day < 1 || day > 42 || period < 0 || period >= 14)
    throw std::out_of_range("Invalid original campaign day/period");
  live.classField("GameParam", 12) = VmValue::num(day);
  live.classField("GameParam", 13) = VmValue::num(period);
  capture(live);
}
int Campaign::date(const Vm &live) const {
  auto vm = worker(live);
  return integer(call(vm, klass("GameParam"), "getDate"));
}
int Campaign::period(const Vm &live) const {
  auto vm = worker(live);
  return integer(call(vm, klass("GameParam"), "getTime"));
}
void Campaign::save(const Vm &live, std::ostream &out) const {
  auto vm = worker(live);
  const auto body = call(vm, klass("EventNode"), "makeShioriBody");
  out.write("AMGC", 4);
  write32(out, 1);
  write32(out, uint32_t(integer(call(vm, klass("GameParam"), "getDate"))));
  write32(out, uint32_t(integer(call(vm, klass("GameParam"), "getTime"))));
  writeValue(out, body);
  if (!out)
    throw std::runtime_error("Cannot write Amagami campaign save");
}
void Campaign::load(Vm &live, std::istream &in) {
  char magic[4];
  if (!in.read(magic, 4) || std::memcmp(magic, "AMGC", 4) || read32(in) != 1)
    throw std::runtime_error("Invalid Amagami campaign save");
  const auto day = read32(in), time = read32(in);
  if (day < 1 || day > 42 || time >= 14)
    throw std::runtime_error("Invalid Amagami campaign save day/period");
  size_t budget = 200000;
  auto body = readValue(in, budget);
  if (array(body).size() != 5)
    throw std::runtime_error("Invalid original EventNode save body");
  // Validate all packed indices and map positions before invoking the original
  // restore loop, which otherwise could mutate some nodes before a later bad
  // entry fails. The envelope remains independent of commercial script text.
  const auto &parts = array(body);
  const auto &packedNodes = array(parts[0]);
  if (packedNodes.size() != events_.size())
    throw std::runtime_error(
        "Campaign save node count does not match this disc");
  std::set<int> indices, seen;
  for (const auto &event : events_)
    indices.insert(event.index);
  for (const auto &value : packedNodes) {
    const int encoded = integer(value);
    const int index = encoded / 1024;
    if (encoded < 0 || !indices.count(index) || !seen.insert(index).second ||
        (encoded % 1024) / 16 > 42)
      throw std::runtime_error("Invalid original campaign node save entry");
  }
  for (const auto &value : array(parts[1])) {
    const int index = integer(value);
    if (index != -1 && !indices.count(index))
      throw std::runtime_error("Invalid original campaign history entry");
  }
  const auto position = [](const VmValue &value) {
    if (value.kind == VmValue::Nil)
      return;
    const auto &point = array(value);
    if (point.size() != 2 || integer(point[0]) < 0 || integer(point[0]) >= 65 ||
        integer(point[1]) < 0 || integer(point[1]) >= 67)
      throw std::runtime_error("Invalid original campaign map position");
  };
  if (array(parts[2]).size() != 7)
    throw std::runtime_error("Invalid original campaign category positions");
  for (const auto &value : array(parts[2]))
    position(value);
  const int category = integer(parts[3]);
  if (category < 0 || category > 7)
    throw std::runtime_error("Invalid original campaign category");
  position(parts[4]);
  auto vm = worker(live);
  call(vm, klass("EventNode"), "restoreShioriBody", {body});
  setDate(live, int(day), int(time));
  capture(live);
}
} // namespace Amagami
