#pragma once

#include <Amagami/Source.hpp>
#include <Amagami/Vm.hpp>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Amagami {

// Campaign data is constructed by EventNode.init from the supplied NodeTable,
// rather than by a replacement schedule or an inferred list of scenario names.
class Campaign {
public:
  struct Event {
    std::string name, title, message;
    int index = 0, category = 0, level = 0, icon = 0;
    int x = -1, y = -1;
    int firstDay = 0, lastDay = 0;
  };
  struct TimeEntry {
    enum Kind { None, EventName, ScriptMethod } kind = None;
    std::string name;
  };

  explicit Campaign(std::shared_ptr<Source> source);
  void initialize(Vm &vm);
  bool initialized() const { return initialized_; }
  // Before Session resets its interpreter, capture shared campaign fields;
  // attach restores them after its normal presentation/character bootstrap.
  void capture(Vm &vm);
  void attach(Vm &vm) const;
  const std::vector<Event> &events() const { return events_; }
  const Event *event(const std::string &name) const;
  VmValue node(const std::string &name) const;
  std::vector<Event> available(const Vm &vm) const;
  std::vector<Event> mapEvents(const Vm &vm) const;
  TimeEntry scheduled(int day, int period) const;

  // All these operations execute the original SCF methods, on an independent
  // call stack sharing the live VM's class and object state.
  void beginEvent(Vm &vm, const std::string &name);
  void queueCompletion(Vm &vm, const std::string &name);
  void recordSelection(Vm &vm, const std::string &name);
  void open(Vm &vm, const std::string &name);
  void pass(Vm &vm, const std::string &name);
  void fail(Vm &vm, const std::string &name);
  void finishPeriod(Vm &vm);
  // execEvent calls blockRead after every returned script block; this executes
  // the branch-specific unlock/break transitions stored in NodeTable[16].
  void recordBlock(Vm &vm, const std::string &name, int block);
  // Data changes from K2X_Script.updateNode, in its pass/fail/open order.
  // Cursor motion, sound and confirmUpdate belong to the map presentation.
  void applyRequests(Vm &vm);
  // GameMain.prolog's post-event setup, using NodeOpenLV1 bytecode.
  void completePrologue(Vm &vm);
  void advancePeriod(Vm &vm);
  void setDate(Vm &vm, int day, int period);
  int date(const Vm &vm) const;
  int period(const Vm &vm) const;
  static bool holiday(int day) { return day > 40 || day % 7 == 0; }

  // Original EventNode.makeShioriBody/restoreShioriBody packing, with a bounded
  // portable envelope containing no retail script text or texture data.
  void save(const Vm &vm, std::ostream &out) const;
  void load(Vm &vm, std::istream &in);

  // True when every EventNode in the original NodeTable has been marked passed
  // by the original interpreter (isPass on each node).
  bool campaignCleared(Vm &vm) const;

  // Independent call stack sharing the live VM's class/object state, used to
  // query original EventNode methods without mutating presentation state.
  static Vm workerPublic(const Vm &live) { return worker(live); }

private:
  std::shared_ptr<Source> source_;
  bool initialized_ = false;
  std::vector<Event> events_;
  std::map<std::string, VmValue> nodes_;
  std::vector<VmValue> fields_;
  std::vector<VmValue> gameFields_;
  VmValue schedule_;
  static VmValue call(Vm &, const VmValue &, const std::string &,
                      std::vector<VmValue> = {});
  static Vm worker(const Vm &);
  void rebuildCatalog(Vm &);
};

} // namespace Amagami
