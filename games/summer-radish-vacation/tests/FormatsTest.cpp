#include <SummerRadish/Native.hpp>
#include <SummerRadish/Pack.hpp>
#include <SummerRadish/Script.hpp>
#include <SummerRadish/Transition.hpp>
#include <iostream>
#include <stdexcept>
void testGraphics();
namespace SummerRadish {
void testMap();
}
using namespace SummerRadish;
void check(bool c) {
  if (!c)
    throw std::runtime_error("Test failed");
}
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected);
}
int main() {
  try {
    check(normalizePlayerName("あいうえおか") == "あいうえおか");
    check(normalizePlayerName("\xef\xa3\xb1") == "?");
    rejects([] { normalizePlayerName(""); });
    rejects([] { normalizePlayerName("1234567"); });
    rejects([] { normalizePlayerName("\xc0\xaf"); });
    const std::array<std::string, 4> names{"間", "瞬", "はじめ", "しゅん"};
    auto edited = names;
    check(playerNameChangeMask(edited, names) == 0);
    edited[0] = "姓";
    check(playerNameChangeMask(edited, names) == 1);
    edited[3] = "なまえ";
    check(playerNameChangeMask(edited, names) == 3);
    check(substitutePlayerNames("\xef\xa3\xb0 / \xef\xa3\xb1", names) ==
          "間 / 瞬");
    check(parseXml("<X>&#xF8F1; &amp;#xF8F1; &#128512;</X>").text ==
          "\xef\xa3\xb1 &#xF8F1; \xf0\x9f\x98\x80");
    rejects([] { parseXml("<X>&#xD800;</X>"); });
    rejects([] { parseXml("<X>&#0;</X>"); });

    check(topLady({0, 0, 0, 0}) == 4 && topLady({1, 2, 2, 2}) == 5 &&
          topLady({0, 1, 3, 3}) == 3 && topLady({0, 1, 2, 3}) == 6);
    check(graphicSuffix(0).empty() && graphicSuffix(2) == "B" &&
          graphicSuffix(24) == "X" && graphicSuffix(0x10000) == "_H" &&
          graphicSuffix(0x13007) == "007_H");
    std::vector<CharacterPlacement> placement{{-1, 2}};
    assignCharacterPositions(placement);
    check(placement[0].previous == 0); // A sole automatic actor centers.
    placement = {{-1, 0}, {-1, 0}};
    assignCharacterPositions(placement);
    check(placement[0].previous == 1 && placement[1].previous == 2);
    assignCharacterPositions(placement);
    check(placement[0].previous == 1 && placement[1].previous == 2);
    placement = {{int32_t(0x40000002), 0}};
    assignCharacterPositions(placement);
    check(placement[0].previous == 0); // Conditional right means center alone.
    placement.push_back({int32_t(0x40000001), 0});
    assignCharacterPositions(placement);
    check(placement[0].previous == 2 && placement[1].previous == 1);
    placement = {{0, 0}, {-1, 2}};
    assignCharacterPositions(placement);
    check(placement[0].previous == 0 && placement[1].previous == 2);
    rejects([] {
      std::vector<CharacterPlacement> p;
      assignCharacterPositions(p, 0);
    });
    testGraphics();
    testMap();
    check(wipeDuration(1) == 300 && wipeDuration(2) == 1000 &&
          wipeDuration(32768) == 4000 && wipeDuration(2, 1000) == 0);
    Image dark;
    dark.width = dark.height = 1;
    dark.rgba = {0, 10, 20, 255};
    Image bright = dark;
    bright.rgba = {100, 110, 120, 255};
    check(wipeFrame(dark, bright, 2, 500, 1000).rgba ==
          Bytes({50, 60, 70, 255}));
    check(wipeFrame(dark, bright, 3, 0, 1000).rgba ==
          Bytes({255, 255, 255, 255}));
    check(wipeFrame(dark, bright, 4, 0, 1000).rgba == Bytes({0, 0, 0, 255}));
    check(decompress({2, 'a', 'b', 'c', 0x88, 2}, 7) ==
          Bytes({'a', 'b', 'c', 'a', 'b', 'c', 'a'}));
    rejects([] { decompress({0x80, 0}, 2); });
    rejects([] { decompress({2, 'a'}, 3); });
    check(utf16({0x3d, 0xd8, 0x00, 0xde}, 0, 2) == "😀");
    rejects([] { utf16({0x3d, 0xd8}, 0, 1); });
    auto root = parseXml(
        "<?xml version=\"1.0\"?><SCRIPT><!--x--><BLOCK label=\"a\"><CREATE "
        "type=\"STR\">a&amp;b&lt;c</CREATE></BLOCK></SCRIPT>");
    check(root.children[0].children[0].text == "a&b<c");
    rejects([] { parseXml("<SCRIPT><BLOCK></SCRIPT>"); });
    rejects([] { parseXml("<!DOCTYPE SCRIPT><SCRIPT/>"); });
    Machine vm;
    vm.add(
        "test",
        R"(<SCRIPT><BLOCK><OPERATOR type="="><CREATE type="LOCAL">x</CREATE><CREATE type="INT">2</CREATE></OPERATOR><CALL><CREATE type="STR">::sub</CREATE></CALL><IFBLOCK><IF><OPERATOR type="=="><CREATE type="LOCAL">x</CREATE><CREATE type="INT">3</CREATE></OPERATOR><FUNCTION name="YES"/></IF><IF><CREATE type="BOOL">TRUE</CREATE><FUNCTION name="NO"/></IF></IFBLOCK><FUNCTION name="WAIT"/><RETURN/></BLOCK><BLOCK label="sub"><OPERATOR type="="><CREATE type="LOCAL">x</CREATE><OPERATOR type="+"><CREATE type="LOCAL">x</CREATE><CREATE type="INT">1</CREATE></OPERATOR></OPERATOR><RETURN/></BLOCK></SCRIPT>)");
    std::string calls;
    vm.function = [&](const auto &n, const auto &) {
      calls += n;
      if (n == "WAIT")
        vm.waiting = true;
    };
    vm.start("test");
    vm.run();
    check(calls == "YESWAIT");
    check(number(vm.variables.at("@x")) == 3);
    auto saved = vm.save();
    Machine restored = vm;
    restored.restore(saved);
    check(restored.save() == saved);
    restored.resume();
    check(restored.ended);
    rejects([&] { vm.restore(saved + "junk"); });
    check(vm.save() == saved);
    // The native XML logical operators eagerly evaluate the RHS, even when
    // the LHS already determines the boolean result (FUN_100125d0).
    auto logical = parseXml(
        R"(<OPERATOR type="&amp;&amp;"><CREATE type="INT">0</CREATE><OPERATOR type="="><CREATE type="LOCAL">rhs</CREATE><CREATE type="INT">7</CREATE></OPERATOR></OPERATOR>)");
    check(number(vm.evaluate(logical)) == 0);
    check(number(vm.variables.at("@rhs")) == 7);
    logical = parseXml(
        R"(<OPERATOR type="||"><CREATE type="INT">1</CREATE><OPERATOR type="="><CREATE type="LOCAL">rhs</CREATE><CREATE type="INT">9</CREATE></OPERATOR></OPERATOR>)");
    check(number(vm.evaluate(logical)) == 1);
    check(number(vm.variables.at("@rhs")) == 9);
    // GSCRIPT is a persistent indexed reference, scoped by the current script;
    // the native marker stores a 16-bit resource index. LSCRIPT resets locally.
    Node globalRead = parseXml(R"(<CREATE type="GSCRIPT">65537</CREATE>)");
    Node localRead = parseXml(R"(<CREATE type="LSCRIPT">1</CREATE>)");
    vm.position.script = "ONE";
    check(number(vm.evaluate(globalRead)) == 0);
    vm.markRead(globalRead);
    vm.markRead(localRead);
    check(vm.hasRead(globalRead));
    check(vm.hasRead(parseXml(R"(<CREATE type="GSCRIPT">1</CREATE>)")));
    vm.position.script = "TWO";
    check(!vm.hasRead(globalRead));
    vm.position.script = "ONE";
    auto flagSave = vm.save();
    vm.start("test");
    vm.position.script = "ONE";
    check(vm.hasRead(globalRead));
    check(!vm.hasRead(localRead));
    // Use a real program name for persistence restoration.
    vm.position.script = "TEST";
    vm.markRead(globalRead);
    flagSave = vm.save();
    restored = vm;
    restored.restore(flagSave);
    check(restored.hasRead(globalRead));
    Bytes cgd = {};
    auto append = [&](uint32_t n) {
      for (int i = 0; i < 4; ++i)
        cgd.push_back((n >> (i * 8)) & 255);
    };
    append(20);
    append(1);
    append(1);
    append(4);
    cgd.insert(cgd.end(), {0xc0, 10, 20, 30, 255});
    auto image = decodeImage(cgd, "CGD");
    check(image.rgba == Bytes({30, 20, 10, 255}));
    cgd.pop_back();
    cgd.pop_back();
    rejects([&] { decodeImage(cgd, "CGD"); });
    std::cout << "Summer formats and script control flow passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
