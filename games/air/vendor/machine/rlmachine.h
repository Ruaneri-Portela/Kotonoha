// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include "libreallive/bytecode.h"
#include "machine/reference.h"
#include <functional>
#include <string>
class RLMachine {
  RLMemory memory_;
  int store_ = 0;

public:
  std::function<void(const libreallive::CommandElement &)> onCommand;
  std::function<void(std::string)> onText;
  std::function<void()> onAdvance;
  int lineNumber = 0, kidokuMarker = 0;
  RLMemory &memory() { return memory_; }
  const RLMemory &memory() const { return memory_; }
  int GetIntValue(const libreallive::IntMemRef &r) const {
    return memory_.GetIntValue(r);
  }
  void SetIntValue(const libreallive::IntMemRef &r, int v) {
    memory_.SetIntValue(r, v);
  }
  const std::string &GetStringValue(int b, int i) {
    return memory_.GetStringValue(b, i);
  }
  void SetStringValue(int b, int i, const std::string &v) {
    memory_.SetStringValue(b, i, v);
  }
  int store_register() const { return store_; }
  void set_store_register(int v) { store_ = v; }
  int *store_register_address() { return &store_; }
  void AdvanceInstructionPointer() {
    if (onAdvance)
      onAdvance();
  }
  void SetLineNumber(int n) { lineNumber = n; }
  void SetKidokuMarker(int n) { kidokuMarker = n; }
  void PerformTextout(const libreallive::TextoutElement &e) {
    if (onText)
      onText(e.GetText());
  }
  void ExecuteExpression(const libreallive::ExpressionElement &e) {
    e.ParsedExpression().GetIntegerValue(*this);
    AdvanceInstructionPointer();
  }
  void ExecuteCommand(const libreallive::CommandElement &e) {
    if (onCommand)
      onCommand(e);
    else
      AdvanceInstructionPointer();
  }
  std::string GetCommandName(const libreallive::CommandElement &e) {
    return std::to_string(e.modtype()) + ":" + std::to_string(e.module()) +
           ":" + std::to_string(e.opcode());
  }
};
