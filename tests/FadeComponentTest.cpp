#include <Kotonoha/components/Fade.hpp>
#include <Kotonoha/utils/OrsTime.h>

#include <cstdio>

int main() {
	using Kotonoha::Fade;
	using Kotonoha::FadeDirection;

	const struct {
		Uint64 tick;
		Uint8 expected;
		FadeDirection direction;
	} cases[] = {
		{ 10, 255, FadeDirection::In },
		{ 15, 128, FadeDirection::In },
		{ 20, 0, FadeDirection::In },
		{ 10, 0, FadeDirection::Out },
		{ 15, 127, FadeDirection::Out },
		{ 20, 255, FadeDirection::Out },
		{ 5, 255, FadeDirection::In },
		{ 25, 0, FadeDirection::In },
	};

	for (const auto& test : cases) {
		const Uint8 actual =
			Fade::AlphaAtTick(test.tick, 10, 20, test.direction);
		if (actual != test.expected) {
			std::fprintf(stderr,
				"fade alpha mismatch at tick %llu: got %u, expected %u\n",
				static_cast<unsigned long long>(test.tick),
				static_cast<unsigned int>(actual),
				static_cast<unsigned int>(test.expected));
			return 1;
		}
	}

	if (Fade::AlphaAtTick(10, 10, 10, FadeDirection::In) != 0 ||
		Fade::AlphaAtTick(10, 10, 10, FadeDirection::Out) != 255) {
		std::fprintf(stderr, "zero-duration fade endpoints are incorrect\n");
		return 1;
	}

	Uint64 tick = 0;
	Uint64 milliseconds = 0;
	if (!Kotonoha_OrsTimeToTick(10, false, &tick) || tick != 2 ||
		!Kotonoha_OrsTimeToMilliseconds(10, false, &milliseconds) ||
		milliseconds != 42) {
		std::fprintf(stderr, "ORS frame timestamp conversion is incorrect\n");
		return 1;
	}

	return 0;
}
