// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/ScriptVm.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

static void check(bool value, const char *name) {
  if (!value)
    throw std::runtime_error(name);
}

int main() {
  try {
    // Test VariableStore
    N2System::VariableStore vars;
    vars.setInt("$counter", 10);
    vars.setString("@name", "Okabe");
    vars.setFlag("#flag_start", true);

    check(vars.getInt("$counter") == 10, "VariableStore getInt");
    check(vars.getString("@name") == "Okabe", "VariableStore getString");
    check(vars.getFlag("#flag_start") == true, "VariableStore getFlag");

    check(vars.evaluateInt("$counter + 5") == 15, "Expression evaluateInt +");
    check(vars.evaluateBool("$counter == 10") == true,
          "Expression evaluateBool ==");
    check(vars.evaluateString("@name + ' Rintarou'") == "Okabe Rintarou",
          "Expression evaluateString +");

    // Test SceneGraph
    N2System::SceneGraph scene;
    auto *bg = scene.createEntity("bg", N2System::EntityType::Sprite);
    bg->imagePath = "bg01.png";

    auto *ch = scene.createEntity("kurisu", N2System::EntityType::Sprite);
    ch->imagePath = "chr01.png";

    scene.moveEntity("kurisu", 100.0f, 200.0f, 0.0f);
    check(ch->x == 100.0f && ch->y == 200.0f, "SceneGraph moveEntity");

    scene.fadeEntity("kurisu", 0.5f, 0.0f);
    check(ch->opacity == 0.5f, "SceneGraph fadeEntity");

    // Test AudioSystem
    N2System::AudioSystem audio;
    audio.playBgm("bgm01.ogg", true, 0.0f);
    check(audio.bgmChannel().isPlaying, "AudioSystem playBgm");
    check(audio.bgmChannel().assetPath == "bgm01.ogg", "AudioSystem bgm path");

    // Test DialogueSystem
    N2System::DialogueSystem dialogue;
    dialogue.setDialogue("Okabe", "El Psy Kongroo", "voice01.ogg");
    check(dialogue.speaker() == "Okabe", "DialogueSystem speaker");
    check(dialogue.fullText() == "El Psy Kongroo", "DialogueSystem fullText");
    check(!dialogue.backlog().empty(), "DialogueSystem backlog");

    // Test SaveStateCodec
    N2System::SaveData saveData;
    saveData.chapterName = "main";
    saveData.position = 5;
    saveData.integers["$counter"] = 42;
    saveData.strings["@name"] = "Mayuri";

    auto json = N2System::SaveStateCodec::serializeJson(saveData);
    N2System::SaveData loadedJson;
    check(N2System::SaveStateCodec::deserializeJson(json, loadedJson),
          "SaveState deserializeJson");
    check(loadedJson.chapterName == "main", "SaveData chapter match");

    auto bytes = N2System::SaveStateCodec::serializeBinary(saveData);
    N2System::SaveData loadedBin;
    check(N2System::SaveStateCodec::deserializeBinary(bytes, loadedBin),
          "SaveState deserializeBinary");
    check(loadedBin.integers["$counter"] == 42, "SaveData integer match");

    // Test ScriptVm
    const std::string script = "chapter main {\n"
                               "SetVariable(\"$count\", \"1\");\n"
                               "SetDialogue(\"Okabe\", \"Hello World!\");\n"
                               "WaitKey();\n"
                               "CreateSprite(\"bg\", \"bg.png\");\n"
                               "PlayBgm(\"music.ogg\");\n"
                               "Finish();\n"
                               "}\n";

    N2System::ScriptVm vm;
    vm.loadScript(script, "main");
    vm.step(2); // Runs SetVariable and SetDialogue

    check(vm.dialogueSystem().speaker() == "Okabe", "VM Dialogue speaker");
    check(vm.dialogueSystem().fullText() == "Hello World!", "VM Dialogue text");

    vm.step(1); // Runs WaitKey -> pauses
    check(vm.dialogueSystem().isWaitingForKey(), "VM Waiting for key");

    vm.advance(); // User advances
    vm.step(10);  // Finish script

    check(vm.finished(), "VM script finished");

    // Test N2System ScriptIndex
    N2System::ScriptIndex index = N2System::ScriptIndex::Parse(
        "chapter _001 {\n  PlayVoice(\"000104\");\n}\n");
    check(!index.chapters().empty(), "ScriptIndex parsed chapters");

    N2System::ScriptVm vmConverted;
    vmConverted.loadScript(
        "chapter _001 {\n  SetDialogue(\"Saya\", \"Sako...\");\n}\n", "_001");
    vmConverted.step(5);
    check(vmConverted.dialogueSystem().speaker() == "Saya", "N2System speaker");
    check(vmConverted.dialogueSystem().fullText() == "Sako...",
          "N2System text");

    // Native debugger jumps to a ScriptIndex label without replaying earlier
    // expressions or leaving a stale WaitKey block in the dialogue system.
    const std::string labelSource =
        "chapter main {\n"
        "start:\n"
        "  SetDialogue(\"First\", \"Old dialogue\");\n"
        "  WaitKey();\n"
        "next:\n"
        "  SetDialogue(\"Second\", \"New dialogue\");\n"
        "  WaitKey();\n"
        "}\n";
    N2System::ScriptVm debugVm;
    debugVm.loadScript(labelSource, "main");
    debugVm.step(2);
    check(debugVm.dialogueSystem().isWaitingForKey(),
          "Native VM is blocked before debug jump");
    check(!debugVm.debugJumpToLabel("nonexistent") &&
              debugVm.dialogueSystem().isWaitingForKey(),
          "Unknown label does not mutate the VM");
    check(debugVm.debugJumpToLabel("next") &&
              !debugVm.dialogueSystem().isWaitingForKey(),
          "Valid label clears stale dialogue block");
    debugVm.step(1);
    check(debugVm.dialogueSystem().speaker() == "Second" &&
              debugVm.dialogueSystem().fullText() == "New dialogue",
          "Native debug label jump executes selected destination");

    std::cout << "All N2System VM tests passed!\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "ScriptVmTest error: " << e.what() << "\n";
    return 1;
  }
}
