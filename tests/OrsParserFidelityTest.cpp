extern "C" {
#include <Kotonoha/parsers/Ors.h>
}

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct Fixture {
	fs::path path;
	Kotonoha_orsData events{};

	explicit Fixture(const std::string& lines) {
		const auto stamp = std::chrono::steady_clock::now()
			.time_since_epoch().count();
		path = fs::temp_directory_path() /
			("kotonoha_ors_parser_" + std::to_string(stamp) + ".ors");
		{
			std::ofstream output(path, std::ios::binary);
			Require(static_cast<bool>(output), "could not create ORS fixture");
			output << lines;
		}
		events = Kotonoha_OrsParser(path.string().c_str());
	}

	~Fixture() {
		Kotonoha_OrsClean(&events);
		std::error_code error;
		fs::remove(path, error);
	}

	Fixture(const Fixture&) = delete;
	Fixture& operator=(const Fixture&) = delete;
};

std::vector<Kotonoha_orsEvent*> Collect(const Kotonoha_orsData& events) {
	std::vector<Kotonoha_orsEvent*> result;
	for (auto* event = events.data; event != nullptr; event = event->next) {
		result.push_back(event);
	}
	Require(result.size() == events.size, "ORS event list size mismatch");
	return result;
}

void RunFixtures() {
	{
		Fixture fixture(
			"[CreateBG]=00:00:00\tBGS\tEvent00/base\t00:02:00;\n"
			"[PrintText]=00:01:00\tMakoto\tHello\t00:02:00;\n"
			"[PlayVoice]=00:01:00\tVoice00/line\t1\tmak\t00:02:00;\n"
			"[PlaySe]=00:02:00\t1\tSe00/hit\t00:03:00;\n");
		const auto events = Collect(fixture.events);
		Require(events.size() == 4, "normal event count");
		Require(events[1]->command == PRINT_TEXT &&
			events[2]->command == PLAY_VOICE &&
			events[1]->start == events[2]->start,
			"same-start source order/timestamps changed");
		Require(events[1]->end == events[3]->start,
			"adjacent event timestamps were altered");
	}

	{
		Fixture fixture(
			"[PlayVoice]=00:00:01\tVoice02/line\t\t\t00:01:00;\n");
		const auto events = Collect(fixture.events);
		Require(events.size() == 1 && events[0]->command == PLAY_VOICE,
			"empty PlayVoice fields were discarded");
		Require(events[0]->data.play_voice->a == 0 &&
			events[0]->data.play_voice->character_short != nullptr &&
			std::string(events[0]->data.play_voice->character_short).empty(),
			"empty PlayVoice fields were not preserved");
	}

	{
		Fixture fixture(
			"[PlayVoice]=00:00:01\tVoice02/line\t\t00:01:00;\n");
		Require(Collect(fixture.events).empty(),
			"malformed PlayVoice was accepted");
	}

	{
		Fixture fixture(
			"[PrintText]=02:52:05\tMakoto\tI know; me too.\t02:56:09;\n");
		const auto events = Collect(fixture.events);
		Require(events.size() == 1 &&
			std::string(events[0]->data.print_text->text) == "I know; me too.",
			"semicolon in dialogue truncated the event");
	}

	{
		Fixture fixture(
			"[SetSELECT]=00:00:00\t'First'\tnull\t00:05:00;\n"
			"[SetSELECT]=00:05:00\t'Second'\tNULL\t00:10:00;\n"
			"[SetSELECT]=00:10:00\t'First'\tnullish\t00:15:00;\n");
		const auto events = Collect(fixture.events);
		Require(events.size() == 3 &&
			events[0]->data.set_select->size == 1 &&
			events[1]->data.set_select->size == 1 &&
			events[2]->data.set_select->size == 2,
			"SetSELECT null token filtering is incorrect");
	}

	{
		Fixture fixture(
			"[PlaySe]=invalid\t1\tSe00/hit\t00:03:00;\n"
			"[PlaySe]=00:00:00, 1, Se00/hit, 00:03:00;\n");
		Require(Collect(fixture.events).empty(),
			"malformed ORS timestamp/fields were accepted");
	}

	{
		Fixture fixture(
			"[PrintTextExtra]=00:00:00\tMakoto\tHello\t00:02:00;\n");
		const auto events = Collect(fixture.events);
		Require(events.size() == 1 && events[0]->command == UNKNOWN,
			"command prefix was mistaken for a known command");
	}

	{
		Fixture fixture("[SkipFRAME]=01:02:09;\n[Next]=01:09:00;\n");
		const auto events = Collect(fixture.events);
		Require(events.size() == 2 &&
			events[0]->command == SkipFRAME &&
			events[1]->command == Next &&
			events[0]->start != events[1]->start,
			"single-field event timestamps were altered");
	}
}
} // namespace

int main() {
	RunFixtures();
	return 0;
}
