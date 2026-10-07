#include <Kotonoha/SchoolDaysAudioEventState.hpp>
#include <Kotonoha/SchoolDaysSceneTimeline.hpp>
#include <Kotonoha/Kotonoha.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Kotonoha::SchoolDaysSeSlots;
using Kotonoha::SchoolDaysSceneTimeline;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Fixture {
    fs::path path;
    Kotonoha_orsData events{};
    explicit Fixture(const char* lines) {
        static unsigned sequence = 0;
        path = fs::temp_directory_path() /
            ("kotonoha_g2_3a_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
                "_" + std::to_string(++sequence) + ".ors");
        std::ofstream out(path, std::ios::binary);
        Require(static_cast<bool>(out), "fixture create");
        out << lines;
        out.close();
        events = Kotonoha_OrsParser(path.string().c_str());
    }
    ~Fixture() {
        Kotonoha_OrsClean(&events);
        std::error_code error;
        fs::remove(path, error);
    }
};

void TestVoicePolicyAndKeys() {
    Kotonoha_Game context{};
    Kotonoha_SetMenVoiceEnabled(&context, true);
    Require(Kotonoha_IsMenVoiceEnabled(&context), "default enabled setting API");
    Kotonoha_SetMenVoiceEnabled(&context, false);
    Require(!Kotonoha_IsMenVoiceEnabled(&context), "disabled setting API");
    Require(Kotonoha::SchoolDaysVoiceAllowed(0, false),
            "numeric 0 incorrectly depends on MenVoice");
    Require(Kotonoha::SchoolDaysVoiceAllowed(1, true) &&
                !Kotonoha::SchoolDaysVoiceAllowed(1, false),
            "numeric 1 MenVoice gate");
    Require(!Kotonoha::SchoolDaysVoiceAllowed(7, false),
            "nonzero numeric should use the same gate");
    Fixture fixture(
        "[PlayVoice]=00:00:00\tVoice/missing\t\t\t00:01:00;\n"
        "[PlayVoice]=00:01:00\tVoice/mak\t1\tmak\t00:02:00;\n"
        "[PlayVoice]=00:02:00\tVoice/x01\t0\tx01\t00:03:00;\n");
    Require(fixture.events.size == 3, "voice fixture parse");
    auto* empty = fixture.events.data;
    Require(empty->data.play_voice->a == 0 &&
                std::string(Kotonoha::SchoolDaysVoiceAnimationKey(empty)).empty() &&
                Kotonoha::SchoolDaysVoiceAllowed(empty->data.play_voice->a, false),
            "empty numeric/key were discarded or blocked");
    Require(std::string(Kotonoha::SchoolDaysVoiceAnimationKey(empty->next)) == "mak" &&
                std::string(Kotonoha::SchoolDaysVoiceAnimationKey(empty->next->next)) == "x01",
            "animation keys were filtered");
    SchoolDaysSceneTimeline timeline;
    timeline.Bind(&fixture.events);
    Require(timeline.AdvanceTo(72).size() == 3,
            "missing voice resource must not block the timeline");
}

void TestSeSlotsAndSeek() {
    Fixture fixture(
        "[PlaySe]=00:00:10\t2\tSe/A\t00:00:20;\n"
        "[PlaySe]=00:00:12\t3\tSe/C\t00:00:21;\n"
        "[PlaySe]=00:00:14\t2\tSe/B\t00:00:22;\n"
        "[PlaySe]=00:00:15\t0\tSe/zero\t00:00:22;\n"
        "[PlaySe]=00:00:16\t8\tSe/eight\t00:00:22;\n"
        "[Next]=00:01:00;\n");
    Require(fixture.events.size == 6, "SE fixture parse");
    auto* a = fixture.events.data;
    auto* c = a->next;
    auto* b = c->next;
    SchoolDaysSeSlots slots;
    SchoolDaysSeSlots::Slot old;
    int fakeA = 1, fakeB = 2, fakeC = 3;
    auto* mediaA = reinterpret_cast<Kotonoha_audioDecode*>(&fakeA);
    auto* mediaB = reinterpret_cast<Kotonoha_audioDecode*>(&fakeB);
    auto* mediaC = reinterpret_cast<Kotonoha_audioDecode*>(&fakeC);
    Require(slots.Replace(2, a, mediaA, &old) && old.event == nullptr,
            "initial slot 2");
    Require(slots.Replace(3, c, mediaC, &old) && old.event == nullptr &&
                slots.At(2).event == a && slots.At(2).media == mediaA,
            "different slots did not coexist");
    Require(slots.Replace(2, b, mediaB, &old) && old.event == a &&
                old.media == mediaA && slots.At(2).event == b &&
                slots.At(2).media == mediaB && slots.At(3).media == mediaC,
            "same slot replacement");
    Require(slots.Replace(0, b->next, nullptr) &&
                slots.Replace(8, b->next->next, nullptr) &&
                slots.At(0).event != nullptr && slots.At(8).event != nullptr,
            "boundary slots 0/8");
    Require(!SchoolDaysSeSlots::Valid(9) &&
                !slots.Replace(9, a, nullptr) && slots.At(9).event == nullptr,
            "out-of-range slot was accepted");

    auto at13 = SchoolDaysSeSlots::ActiveEventsAt(
        &fixture.events, Kotonoha_SceneTickToMillisecondsCeil(13));
    Require(at13[2] == a && at13[3] == c,
            "seek before replacement lost active slots");
    auto at14 = SchoolDaysSeSlots::ActiveEventsAt(
        &fixture.events, Kotonoha_SceneTickToMillisecondsCeil(14));
    Require(at14[2] == b && at14[3] == c,
            "seek after replacement revived old slot occupant");
    auto at23 = SchoolDaysSeSlots::ActiveEventsAt(
        &fixture.events, Kotonoha_SceneTickToMillisecondsCeil(23));
    Require(at23[2] == nullptr && at23[3] == nullptr,
            "expired events revived after seek");

    SchoolDaysSceneTimeline timeline;
    timeline.Bind(&fixture.events);
    Require(timeline.AdvanceTo(9).empty(), "SE fired early");
    auto crossed = timeline.AdvanceTo(16);
    Require(crossed.size() == 5 && crossed[0] == a && crossed[2] == b,
            "multi-tick SE order was lost");
}

void TestMixedTimeline() {
    Fixture fixture(
        "[PlaySe]=00:00:10\t2\tSe/A\t00:00:20;\n"
        "[PlayVoice]=00:00:12\tVoice/mak\t1\tmak\t00:00:20;\n"
        "[PlaySe]=00:00:14\t2\tSe/B\t00:00:22;\n"
        "[Next]=00:01:00;\n");
    SchoolDaysSceneTimeline timeline;
    timeline.Bind(&fixture.events);
    Require(timeline.AdvanceTo(0).empty(), "early mixed event");
    auto due = timeline.AdvanceTo(14);
    Require(due.size() == 3 && due[0]->command == PLAY_SE &&
                due[1]->command == PLAY_VOICE && due[2]->command == PLAY_SE,
            "SE/voice/SE dispatch order");
    SchoolDaysSeSlots slots;
    SchoolDaysSeSlots::Slot old;
    slots.Replace(2, due[0], nullptr);
    Require(Kotonoha::SchoolDaysVoiceAllowed(due[1]->data.play_voice->a, true),
            "mixed voice activation");
    slots.Replace(2, due[2], nullptr, &old);
    Require(old.event == due[0] && slots.At(2).event == due[2],
            "mixed same-slot replacement");
    timeline.Restart();
    auto jumped = timeline.AdvanceTo(14);
    Require(jumped.size() == 3 && jumped[0] == due[0] &&
                jumped[1] == due[1] && jumped[2] == due[2],
            "direct jump differs from stepped advance");
}
} // namespace

int main() {
    try {
        TestVoicePolicyAndKeys();
        TestSeSlotsAndSeek();
        TestMixedTimeline();
        std::cout << "SchoolDaysAudioEventTest PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SchoolDaysAudioEventTest FAIL: " << error.what() << '\n';
        return 1;
    }
}
