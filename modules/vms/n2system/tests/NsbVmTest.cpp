// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/NsbScript.hpp>
#include <N2System/NsbVm.hpp>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;

void require(bool cond, const char *msg) {
  if (!cond)
    throw std::runtime_error(msg);
}

void put16(Bytes &b, std::uint16_t v) {
  b.push_back(static_cast<std::uint8_t>(v & 0xFF));
  b.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(Bytes &b, std::uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

Bytes utf16(std::string_view ascii) {
  Bytes out;
  for (unsigned char c : ascii) {
    out.push_back(c);
    out.push_back(0);
  }
  return out;
}

void addInsn(Bytes &nsb, std::uint32_t line, std::uint16_t op,
             const std::vector<std::string> &args) {
  put32(nsb, line);
  put16(nsb, op);
  put16(nsb, static_cast<std::uint16_t>(args.size()));
  for (const auto &a : args) {
    const auto u = utf16(a);
    put32(nsb, static_cast<std::uint32_t>(u.size()));
    nsb.insert(nsb.end(), u.begin(), u.end());
  }
}

void addMap(Bytes &map, std::uint32_t off, std::string_view name) {
  const auto u = utf16(name);
  put32(map, off);
  put16(map, static_cast<std::uint16_t>(u.size()));
  map.insert(map.end(), u.begin(), u.end());
}
} // namespace

int main() {
  // 1. Test UTF-16LE system.ini parsing
  {
    const std::string iniText =
        "[product]\r\nname=STEINS;GATE\r\nstart=nss/0_boot.nss\r\n"
        "bgm_volume = 600\r\n";
    Bytes raw = {0xFF, 0xFE};
    auto u = utf16(iniText);
    raw.insert(raw.end(), u.begin(), u.end());
    auto cfg = N2System::SystemIniConfig::parseUtf16Le(raw);
    require(cfg.productName == "STEINS;GATE", "productName mismatch");
    require(cfg.startScript == "nss/0_boot.nss", "startScript mismatch");
    require(cfg.bgmVolume == 600, "bgmVolume mismatch");
  }

  // 2. Test <PRE> XML block parsing
  {
    auto blk = N2System::NsbVm::parsePreBlock(
        "text00010", "@box00",
        "<PRE @box00>[text00010]\n<voice name=\"Rintaro\" class=\"VID_OKA\" "
        "src=\"voice/OKA_0102\" mode=\"on\">\n\"El Psy "
        "Kongroo.\"<br>Done.</PRE>");
    require(blk.speaker == "Rintaro", "speaker mismatch");
    require(blk.voicePath == "voice/OKA_0102.ogg", "voicePath mismatch");
    require(blk.text == "\"El Psy Kongroo.\"\nDone.", "pre text mismatch");
  }

  // 3. Test .nsb bytecode execution: vars, arithmetic, texture, PreCreate,
  // WaitKey, Phone
  {
    Bytes nsb, map;
    addMap(map, 0, "chapter.main");
    addInsn(nsb, 1, 0x094, {"chapter.main"});
    addInsn(nsb, 2, 0x0d0, {"INT", "40"});
    addInsn(nsb, 3, 0x0d0, {"INT", "2"});
    addInsn(nsb, 4, 0x0a5, {}); // 40 + 2
    addInsn(nsb, 5, 0x0b0, {"$answer"});
    addInsn(nsb, 6, 0x08e, {});
    // CreateTexture("bg", 100, 0, 0, "cg/bg/bg01a.jpg")
    addInsn(nsb, 7, 0x0d0, {"STRING", "bg"});
    addInsn(nsb, 8, 0x0d0, {"INT", "100"});
    addInsn(nsb, 9, 0x0d0, {"INT", "0"});
    addInsn(nsb, 10, 0x0d0, {"INT", "0"});
    addInsn(nsb, 11, 0x0d0, {"STRING", "cg/bg/bg01a.jpg"});
    addInsn(nsb, 12, 0x00d, {"bg", "100", "0", "0", "cg/bg/bg01a.jpg"});
    addInsn(nsb, 13, 0x08e, {});
    // AddressBookSet("PhID_MAY")
    addInsn(nsb, 14, 0x0d0, {"STRING", "PhID_MAY"});
    addInsn(nsb, 15, 0x0f8, {"PhID_MAY"});
    addInsn(nsb, 16, 0x08e, {});
    // PreCreate
    addInsn(nsb, 17, 0x0d8,
            {"text00010", "@box00",
             "<PRE @box00>[text00010]\n<voice name=\"Okabe\" "
             "src=\"voice/oka_0001\">\nHello!"});
    // WaitKey
    addInsn(nsb, 18, 0x03a, {});
    addInsn(nsb, 19, 0x0d4, {"chapter.main"});

    N2System::NsbVm vm;
    vm.setLoader([&](std::string_view, Bytes &nsbOut, Bytes &mapOut) {
      nsbOut = nsb;
      mapOut = map;
      return true;
    });
    require(vm.boot("nss/0_boot.nss"), "boot failed");
    vm.step();

    require(vm.findVar("$answer") != nullptr &&
                vm.findVar("$answer")->asInt() == 42,
            "arithmetic/assign failed");
    const auto *bg = vm.sceneGraph().findEntity("bg");
    require(bg != nullptr && bg->imagePath == "cg/bg/bg01a.jpg",
            "CreateTexture failed");
    require(vm.phoneState().addressBook.count("PhID_MAY") == 1,
            "AddressBookSet failed");
    require(vm.dialogueSystem().speaker() == "Okabe",
            "dialogue speaker failed");
    require(vm.dialogueSystem().fullText() == "Hello!", "dialogue text failed");
    require(vm.waitingForKey(), "WaitKey did not pause");

    const auto beforeSave = vm.saveState();
    require(!beforeSave.runtimeBlob.empty(), "Runtime snapshot must be present");
    const auto jsonSave = N2System::SaveStateCodec::serializeJson(beforeSave);
    N2System::SaveData fromJson;
    require(N2System::SaveStateCodec::deserializeJson(jsonSave, fromJson),
            "JSON save must parse valid generated data");
    require(fromJson.runtimeBlob == beforeSave.runtimeBlob &&
                fromJson.integers == beforeSave.integers &&
                fromJson.strings == beforeSave.strings &&
                fromJson.backlog.size() == beforeSave.backlog.size(),
            "JSON debug save must round-trip full VM state and backlog");
    N2System::SaveData malformed;
    malformed.chapterName = "do not overwrite";
    require(!N2System::SaveStateCodec::deserializeJson(
                R"({"chapter":"ok","strings":{"bad":"\q"}})", malformed) &&
                malformed.chapterName == "do not overwrite",
            "Malformed JSON escapes must fail without modifying output");
    const auto saveBytes = N2System::SaveStateCodec::serializeBinary(beforeSave);
    N2System::SaveData parsed;
    require(N2System::SaveStateCodec::deserializeBinary(saveBytes, parsed),
            "Versioned N2SV must deserialize");
    require(parsed.runtimeBlob == beforeSave.runtimeBlob,
            "Runtime snapshot must round-trip without mutation");

    N2System::NsbVm loadedVm;
    loadedVm.setLoader([&](std::string_view, Bytes &nsbOut, Bytes &mapOut) {
      nsbOut = nsb;
      mapOut = map;
      return true;
    });
    require(loadedVm.boot("nss/0_boot.nss"), "restore VM boot failed");
    require(loadedVm.loadState(parsed), "VM restore failed");
    require(loadedVm.findVar("$answer") &&
                loadedVm.findVar("$answer")->asInt() == 42,
            "Runtime restore must preserve variables");
    require(loadedVm.phoneState().addressBook.count("PhID_MAY") == 1,
            "Runtime restore must preserve phone state");
    const auto *restoredBg = loadedVm.sceneGraph().findEntity("bg");
    require(restoredBg && restoredBg->imagePath == "cg/bg/bg01a.jpg",
            "Runtime restore must preserve image path and entity type");
    require(loadedVm.waitingForKey() &&
                loadedVm.dialogueSystem().fullText() == "Hello!" &&
                loadedVm.dialogueSystem().backlog().size() ==
                    vm.dialogueSystem().backlog().size(),
            "Runtime restore must preserve wait and dialogue/backlog state");

    auto truncated = saveBytes;
    truncated.pop_back();
    N2System::SaveData untouched;
    untouched.chapterName = "must-remain-unchanged";
    require(!N2System::SaveStateCodec::deserializeBinary(truncated, untouched) &&
                untouched.chapterName == "must-remain-unchanged",
            "Truncated save must fail atomically");
    auto corrupt = parsed;
    corrupt.runtimeBlob[0] ^= 0xff;
    require(!loadedVm.loadState(corrupt), "Invalid snapshot header rejected");

    loadedVm.advance();
    loadedVm.advance();
    loadedVm.step();
    require(loadedVm.finished(), "Restored VM must resume at original PC");

    vm.advance(); // finish typing
    vm.advance(); // clear WaitKey
    vm.step();
    require(vm.finished(), "VM should finish cleanly");
  }

  // The worker can spawn another process while the VM still holds a
  // reference to that worker. A std::vector<Thread> invalidated the reference.
  {
    Bytes nsb, map;
    addMap(map, 0, "chapter.main");
    auto ins = [&](uint16_t opcode, std::vector<std::string> args) {
      addInsn(nsb, static_cast<uint32_t>(nsb.size()+1), opcode, args);
    };
    ins(0x094, {"chapter.main"});
    const auto literal = [&](std::string v) { ins(0x0d0, {"STRING", v}); };
    literal("workerA"); literal("0"); literal("0"); literal("0"); literal("fnA");
    ins(0x009, {"workerA", "0", "0", "0", "fnA"});
    literal("workerA"); literal("start");
    ins(0x023, {"workerA", "start"});
    ins(0x03a, {});
    ins(0x097, {"fnA"});
    literal("workerB"); literal("0"); literal("0"); literal("0"); literal("fnB");
    ins(0x009, {"workerB", "0", "0", "0", "fnB"});
    literal("workerB"); literal("start");
    ins(0x023, {"workerB", "start"});
    ins(0x0d7, {});
    ins(0x097, {"fnB"});
    ins(0x0d0, {"INT", "77"});
    ins(0x0b0, {"$workerValue"});
    ins(0x0d7, {});

    N2System::NsbVm scheduler;
    scheduler.setLoader([&](std::string_view, Bytes &nsbOut, Bytes &mapOut) {
      nsbOut = nsb;
      mapOut = map;
      return true;
    });
    require(scheduler.boot("nss/0_boot.nss"), "scheduler boot");
    scheduler.step(4096);
    require(scheduler.threadCount() == 2,
            "Main creates workerA; workerA must await the next scheduler tick");
    require(!scheduler.findVar("$workerValue"),
            "New processes must not execute during their creator's frame");
    scheduler.step(4096);
    require(scheduler.threadCount() == 3,
            "workerA can spawn workerB without invalidating its own frame");
    require(!scheduler.findVar("$workerValue"),
            "workerB must wait until the next scheduler tick");
    scheduler.step(4096);
    require(scheduler.findVar("$workerValue") &&
                scheduler.findVar("$workerValue")->asInt() == 77,
            "Independently scheduled workerB runs on its next VM frame");
  }

  // Replace native built-in stubs with real media clock and persistence
  // callbacks, and verify the UTC-independent local date structure.
  {
    Bytes nsb, map;
    addMap(map, 0, "chapter.main");
    auto emit = [&](uint16_t op, std::vector<std::string> args) {
      addInsn(nsb, uint32_t(nsb.size() + 1), op, args);
    };
    const auto literal = [&](std::string type, std::string value) {
      emit(0x0d0, {std::move(type), std::move(value)});
    };
    emit(0x094, {"chapter.main"});
    literal("STRING", "movie/test.ngs");
    emit(0x05e, {"movie/test.ngs"});
    emit(0x0b0, {"$remain"});
    emit(0x08e, {});
    literal("STRING", "movie/test.ngs");
    emit(0x05f, {"movie/test.ngs"});
    emit(0x0b0, {"$duration"});
    emit(0x08e, {});
    literal("STRING", "movie/test.ngs");
    emit(0x060, {"movie/test.ngs"});
    emit(0x0b0, {"$passage"});
    emit(0x08e, {});
    literal("INT", "5");
    emit(0x052, {"5"});
    emit(0x0b0, {"$savePresent"});
    emit(0x08e, {});
    emit(0x056, {"$year", "$month", "$day", "$hour", "$minute", "$second"});
    emit(0x06c, {});
    emit(0x0b0, {"$elapsed"});
    emit(0x0d4, {"chapter.main"});
    N2System::NsbVm vm;
    vm.setLoader([&](std::string_view, Bytes &out, Bytes &debug) {
      out = nsb;
      debug = map;
      return true;
    });
    int mediaCalls = 0;
    vm.setMediaTimeQuery([&](std::string_view key, N2System::MediaTimeKind k) {
      require(key == "movie/test.ngs", "Media query must preserve asset key");
      ++mediaCalls;
      return std::int64_t(k == N2System::MediaTimeKind::Remaining ? 2000 :
                           k == N2System::MediaTimeKind::Duration ? 9000 : 7000);
    });
    vm.setSaveExistsQuery([](std::uint32_t slot) { return slot == 5; });
    require(vm.boot("nss/0_boot.nss"), "Builtins test boot");
    vm.update(7.5f);
    vm.step(4096);
    const auto value = [&vm](const char *name) -> std::int64_t {
      auto *v = vm.findVar(name);
      require(v != nullptr, "Built-in result missing");
      return v->asInt();
    };
    require(mediaCalls == 3 && value("$remain") == 2000 &&
                value("$duration") == 9000 && value("$passage") == 7000,
            "Native media time built-ins must report player clocks");
    require(value("$savePresent") == 1,
            "ExistSave must consult real storage callback");
    require(value("$elapsed") == 7,
            "Time builtin returns process elapsed seconds");
    require(value("$year") >= 2020 && value("$year") < 2200 &&
                value("$month") >= 1 && value("$month") <= 12 &&
                value("$day") >= 1 && value("$day") <= 31 &&
                value("$hour") >= 0 && value("$hour") < 24,
            "DateTime writes all six calendar outputs");
  }

  // Verify actual N2System opcodes, not just the SceneGraph helper methods:
  // libnpa maps SetShade=0x02d and SetTone=0x02e in retail NSB scripts.
  {
    Bytes nsb, map;
    addMap(map, 0, "chapter.main");
    const auto add = [&](uint16_t op, std::vector<std::string> args) {
      addInsn(nsb, uint32_t(nsb.size() + 1), op, args);
    };
    const auto string = [&](const char *value) {
      add(0x0d0, {"STRING", value});
    };
    add(0x094, {"chapter.main"});
    string("fx"); string("Monochrome");
    add(0x02e, {"fx", "Monochrome"});
    add(0x08e, {});
    string("fx"); string("Heavy");
    add(0x02d, {"fx", "Heavy"});
    add(0x08e, {});
    add(0x0d4, {"chapter.main"});
    N2System::NsbVm vm;
    vm.setLoader([&](std::string_view, Bytes &script, Bytes &symbols) {
      script = nsb;
      symbols = map;
      return true;
    });
    require(vm.boot("nss/0_boot.nss"), "Effects opcode test boot");
    vm.sceneGraph().createEntity("fx", N2System::EntityType::Sprite);
    vm.step(4096);
    const auto *effect = vm.sceneGraph().findEntity("fx");
    require(effect && effect->monochrome && effect->boxBlurPasses == 16,
            "NSB SetTone and SetShade apply grayscale + heavy blur");
    auto saved = vm.saveState();
    N2System::NsbVm restored;
    restored.setLoader([&](std::string_view, Bytes &script, Bytes &symbols) {
      script = nsb;
      symbols = map;
      return true;
    });
    require(restored.boot("nss/0_boot.nss") && restored.loadState(saved),
            "Effect state must resume across a full VM snapshot");
    const auto *restoredEffect = restored.sceneGraph().findEntity("fx");
    require(restoredEffect && restoredEffect->monochrome &&
                restoredEffect->boxBlurPasses == 16,
            "Saved scene retains shader effect selection");
  }

  std::cout << "N2System NsbVm tests passed\n";
  return 0;
}
