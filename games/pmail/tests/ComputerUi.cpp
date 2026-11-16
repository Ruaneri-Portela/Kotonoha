#include <Pmail/Port.hpp>
#include <Pmail/Vm.hpp>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
Pmail::Arg integer(int value) {
  return {static_cast<uint32_t>(uint16_t(value))};
}
Pmail::Arg string(unsigned index) {
  return {(3u << 20) | (2u << 16) | index};
}
} // namespace

int main() {
  try {
    Kotonoha::PmailPort::PmailGame game({});
    auto object = std::make_shared<Pmail::ScriptObject>();
    object->id = 3;
    object->name = "synthetic-computer.obj";
    object->strings = {"mail", "hello", "12:30", "one", "two",
                       "$NO meets $HO", "ROOM"};
    game.vm().registerObject(object);
    const auto call = [&](int id, std::vector<Pmail::Arg> args) {
      game.native(game.vm(), id, args);
    };

    call(52, {string(0), integer(251), integer(160), integer(200),
              integer(100), integer(24), integer(10)});
    auto labels = game.computerText();
    check(labels.size() == 1 && labels[0].text == "mail" &&
              labels[0].x == 251 && labels[0].y == 160 &&
              labels[0].width == 200 && labels[0].height == 100 &&
              labels[0].size == 24 && labels[0].color == 10,
          "SYS 52 did not register positioned computer text");
    for (unsigned i = 0; i < 15; ++i)
      call(52, {string(0), integer(int(i)), integer(20), integer(50),
                integer(25), integer(16), integer(8)});
    check(game.computerText().size() == 10, "SYS 52 text slots are unbounded");

    call(80, {string(2)});
    check(game.computerClock() == "12:30", "SYS 80 clock missing");

    call(160, {integer(11), string(1)});
    call(162, {integer(2), string(3)});
    auto history = game.chatHistory();
    check(history.size() == 2 && history[0].mode == 11 &&
              history[0].text == "hello" && history[1].mode == 2 &&
              history[1].text == "one",
          "SYS 160 / 162 history order or modes incorrect");
    call(163, {});
    check(game.chatHistory().size() == 2,
          "SYS 163 redraw unexpectedly cleared chat");
    for (int i = 0; i < 17; ++i)
      call(162, {integer(i), string(4)});
    history = game.chatHistory();
    check(history.size() == 15 && history.front().mode == 2 &&
              history.back().mode == 16,
          "Chat history must retain the newest 15 entries");
    call(161, {});
    check(game.chatHistory().empty(), "SYS 161 did not clear chat");

    game.setPlayerNames("Alice", "Bob");
    check(game.expandPlayerNames("$NO and $HO / $NO") ==
              "Alice and Bob / Alice",
          "Chosen player names were not substituted");
    call(4, {string(5)});
    check(game.waitingForClick() && game.message() == "Alice meets Bob",
          "Dialogue did not expand customized $NO / $HO");
    call(160, {integer(1), string(5)});
    check(game.hasComputerOverlay() && game.chatHistory().back().text ==
              "Alice meets Bob",
          "Chat customization did not expand player names");
    call(48, {string(6)});
    check(!game.hasComputerOverlay() && game.computerText().empty() &&
              game.chatHistory().empty() && game.computerClock().empty(),
          "Changing the scene retained previous computer overlays");

    call(160, {integer(2), string(3)});
    check(game.hasComputerOverlay(), "Chat overlay did not activate");
    call(69, {integer(0), integer(0), integer(0)});
    check(!game.hasComputerOverlay(),
          "Clearing the scene retained computer overlays");

    const auto saved = game.serialize();
    check(saved.find("PMAIL-GAME-4") != std::string::npos &&
              saved.find("Alice") != std::string::npos,
          "Name customization not included in game save state");
    std::cout << "Pure Mail computer UI, player names, scene transitions"
                 " and save format passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
