#pragma once

#include <atomic>

// Runs an ImGui widget on a local copy of an atomic value and stores it back
// only when the widget reports a change.
template <class T, class Widget>
bool EditAtomic(std::atomic<T> &value, Widget &&widget) {
  T local = value.load();
  if (widget(&local)) {
    value.store(local);
    return true;
  }
  return false;
}
